// GRID0+ Toolbox, the small JSON reader and file helpers shared by the
// self-updater (update.c) and patch delivery (patches.c). Defined in update.c.
#ifndef GRID0PLUS_JSON_H
#define GRID0PLUS_JSON_H

#include <stdbool.h>
#include <stddef.h>

const char *jsonSpace(const char *p);
// End of the complete JSON value starting at p, or NULL if it is malformed.
const char *jsonEnd(const char *p, unsigned depth);
// The value of `key` in the object starting at `object`, or NULL.
const char *jsonField(const char *object, const char *key);
// Copies an unescaped JSON string value; false if absent, escaped or too long.
bool jsonString(const char *p, char *out, size_t cap);

// Creates every parent directory of `path`.
bool mkdirParents(const char *path);
// Moves `temporary` over `destination`, restoring the old file on failure.
bool installStagedFile(const char *temporary, const char *destination);

#endif // GRID0PLUS_JSON_H
