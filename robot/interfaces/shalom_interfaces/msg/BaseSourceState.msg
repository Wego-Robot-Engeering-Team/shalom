# Mutually exclusive owner of the autonomous base command path.
uint8 NONE=0
uint8 NAV=1
uint8 DOCK=2
uint8 STAIR=3
uint8 MISSION=4

uint8 INACTIVE=0
uint8 TRANSITIONING=1
uint8 ACTIVE=2
uint8 FAULT=3

builtin_interfaces/Time stamp
uint64 sequence
uint8 phase
uint8 active_source
uint8 requested_source
# Requester with a pending or active claim; empty when no source is owned.
string owner_requester
string reason_code
string detail
