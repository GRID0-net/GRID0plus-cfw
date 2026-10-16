#include "patches.h"
#include "certs.h"
#include "config.h"
#include "json.h"
#include "net.h"
#include "update.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "minizip/unzip.h"

#ifdef GRID0PLUS_HOST_TEST
#include "patches_host_shim.h"
#else
#include <switch.h>
#endif

#ifndef PATCHES_SD_ROOT
#define PATCHES_SD_ROOT "sdmc:/atmosphere"
#endif
#define PATCHES_STATE_FILE GRID0PLUS_DIR "/patches-installed.txt"
#define PATCHES_ZIP_TMP    GRID0PLUS_DIR "/patches.zip.tmp"
#define PATCHES_JSON_TMP   GRID0PLUS_DIR "/patches-manifest.tmp"

#define GITHUB_HOST      "github.com"
#define GITHUB_DOWNLOAD  "/GRID0-net/GRID0plus-cfw/releases/download/"
// The mirror serves the same two files from GRID0+'s Toolbox API port.
#define MIRROR_PATH      "/grid0plus/patches/"

#define MAX_PATCHES   256
#define MAX_DEST      160
#define MAX_PATCH_SIZE (1 << 20)

// Patch directories a released Toolbox once installed and no manifest will
// list again. Removed whenever patches are applied or GRID0+ is switched off.
static const char *const RETIRED_DIRS[] = {
    // Never fixed 2002-0001; dropped when patches moved out of the Toolbox.
    "exefs_patches/account_delete_user_fix",
};

typedef enum { KIND_GAME, KIND_SYSTEM } PatchKind;

typedef struct {
    PatchKind kind;
    char dest[MAX_DEST];   // relative to PATCHES_SD_ROOT
    char sha256[65];
    char asset[96];
    long size;
    bool needed;           // missing or different on the SD card
} PatchEntry;

typedef struct {
    PatchKind kind;
    char dest[MAX_DEST];
    bool stale;            // installed by us, no longer in the manifest
} InstalledEntry;

static PatchEntry s_patches[MAX_PATCHES];
static int s_patchCount;
static InstalledEntry s_installed[MAX_PATCHES];
static int s_installedCount;
static bool s_fromMirror;
static char s_tag[64];

// --- manifest ---------------------------------------------------------------

static bool destSafe(const char *dest) {
    if (strncmp(dest, "exefs_patches/", 14) && strncmp(dest, "nro_patches/", 12)) return false;
    if (strstr(dest, "..") || strchr(dest, '\\')) return false;
    int slashes = 0;
    for (const char *p = dest; *p; p++) if (*p == '/') slashes++;
    return slashes == 2;
}

static bool shaValid(const char *sha) {
    if (strlen(sha) != 64) return false;
    for (const char *p = sha; *p; p++) if (!isxdigit((unsigned char)*p) || isupper((unsigned char)*p)) return false;
    return true;
}

static bool arrayHasString(const char *array, const char *want) {
    if (!array || *array++ != '[') return false;
    while (*(array = jsonSpace(array)) && *array != ']') {
        char s[32];
        if (jsonString(array, s, sizeof(s)) && !strcmp(s, want)) return true;
        array = jsonEnd(array, 0);
        if (!array) return false;
        array = jsonSpace(array);
        if (*array == ',') array++;
    }
    return false;
}

