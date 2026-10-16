// GRID0+ Toolbox, IPS patch delivery.
//
// No patch ships inside the Toolbox. The newest `patches-<serial>` release of
// the cfw repo carries manifest.json (one entry per file, with its SHA-256)
// and patches.zip (every file, named by its hash). On every launch in GRID0+
// mode the Toolbox hashes what is actually on the SD card, compares it with
// the manifest, and the update dialog installs whatever differs from one
// download of patches.zip. GitHub is asked first; GRID0+'s own server is the
// mirror when GitHub cannot be reached.
//
// Files the Toolbox installed are listed in GRID0PLUS_DIR/patches-installed.txt,
// so a patch dropped from the manifest is removed, and nothing else under
// exefs_patches/nro_patches (other homebrew's patches) is ever touched.
#ifndef GRID0PLUS_PATCHES_H
#define GRID0PLUS_PATCHES_H

#include <stdbool.h>

typedef struct {
    // 0 when the manifest was read; otherwise a NET_ERR_* code or the HTTP
    // status of the last source tried.
    int error;
    int serial;
    const char *source;    // "GitHub" or "GRID0+ server"
    // Files to install, replace or remove, by kind. `game` patches apply at
    // the game's next launch; `system` ones only after a console restart.
    int game_changes;
    int system_changes;
} Grid0plusPatchStatus;

typedef void (*Grid0plusPatchProgressFn)(long done, long total);

// Fetches the manifest and compares it with the SD card.
Grid0plusPatchStatus patches_check(void);

// Installs the changes the last patches_check() found, from the same source.
// Every file is checked against its SHA-256 before it replaces anything.
bool patches_apply(Grid0plusPatchProgressFn onProgress);

// Removes every patch the Toolbox installed (Switch to Default).
void patches_remove_all(void);

#endif // GRID0PLUS_PATCHES_H
