// GRID0+ Toolbox, backup/restore of the user's /atmosphere/hosts folder.
//
// Applying GRID0+ mode overwrites sysmmc.txt/emummc.txt. Anyone who
// arrived with their own DNS-MITM redirections (a different community
// server, their own blocklist, or files with other names entirely) would
// otherwise lose them the first time the toolbox writes to that folder. This
// backs up every file currently in the hosts folder, not just the two this
// app manages, before anything is overwritten.
#ifndef GRID0PLUS_BACKUP_H
#define GRID0PLUS_BACKUP_H

#include <stdbool.h>

// True if a backup has already been made (i.e. GRID0PLUS_BACKUP_DIR has at
// least one file in it).
bool backup_exists(void);

// Copies every regular file from /atmosphere/hosts into GRID0PLUS_BACKUP_DIR,
// skipping any file that already carries GRID0+'s own header mark (so a
// backup never captures files this app wrote itself). Returns the number of
// files copied (0 = nothing worth backing up, not an error).
int backup_create(void);

// Removes GRID0+'s own managed hosts files, then copies every file from
// GRID0PLUS_BACKUP_DIR back into /atmosphere/hosts, overwriting. Returns the
// number of files restored.
int backup_restore(void);

#endif // GRID0PLUS_BACKUP_H
