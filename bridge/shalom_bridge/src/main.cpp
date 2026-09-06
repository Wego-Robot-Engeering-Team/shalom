#include <rclcpp/rclcpp.hpp>

#include "shalom_bridge/bridge_node.hpp"

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<shalom_bridge::BridgeNode>());
    } catch (const std::exception &e) {
        RCLCPP_FATAL(rclcpp::get_logger("shalom_bridge"), "기동 실패: %s", e.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
