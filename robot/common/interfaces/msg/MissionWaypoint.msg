# Operation performed at this waypoint.
uint8 NAVIGATE_ONLY=0
uint8 INSPECT=1

string waypoint_id
geometry_msgs/PoseStamped target_pose
uint8 operation

# Capabilities required to execute this waypoint. The mission manager rejects
# the whole plan before READY if any capability is unavailable.
string[] required_capabilities
