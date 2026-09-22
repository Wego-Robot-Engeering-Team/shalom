# Mission lifecycle owned by mission_manager.
uint8 IDLE=0
uint8 READY=1
uint8 RUNNING=2
uint8 PAUSING=3
uint8 PAUSED=4
uint8 RECOVERING=5
uint8 RETURNING=6
uint8 COMPLETED=7
uint8 FAILED=8

builtin_interfaces/Time stamp
uint64 sequence
string mission_id
uint8 state

# Zero-based active step. -1 means that no step is active.
int32 current_step
uint32 total_steps
bool return_to_dock

# Stable diagnostic identifier. Consumers must not parse detail.
string reason_code
string detail
