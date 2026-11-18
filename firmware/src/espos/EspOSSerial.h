#pragma once

#include <string>

// USB provisioning from the EspOS web installer (contract: docs/espos-api.md).
// The main loop hands over every serial line that starts with "ESPOS:".
namespace espos::serial {

struct ProvisionSummary {
  std::string wifiSsid;
  int settingsApplied = 0;
};

// Handles one line (without the "ESPOS:" prefix and trailing newline).
// Returns true when a PROVISION succeeded; the caller then opens the welcome screen.
bool handleLine(const char* payload);

// What the last successful PROVISION delivered, for the welcome screen.
const ProvisionSummary& lastProvision();

}  // namespace espos::serial
