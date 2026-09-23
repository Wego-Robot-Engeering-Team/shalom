# Typed internal safety event. Operator commands use SafetyCommand instead.
uint8 REQUEST_STOP=1
uint8 HEALTH_FAULT=2

builtin_interfaces/Time stamp
uint64 sequence
uint8 event
string source
string reason_code
string detail
