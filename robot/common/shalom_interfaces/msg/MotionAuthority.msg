# Mutually exclusive motion authority owned by motion_interlock_manager.
uint8 NONE=0
uint8 BASE_ACTIVE=1
uint8 BASE_STOPPING=2
uint8 ARM_ACTIVE=3
uint8 ARM_STOPPING=4

builtin_interfaces/Time stamp
uint64 sequence
uint8 state

# Authority to apply after the active resource reports a confirmed stop.
uint8 pending

# Logical requester holding or waiting for authority.
string owner
string reason_code
string detail
