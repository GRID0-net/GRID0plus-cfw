// GRID0+ Toolbox — apply/restore the two network modes.
#ifndef SWITCHNET_APPLY_H
#define SWITCHNET_APPLY_H

#include <stdbool.h>
#include <switch.h>

typedef enum { SWITCHNET_MODE_SWITCHNET = 0, SWITCHNET_MODE_DEFAULT = 1 } SwitchnetMode;

// Mode currently written to the hosts files (cosmetic detection, for the UI).
SwitchnetMode apply_current_mode(void);

// Switches to GRID0+ mode: makes a one-time safety backup of the user's
// existing /atmosphere/hosts (if none has been made yet), provisions the
// certificate stub files, writes the GRID0+ hosts redirections for `ip`,
// and enables DNS-MITM. Does not reboot; call switchnet_reboot() after.
bool apply_switchnet(const char *ip);

// Switches back to Default mode: removes GRID0+'s own hosts files,
// restores the backed-up hosts (if config_get_restore_on_default() and a
// backup exists), removes the certificate files, and leaves DNS-MITM active
// with Atmosphère's own default telemetry blocking. Does not reboot.
bool apply_default(void);

// Reboots the console (bpcRebootSystem). Only returns on failure.
Result switchnet_reboot(void);

#endif // SWITCHNET_APPLY_H
