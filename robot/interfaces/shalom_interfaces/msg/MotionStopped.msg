# Fresh stopped feedback used by motion_interlock_manager.
uint8 BASE=1
uint8 ARM=2

builtin_interfaces/Time stamp
uint64 sequence
uint8 resource
bool stopped
string source

# Human-readable context only. Safety logic must not parse this field.
string detail
