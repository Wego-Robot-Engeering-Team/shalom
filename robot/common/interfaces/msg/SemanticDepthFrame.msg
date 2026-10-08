# One acquisition cycle from a semantic/depth camera.
# header.stamp: host ROS receipt time. Device timestamps remain unmodified.
std_msgs/Header header
uint64 semantic_timestamp_ns
uint64 depth_timestamp_ns

# Camera preview and its semantic labels (mono8 class IDs).
# Missing optional images have width=height=0 and empty data.
sensor_msgs/Image camera
sensor_msgs/Image semantic

# Organized optical XYZ in metres (32FC3: right, down, forward).
# Semantic labels registered to the depth pixels (mono8 class IDs).
sensor_msgs/Image depth_points
sensor_msgs/Image depth_labels
sensor_msgs/Image texture
