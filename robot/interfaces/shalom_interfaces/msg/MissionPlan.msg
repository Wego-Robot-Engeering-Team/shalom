# Immutable mission snapshot owned by mission_manager after configuration.
builtin_interfaces/Time created_at
uint64 revision
string mission_id
string map_id

shalom_interfaces/MissionWaypoint[] waypoints

# Dock approach is a navigation target only. It never means that physical
# docking or charging succeeded.
bool return_to_dock
bool has_dock_approach
shalom_interfaces/MissionWaypoint dock_approach

# Capabilities required by the mission as a whole, in addition to per-waypoint
# requirements.
string[] required_capabilities
