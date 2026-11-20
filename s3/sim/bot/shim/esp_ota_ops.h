#pragma once
// No app slots in the simulator: an update is never installed (transport_curl.cpp says so).
inline int esp_ota_mark_app_valid_cancel_rollback() { return 0; }