// Reads the console's entries from manifest.json. Any malformed entry
// rejects the whole manifest: installing part of a patch set is worse than
// keeping the old one.
static bool parseManifest(const char *json, int *serial) {
    s_patchCount = 0;
    const char *sp = jsonField(json, "serial");
    if (!sp) return false;
    *serial = atoi(sp);
    const char *p = jsonField(json, "patches");
    if (!p || *p++ != '[') return false;
    while (*(p = jsonSpace(p)) != ']') {
        const char *end = jsonEnd(p, 0);
        if (!end) return false;
        char kind[16], dest[MAX_DEST], sha[80], asset[96];
        const char *size = jsonField(p, "size");
        if (!jsonString(jsonField(p, "kind"), kind, sizeof(kind)) ||
            !jsonString(jsonField(p, "dest"), dest, sizeof(dest)) ||
            !jsonString(jsonField(p, "sha256"), sha, sizeof(sha)) ||
            !jsonString(jsonField(p, "asset"), asset, sizeof(asset)) || !size ||
            (strcmp(kind, "game") && strcmp(kind, "system")) ||
            !destSafe(dest) || !shaValid(sha) || strchr(asset, '/'))
            return false;
        if (arrayHasString(jsonField(p, "targets"), "console")) {
            if (s_patchCount == MAX_PATCHES) return false;
            PatchEntry *e = &s_patches[s_patchCount++];
            memset(e, 0, sizeof(*e));
            e->kind = strcmp(kind, "game") ? KIND_SYSTEM : KIND_GAME;
            snprintf(e->dest, sizeof(e->dest), "%s", dest);
            memcpy(e->sha256, sha, sizeof(e->sha256));   // shaValid: exactly 64 digits
            snprintf(e->asset, sizeof(e->asset), "%s", asset);
            e->size = atol(size);
            if (e->size <= 0 || e->size > MAX_PATCH_SIZE) return false;
        }
        p = jsonSpace(end);
        if (*p == ',') p++;
        else if (*p != ']') return false;
    }
    return s_patchCount > 0;
}

// --- SD card state ----------------------------------------------------------

static void hexDigest(const void *data, size_t len, char out[65]) {
    unsigned char d[32];
    sha256CalculateHash(d, data, len);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
}

static unsigned char *readWhole(const char *path, long max, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *buf = malloc((size_t)max + 1);
    long n = buf ? (long)fread(buf, 1, (size_t)max + 1, f) : -1;
    fclose(f);
    if (n < 0 || n > max) { free(buf); return NULL; }
    *len = n;
    return buf;
}

static bool fileMatches(const PatchEntry *e) {
    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", PATCHES_SD_ROOT, e->dest);
    long len = 0;
    unsigned char *data = readWhole(path, MAX_PATCH_SIZE, &len);
    if (!data) return false;
    char hex[65];
    hexDigest(data, (size_t)len, hex);
    free(data);
    return len == e->size && !strcmp(hex, e->sha256);
}

static void loadInstalled(void) {
    s_installedCount = 0;
    FILE *f = fopen(PATCHES_STATE_FILE, "r");
    if (!f) return;
    char line[256];
    while (s_installedCount < MAX_PATCHES && fgets(line, sizeof(line), f)) {
        char kind[16], dest[MAX_DEST];
        if (sscanf(line, "%15s %159s", kind, dest) != 2 || !destSafe(dest)) continue;
        InstalledEntry *e = &s_installed[s_installedCount++];
        e->kind = strcmp(kind, "game") ? KIND_SYSTEM : KIND_GAME;
        snprintf(e->dest, sizeof(e->dest), "%s", dest);
        e->stale = false;
    }
    fclose(f);
}

