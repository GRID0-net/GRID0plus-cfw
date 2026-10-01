// GRID0+ Toolbox, apply/restore the two network modes.
#ifndef GRID0PLUS_APPLY_H
#define GRID0PLUS_APPLY_H

#include <stdbool.h>
#include <switch.h>

typedef enum { GRID0PLUS_MODE_GRID0PLUS = 0, GRID0PLUS_MODE_DEFAULT = 1 } Grid0plusMode;

// Mode currently written to the hosts files (cosmetic detection, for the UI).
Grid0plusMode apply_current_mode(void);

// Switches to GRID0+ mode: makes a one-time safety backup of the user's
// existing /atmosphere/hosts (if none has been made yet), provisions the
// certificate stub files, writes the GRID0+ hosts redirections for `ip`,
// and enables DNS-MITM. Does not reboot; call grid0plus_reboot() after.
bool apply_grid0plus(const char *ip);

// Switches back to Default mode: removes GRID0+'s own hosts files,
// restores the backed-up hosts (if config_get_restore_on_default() and a
// backup exists), removes the certificate files, and leaves DNS-MITM active
// with Atmosphère's own default telemetry blocking. Does not reboot.
bool apply_default(void);

// Reboots the console (bpcRebootSystem). Only returns on failure.
Result grid0plus_reboot(void);

#endif // GRID0PLUS_APPLY_H
