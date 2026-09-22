# Liveness input consumed by safety_manager.
builtin_interfaces/Time stamp
uint64 sequence
string source
bool alive

# The receiver uses local steady-clock arrival time for timeout decisions. stamp
# is for correlation and diagnostics, never for the safety timeout itself.