static bool saveInstalled(void) {
    if (!grid0plus_ensure_dir(GRID0PLUS_DIR)) return false;
    char tmp[FS_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s.tmp", PATCHES_STATE_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < s_patchCount; i++)
        if (fprintf(f, "%s %s\n", s_patches[i].kind == KIND_GAME ? "game" : "system", s_patches[i].dest) < 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (ok) ok = installStagedFile(tmp, PATCHES_STATE_FILE);
    if (!ok) remove(tmp);
    return ok;
}

// Removes a file we installed, then its patch directory if that left it empty.
static void removeInstalled(const char *dest) {
    char path[FS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", PATCHES_SD_ROOT, dest);
    remove(path);
    char *slash = strrchr(path, '/');
    if (slash) { *slash = '\0'; rmdir(path); }
}

static void removeRetired(void) {
    for (size_t i = 0; i < sizeof(RETIRED_DIRS) / sizeof(RETIRED_DIRS[0]); i++) {
        char dir[FS_MAX_PATH];
        snprintf(dir, sizeof(dir), "%s/%s", PATCHES_SD_ROOT, RETIRED_DIRS[i]);
        grid0plus_remove_dir_and_contents(dir);
    }
}

static void compare(Grid0plusPatchStatus *st) {
    loadInstalled();
    for (int i = 0; i < s_patchCount; i++) {
        s_patches[i].needed = !fileMatches(&s_patches[i]);
        if (s_patches[i].needed) {
            if (s_patches[i].kind == KIND_GAME) st->game_changes++;
            else st->system_changes++;
        }
    }
    // A retired patch still on the card is a system change: removing it is
    // what the dialog is for, and it loads at boot like any system patch.
    for (size_t i = 0; i < sizeof(RETIRED_DIRS) / sizeof(RETIRED_DIRS[0]); i++) {
        char dir[FS_MAX_PATH];
        struct stat sb;
        snprintf(dir, sizeof(dir), "%s/%s", PATCHES_SD_ROOT, RETIRED_DIRS[i]);
        if (stat(dir, &sb) == 0) st->system_changes++;
    }
    for (int i = 0; i < s_installedCount; i++) {
        bool listed = false;
        for (int j = 0; j < s_patchCount && !listed; j++) listed = !strcmp(s_installed[i].dest, s_patches[j].dest);
        s_installed[i].stale = !listed;
        if (!listed) {
            if (s_installed[i].kind == KIND_GAME) st->game_changes++;
            else st->system_changes++;
        }
    }
}

// --- network ----------------------------------------------------------------

// Downloads to a file (following GitHub's one redirect) and reads it back.
static unsigned char *download(const char *host, int port, const char *path, const char *tmp,
                               long max, long *len, int *status, Grid0plusPatchProgressFn onProgress) {
    if (!grid0plus_ensure_dir(GRID0PLUS_DIR)) { *status = NET_ERR_UNKNOWN; return NULL; }
    FILE *f = fopen(tmp, "wb");
    if (!f) { *status = NET_ERR_UNKNOWN; return NULL; }
    long n = net_https_get_to_file(host, port, path, f, status, onProgress);
    fclose(f);
    unsigned char *data = NULL;
    if (n >= 0 && *status == 200) data = readWhole(tmp, max, len);
    else if (*status == 200) *status = NET_ERR_UNKNOWN;
    remove(tmp);
    return data;
}

// The newest published patches release, from the release list update_check()
// already read: Toolbox releases sit in the same list. Without that list
// (GitHub unreachable a moment ago) GitHub is not tried again, so a network
// that drops packets costs one timeout before the mirror, not two.
static bool findGithubTag(int *status) {
    const char *json = update_release_list(status);
    if (!json) { if (*status == 0) *status = NET_ERR_UNKNOWN; return false; }
    const char *r = jsonSpace(json);
    if (*r == '[') {
        r++;
        while (*(r = jsonSpace(r)) && *r != ']') {
            const char *end = jsonEnd(r, 0);
            if (!end) break;
            const char *draft = jsonField(r, "draft");
            char tag[64];
            if (draft && !strncmp(draft, "false", 5) &&
                jsonString(jsonField(r, "tag_name"), tag, sizeof(tag)) && !strncmp(tag, "patches-", 8)) {
                snprintf(s_tag, sizeof(s_tag), "%s", tag);
                return true;
            }
            r = jsonSpace(end);
            if (*r == ',') r++;
        }
    }
    *status = NET_ERR_PROTO;
    return false;
}

static unsigned char *fetchFile(bool mirror, const char *name, const char *tmp, long max, long *len,
                                int *status, Grid0plusPatchProgressFn onProgress) {
    char path[256];
    if (mirror) {
        snprintf(path, sizeof(path), MIRROR_PATH "%s", name);
        return download(g_server_ip, GRID0PLUS_TOOLBOX_PORT, path, tmp, max, len, status, onProgress);
    }
    snprintf(path, sizeof(path), GITHUB_DOWNLOAD "%s/%s", s_tag, name);
    return download(GITHUB_HOST, 443, path, tmp, max, len, status, onProgress);
}

Grid0plusPatchStatus patches_check(void) {
    Grid0plusPatchStatus st = {0};
    s_patchCount = 0;
    if (!net_ready()) { st.error = NET_ERR_NOT_READY; return st; }

    for (int source = 0; source < 2; source++) {
        bool mirror = source == 1;
        int status = 0;
        if (!mirror && !findGithubTag(&status)) { st.error = status; continue; }
        long len = 0;
        unsigned char *body = fetchFile(mirror, "manifest.json", PATCHES_JSON_TMP, 1 << 20, &len, &status, NULL);
        if (!body) { st.error = status ? status : NET_ERR_UNKNOWN; continue; }
        body[len] = '\0';
        int serial = 0;
        bool ok = parseManifest((const char *)body, &serial);
        free(body);
        if (!ok) { st.error = NET_ERR_PROTO; continue; }
        s_fromMirror = mirror;
        st.error = 0;
        st.serial = serial;
        st.source = mirror ? "GRID0+ server" : "GitHub";
        compare(&st);
        // Files already on the card from a Toolbox that bundled them match
        // the manifest; record them so Switch to Default removes them too.
        if (st.game_changes + st.system_changes == 0) saveInstalled();
        grid0plus_trace(mirror ? "patches: manifest from the GRID0+ server" : "patches: manifest from GitHub");
        return st;
    }
    grid0plus_trace("patches: no manifest from any source");
    return st;
}

static bool installOne(unzFile uf, const PatchEntry *e) {
    if (unzLocateFile(uf, e->asset, 1) != UNZ_OK || unzOpenCurrentFile(uf) != UNZ_OK) return false;
    unsigned char *buf = malloc((size_t)e->size + 1);
    int n = buf ? unzReadCurrentFile(uf, buf, (unsigned)e->size + 1) : -1;
    bool crcOk = unzCloseCurrentFile(uf) == UNZ_OK;
    char hex[65] = {0};
    if (n >= 0) hexDigest(buf, (size_t)n, hex);
    bool ok = crcOk && n == e->size && !strcmp(hex, e->sha256);

    char path[FS_MAX_PATH], tmp[FS_MAX_PATH + 32];
    snprintf(path, sizeof(path), "%s/%s", PATCHES_SD_ROOT, e->dest);
    snprintf(tmp, sizeof(tmp), "%s.grid0-update.tmp", path);
    if (ok) ok = mkdirParents(path);
    FILE *f = ok ? fopen(tmp, "wb") : NULL;
    if (ok && !f) ok = false;
    if (f) {
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) ok = false;
        if (fclose(f) != 0) ok = false;
    }
    free(buf);
    if (ok) ok = installStagedFile(tmp, path);
    remove(tmp);
    return ok;
}

bool patches_apply(Grid0plusPatchProgressFn onProgress) {
    if (s_patchCount == 0) return false;
    bool anyNeeded = false;
    for (int i = 0; i < s_patchCount; i++) anyNeeded |= s_patches[i].needed;

    bool ok = true;
    if (anyNeeded) {
        int status = 0;
        if (!grid0plus_ensure_dir(GRID0PLUS_DIR)) return false;
        FILE *f = fopen(PATCHES_ZIP_TMP, "wb");
        if (!f) return false;
        char path[256];
        long n;
        if (s_fromMirror) {
            snprintf(path, sizeof(path), MIRROR_PATH "patches.zip");
            n = net_https_get_to_file(g_server_ip, GRID0PLUS_TOOLBOX_PORT, path, f, &status, onProgress);
        } else {
            snprintf(path, sizeof(path), GITHUB_DOWNLOAD "%s/patches.zip", s_tag);
            n = net_https_get_to_file(GITHUB_HOST, 443, path, f, &status, onProgress);
        }
        fclose(f);
        if (n < 0 || status != 200) { remove(PATCHES_ZIP_TMP); grid0plus_trace("patches: download failed"); return false; }

        unzFile uf = unzOpen(PATCHES_ZIP_TMP);
        if (!uf) { remove(PATCHES_ZIP_TMP); return false; }
        for (int i = 0; ok && i < s_patchCount; i++)
            if (s_patches[i].needed && !installOne(uf, &s_patches[i])) ok = false;
        unzClose(uf);
        remove(PATCHES_ZIP_TMP);
    }
    if (ok) {
        for (int i = 0; i < s_installedCount; i++) if (s_installed[i].stale) removeInstalled(s_installed[i].dest);
        removeRetired();
        ok = saveInstalled();
    }
    fsdevCommitDevice("sdmc");
    grid0plus_trace(ok ? "patches: installed" : "patches: install FAILED");
    if (ok) for (int i = 0; i < s_patchCount; i++) s_patches[i].needed = false;
    return ok;
}

void patches_remove_all(void) {
    loadInstalled();
    for (int i = 0; i < s_installedCount; i++) removeInstalled(s_installed[i].dest);
    removeRetired();
    remove(PATCHES_STATE_FILE);
    fsdevCommitDevice("sdmc");
    grid0plus_trace("patches: removed");
}
