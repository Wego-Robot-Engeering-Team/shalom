# Operational safety state owned by safety_manager.
uint8 INITIALIZING=0
uint8 CONTROLLED_STOP=1
uint8 NORMAL=2
uint8 E_STOP_LATCHED=3
uint8 FAULT=4

builtin_interfaces/Time stamp
uint64 sequence
uint8 state
bool motion_permitted
bool physical_estop_active
bool software_estop_active

# Source which caused the most recent accepted transition.
string source
string reason_code
string detail
