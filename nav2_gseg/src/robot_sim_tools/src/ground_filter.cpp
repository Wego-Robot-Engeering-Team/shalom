#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"

namespace
{
struct GroundCell
{
  double sum_x{0.0};
  double sum_y{0.0};
  double sum_z{0.0};
  std::size_t count{0};
};

class GroundFilter : public rclcpp::Node
{
public:
  GroundFilter()
  : Node("ground_filter")
  {
    min_height_ = declare_parameter<double>("min_height_above_ground", 0.15);
    max_height_ = declare_parameter<double>("max_height_above_ground", 1.20);
    cell_size_ = declare_parameter<double>("ground_cell_size", 0.35);
    search_radius_ = declare_parameter<double>("ground_search_radius", 1.0);

    if (cell_size_ <= 0.0 || search_radius_ < 0.0 || min_height_ < 0.0 || max_height_ <= min_height_) {
      throw std::invalid_argument("Invalid ground-relative height filter parameters");
    }

    filtered_publisher_ = create_publisher<sensor_msgs::msg::PointCloud2>(
      "filtered", rclcpp::SensorDataQoS());
    ground_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      "ground", rclcpp::SensorDataQoS(),
      std::bind(&GroundFilter::groundCallback, this, std::placeholders::_1));
    obstacle_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      "obstacles", rclcpp::SensorDataQoS(),
      std::bind(&GroundFilter::obstacleCallback, this, std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(), "Keeping obstacle points %.2f to %.2f m above local ground",
      min_height_, max_height_);
  }

private:
  using Cloud = sensor_msgs::msg::PointCloud2;

  static const sensor_msgs::msg::PointField * findField(const Cloud & cloud, const std::string & name)
  {
    for (const auto & field : cloud.fields) {
      if (field.name == name) {
        return &field;
      }
    }
    return nullptr;
  }

  static bool readFloat32(
    const uint8_t * point, const sensor_msgs::msg::PointField * field,
    uint32_t point_step, float & value)
  {
    if (field == nullptr || field->datatype != sensor_msgs::msg::PointField::FLOAT32 ||
      field->offset + sizeof(float) > point_step)
    {
      return false;
    }
    std::memcpy(&value, point + field->offset, sizeof(float));
    return std::isfinite(value);
  }

  int64_t cellKey(int x, int y) const
  {
    return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(y);
  }

  std::pair<int, int> cellIndex(float x, float y) const
  {
    return {
      static_cast<int>(std::floor(static_cast<double>(x) / cell_size_)),
      static_cast<int>(std::floor(static_cast<double>(y) / cell_size_))};
  }

  bool fieldsSupported(const Cloud & cloud) const
  {
    const auto * x = findField(cloud, "x");
    const auto * y = findField(cloud, "y");
    const auto * z = findField(cloud, "z");
    return x != nullptr && y != nullptr && z != nullptr &&
           x->datatype == sensor_msgs::msg::PointField::FLOAT32 &&
           y->datatype == sensor_msgs::msg::PointField::FLOAT32 &&
           z->datatype == sensor_msgs::msg::PointField::FLOAT32;
  }

  template<typename Callback>
  void forEachPoint(const Cloud & cloud, Callback callback) const
  {
    const auto * x_field = findField(cloud, "x");
    const auto * y_field = findField(cloud, "y");
    const auto * z_field = findField(cloud, "z");
    for (uint32_t row = 0; row < cloud.height; ++row) {
      const auto row_offset = static_cast<std::size_t>(row) * cloud.row_step;
      for (uint32_t column = 0; column < cloud.width; ++column) {
        const auto point_offset = row_offset + static_cast<std::size_t>(column) * cloud.point_step;
        if (point_offset + cloud.point_step > cloud.data.size()) {
          return;
        }
        const auto * point = cloud.data.data() + point_offset;
        float x;
        float y;
        float z;
        if (readFloat32(point, x_field, cloud.point_step, x) &&
          readFloat32(point, y_field, cloud.point_step, y) &&
          readFloat32(point, z_field, cloud.point_step, z))
        {
          callback(point, x, y, z);
        }
      }
    }
  }

