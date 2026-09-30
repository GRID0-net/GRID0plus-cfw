// GRID0+ Toolbox — app configuration and persisted settings.
//
// Everything the app reads or writes on the SD card outside of Atmosphere's own
// folders lives under SWITCHNET_DIR, so uninstalling is "delete two folders":
// this one, and whatever apply_switchnet() wrote under /atmosphere.
#ifndef SWITCHNET_CONFIG_H
#define SWITCHNET_CONFIG_H

#include <stdbool.h>

// Default GRID0+ server IP. Overridden at runtime via the in-app IP field
// (Set custom SwitchNet IP), persisted in SWITCHNET_CONFIG_FILE.
#define SWITCHNET_SERVER_IP_DEFAULT "89.168.58.206"
// The Azure address the server had before moving to GRID0 (Oracle, 2026-09-27).
// A saved config still naming it is moved to the new default on load.
#define SWITCHNET_SERVER_IP_PREVIOUS "9.205.104.23"
// The default server's second public address, for Pia's NAT check: nncs2
// must answer from a different IP than nncs1, or the console de-duplicates
// the probe and NAT detection never completes.
#define SWITCHNET_NATCHECK_SECONDARY_IP_DEFAULT "145.241.167.178"
#define SWITCHNET_SERVER_IP_MAX 64

// Currently selected server IP (default, or the override once loaded).
extern char g_server_ip[SWITCHNET_SERVER_IP_MAX];

// GRID0+'s own toolbox API (server status, update checks) is reached by
// IP on this fixed port — never by a redirected Nintendo hostname, and
// never DNS-resolved: it is the same g_server_ip the hosts file points
// games at, just a different port on the nginx edge (see
// deploy/nginx.toolbox.conf in the switchnet repo, the same pattern
// gamesync uses for port 7575 instead of a slot in the SNI map).
#define SWITCHNET_TOOLBOX_PORT 8443

#define SWITCHNET_DIR           "sdmc:/switchnet"
#define SWITCHNET_CONFIG_FILE   SWITCHNET_DIR "/config.cfg"
#define SWITCHNET_TRACE_PATH    SWITCHNET_DIR "/trace.txt"
#define SWITCHNET_BACKUP_DIR    SWITCHNET_DIR "/hosts_backup"

// Loads g_server_ip and the flags below from SWITCHNET_CONFIG_FILE (falls back
// to defaults if the file doesn't exist yet). Call once at startup.
void config_load(void);

// Persists g_server_ip and the flags below to SWITCHNET_CONFIG_FILE.
bool config_save(void);

// Sets g_server_ip (validated as a dotted IPv4 address by the caller) and saves.
bool config_set_server_ip(const char *ip);

// True once a hosts backup has been attempted (successfully or not) — the
// automatic pre-first-apply safety backup only fires while this is false.
bool config_get_backup_done(void);
void config_set_backup_done(bool done);

// True if "Switch to Default" should restore the backed-up hosts files
// (when one exists). Defaults to true; exposed as a menu toggle.
bool config_get_restore_on_default(void);
void config_set_restore_on_default(bool on);

// Writes a line to SWITCHNET_TRACE_PATH and commits the SD card immediately,
// so the last line on disk is always the last step actually reached — useful
// if the console is powered off before the app can exit cleanly.
void switchnet_trace(const char *step);

// mkdir -p for an sdmc: path (mkdir() does not create intermediate dirs).
bool switchnet_ensure_dir(const char *path);

#endif // SWITCHNET_CONFIG_H
