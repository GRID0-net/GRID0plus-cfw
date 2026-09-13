// SwitchNet Toolbox — backup/restore of the user's /atmosphere/hosts folder.
//
// Applying SwitchNet mode overwrites sysmmc.txt/emummc.txt. Anyone who
// arrived with their own DNS-MITM redirections (a different community
// server, their own blocklist, or files with other names entirely) would
// otherwise lose them the first time the toolbox writes to that folder. This
// backs up every file currently in the hosts folder — not just the two this
// app manages — before anything is overwritten.
#ifndef SWITCHNET_BACKUP_H
#define SWITCHNET_BACKUP_H

#include <stdbool.h>

// True if a backup has already been made (i.e. SWITCHNET_BACKUP_DIR has at
// least one file in it).
bool backup_exists(void);

// Copies every regular file from /atmosphere/hosts into SWITCHNET_BACKUP_DIR,
// skipping any file that already carries SwitchNet's own header mark (so a
// backup never captures files this app wrote itself). Returns the number of
// files copied (0 = nothing worth backing up, not an error).
int backup_create(void);

// Removes SwitchNet's own managed hosts files, then copies every file from
// SWITCHNET_BACKUP_DIR back into /atmosphere/hosts, overwriting. Returns the
// number of files restored.
int backup_restore(void);

#endif // SWITCHNET_BACKUP_H
