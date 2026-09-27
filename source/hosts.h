// GRID0+ Toolbox — Atmosphère DNS-MITM hosts file content.
//
// Atmosphère's hosts files use a slightly extended format: '*' matches 0+
// characters anywhere in a hostname, '%' stands in for the environment id
// (always "lp1" on production consoles), and when several lines match the
// same domain the LAST matching line wins. See
// Atmosphere-NX/Atmosphere docs/features/dns_mitm.md for the authoritative
// reference.
//
// This is a generic starting template that redirects the common Nintendo
// online endpoints to a GRID0+ server IP and null-routes telemetry. It
// does not contain per-game NEX server IDs or any bundled game patches —
// add hostnames here as GRID0+'s own infrastructure grows.
#ifndef SWITCHNET_HOSTS_H
#define SWITCHNET_HOSTS_H

#include <stdbool.h>
#include <stddef.h>

// Recognizable in any hosts file GRID0+ itself generated, regardless of
// which IP it pointed at — used to tell "ours" apart from a file the user
// (or another community server project) put there themselves.
#define SWITCHNET_HOSTS_HEADER_MARK "SWITCHNET NETWORK - Atmosphere DNS-MITM"

#define SWITCHNET_HOSTS_DIR    "sdmc:/atmosphere/hosts"
#define SWITCHNET_HOSTS_SYSMMC SWITCHNET_HOSTS_DIR "/sysmmc.txt"
#define SWITCHNET_HOSTS_EMUMMC SWITCHNET_HOSTS_DIR "/emummc.txt"
#define SWITCHNET_SETTINGS_INI "sdmc:/atmosphere/config/system_settings.ini"

// exosphere.ini lives at the SD card root, not under atmosphere/config like
// system_settings.ini above -- a different file, read by a different stage
// of boot (Exosphere itself, not Atmosphere's own config loader).
#define SWITCHNET_EXOSPHERE_INI "sdmc:/exosphere.ini"

// Builds the hosts file content redirecting to `ip`. Caller must free() the
// returned buffer.
char *hosts_build(const char *ip);

// Writes SWITCHNET_HOSTS_SYSMMC and SWITCHNET_HOSTS_EMUMMC with hosts_build(ip).
bool hosts_write_all(const char *ip);

// Removes SWITCHNET_HOSTS_SYSMMC / SWITCHNET_HOSTS_EMUMMC, but only if they
// carry SWITCHNET_HOSTS_HEADER_MARK — never a same-named file SwitchNet did
// not write itself.
void hosts_clear_own(void);

// True if either managed hosts file currently redirects to a GRID0+ IP
// (used to show the current mode in the UI).
bool hosts_is_switchnet_active(void);

// Edits system_settings.ini, setting enable_dns_mitm / add_defaults_to_dns_hosts
// under [atmosphere] while preserving every other key/section.
bool hosts_set_dns_mitm(bool enable, bool addDefaults);

// Edits exosphere.ini's [exosphere] section, setting blank_prodinfo_emummc
// while preserving every other key/section. `blank` true writes 1 (the
// account sysmodule sees a blanked PRODINFO on this emuMMC), false writes 0
// (the real one). Only ever touches the emuMMC key -- sysmmc's own PRODINFO
// handling is not this project's concern, and is never written here.
//
// GRID0+ mode wants this at 0: the emuMMC's account/BAAS auth flow reads
// PRODINFO-derived device identity, and a blanked one is exactly the shape
// Prelude-Nro's own source documents causing account-link failures (their
// comment names 2123-0011) when its own "server" mode needs a real one.
// Default mode wants it back at 1, the same anti-ban reasoning Prelude
// applies when returning to real Nintendo: this emuMMC's real device
// identity should never be presented on a boot that might reach Nintendo's
// actual servers, even by mistake.
bool hosts_set_blank_prodinfo_emummc(bool blank);

#endif // SWITCHNET_HOSTS_H