  void groundCallback(const Cloud::SharedPtr message)
  {
    if (!fieldsSupported(*message)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Ground cloud must provide float32 x, y, and z fields");
      return;
    }

    ground_cells_.clear();
    ground_frame_ = message->header.frame_id;
    forEachPoint(*message, [this](const uint8_t *, float x, float y, float z) {
      const auto [cell_x, cell_y] = cellIndex(x, y);
      auto & cell = ground_cells_[cellKey(cell_x, cell_y)];
      cell.sum_x += x;
      cell.sum_y += y;
      cell.sum_z += z;
      ++cell.count;
    });
  }

  std::optional<double> localGroundHeight(float x, float y) const
  {
    if (ground_cells_.empty()) {
      return std::nullopt;
    }

    const auto [center_x, center_y] = cellIndex(x, y);
    const int cell_radius = static_cast<int>(std::ceil(search_radius_ / cell_size_));
    const double max_distance_squared = search_radius_ * search_radius_;
    double nearest_distance_squared = std::numeric_limits<double>::infinity();
    std::optional<double> height;

    for (int dx = -cell_radius; dx <= cell_radius; ++dx) {
      for (int dy = -cell_radius; dy <= cell_radius; ++dy) {
        const auto iterator = ground_cells_.find(cellKey(center_x + dx, center_y + dy));
        if (iterator == ground_cells_.end() || iterator->second.count == 0) {
          continue;
        }
        const auto & cell = iterator->second;
        const double ground_x = cell.sum_x / static_cast<double>(cell.count);
        const double ground_y = cell.sum_y / static_cast<double>(cell.count);
        const double distance_squared = (x - ground_x) * (x - ground_x) + (y - ground_y) * (y - ground_y);
        if (distance_squared <= max_distance_squared && distance_squared < nearest_distance_squared) {
          nearest_distance_squared = distance_squared;
          height = cell.sum_z / static_cast<double>(cell.count);
        }
      }
    }
    return height;
  }

  void obstacleCallback(const Cloud::SharedPtr message)
  {
    if (!fieldsSupported(*message)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Obstacle cloud must provide float32 x, y, and z fields");
      return;
    }
    if (ground_cells_.empty() || message->header.frame_id != ground_frame_) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "No matching ground cloud available; waiting before publishing SLAM points");
      return;
    }

    Cloud filtered = *message;
    filtered.height = 1;
    filtered.width = 0;
    filtered.row_step = 0;
    filtered.data.clear();
    filtered.data.reserve(message->data.size());
    filtered.is_dense = false;

    std::size_t source_points = 0;
    std::size_t kept_points = 0;
    forEachPoint(*message, [this, &message, &filtered, &source_points, &kept_points](
      const uint8_t * point, float x, float y, float z)
      {
        ++source_points;
        const auto ground_z = localGroundHeight(x, y);
        if (!ground_z.has_value()) {
          return;
        }
        const double height_above_ground = static_cast<double>(z) - *ground_z;
        if (height_above_ground < min_height_ || height_above_ground > max_height_) {
          return;
        }
        filtered.data.insert(filtered.data.end(), point, point + message->point_step);
        ++kept_points;
      });

    filtered.width = static_cast<uint32_t>(kept_points);
    filtered.row_step = filtered.width * filtered.point_step;
    filtered_publisher_->publish(std::move(filtered));
    RCLCPP_DEBUG_THROTTLE(
      get_logger(), *get_clock(), 2000,
      "SLAM ground filter kept %zu/%zu obstacle points", kept_points, source_points);
  }

  double min_height_;
  double max_height_;
  double cell_size_;
  double search_radius_;
  std::string ground_frame_;
  std::unordered_map<int64_t, GroundCell> ground_cells_;
  rclcpp::Publisher<Cloud>::SharedPtr filtered_publisher_;
  rclcpp::Subscription<Cloud>::SharedPtr ground_subscription_;
  rclcpp::Subscription<Cloud>::SharedPtr obstacle_subscription_;
};
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<GroundFilter>());
  rclcpp::shutdown();
  return 0;
}
