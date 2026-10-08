#include "update.h"
#include "config.h"
#include "net.h"
#include "version.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "minizip/unzip.h"

#define RELEASES_HOST "api.github.com"
#define RELEASES_PATH "/repos/GRID0-net/GRID0plus-cfw/releases?per_page=5"
#define SYS_RELEASE_PATH "sdmc:/GRID0plus/sys-grid0-installed-release.txt"
#define UPDATE_ZIP_TMP "sdmc:/switch/grid0plus-update.zip"

void update_set_self_path(const char *argv0) {
    (void)argv0;
}

static char s_downloadUrl[512] = {0};
static long s_downloadSize = 0;
static char s_releaseTag[64] = {0};
static int s_availableTargets = 0;

static Grid0plusUpdateProgressFn s_progressCb = NULL;
static long s_progressTotal = 0;

static void progressRelay(long received, long total) {
    if (total <= 0) total = s_progressTotal;
    if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_DOWNLOAD, received, total);
}

// Walk complete JSON values so an asset cannot borrow fields from its sibling.
static const char *jsonSpace(const char *p) { while (isspace((unsigned char)*p)) p++; return p; }
static const char *jsonEnd(const char *p, unsigned depth) {
    p = jsonSpace(p);
    if (depth > 32 || !*p) return NULL;
    if (*p == '"') {
        for (p++; *p; p++) {
            if (*p == '\\') { if (!*++p) return NULL; }
            else if (*p == '"') return p + 1;
        }
        return NULL;
    }
    if (*p == '{' || *p == '[') {
        char close = *p++ == '{' ? '}' : ']';
        while (*(p = jsonSpace(p)) && *p != close) {
            if (*p == ',' || *p == ':') { p++; continue; }
            p = jsonEnd(p, depth + 1);
            if (!p) return NULL;
        }
        return *p == close ? p + 1 : NULL;
    }
    const char *begin = p;
    while (*p && !isspace((unsigned char)*p) && !strchr(",:}]", *p)) p++;
    return p != begin ? p : NULL;
}
static const char *jsonField(const char *object, const char *key) {
    const char *p = jsonSpace(object);
    if (*p++ != '{') return NULL;
    while (*(p = jsonSpace(p)) != '}') {
        const char *end = jsonEnd(p, 0);
        if (!end || *p != '"') return NULL;
        bool match = (size_t)(end - p) == strlen(key) + 2 && !memcmp(p + 1, key, strlen(key));
        p = jsonSpace(end);
        if (*p++ != ':') return NULL;
        p = jsonSpace(p);
        if (match) return p;
        p = jsonEnd(p, 0);
        if (!p) return NULL;
        p = jsonSpace(p);
        if (*p == ',') p++;
        else if (*p != '}') return NULL;
    }
    return NULL;
}
static bool jsonString(const char *p, char *out, size_t cap) {
    if (!p || *p != '"') return false;
    const char *end = jsonEnd(p, 0);
    if (!end || (size_t)(end - p - 2) >= cap) return false;
    // Tags, asset names and GitHub URLs do not need JSON escape decoding.
    size_t n = (size_t)(end - p - 2);
    if (memchr(p + 1, '\\', n)) return false;
    memcpy(out, p + 1, n); out[n] = '\0'; return true;
}
static bool parseReleaseJson(const char *json, int *maj, int *min, int *patch,
                              char *url, size_t urlCap, long *size) {
    const char *draft = jsonField(json, "draft"), *pre = jsonField(json, "prerelease");
    if (!draft || !pre || strncmp(draft, "false", 5) || strncmp(pre, "false", 5)) return false;
    char tag[64]; int consumed = 0;
    if (!jsonString(jsonField(json, "tag_name"), tag, sizeof(tag)) ||
        sscanf(tag, "v%d.%d.%d%n", maj, min, patch, &consumed) != 3 ||
        *maj < 0 || *min < 0 || *patch < 0) return false;
    if (tag[consumed] && strncmp(tag + consumed, "-sys.", 5)) return false;
    char assetName[100]; snprintf(assetName, sizeof(assetName), "GRID0-cfw-%s.zip", tag);
    const char *asset = jsonField(json, "assets");
    if (!asset || *asset++ != '[') return false;
    while (*(asset = jsonSpace(asset)) != ']') {
        const char *end = jsonEnd(asset, 0);
        if (!end) return false;
        char name[128];
        if (jsonString(jsonField(asset, "name"), name, sizeof(name)) && !strcmp(name, assetName)) {
            const char *sp = jsonField(asset, "size"); char *se;
            if (!sp) return false;
            errno = 0; *size = strtol(sp, &se, 10);
            if (errno || se == sp || *size <= 4096 ||
                !jsonString(jsonField(asset, "browser_download_url"), url, urlCap) ||
                strncmp(url, "https://github.com/GRID0-net/GRID0plus-cfw/releases/download/", strlen("https://github.com/GRID0-net/GRID0plus-cfw/releases/download/"))) return false;
            snprintf(s_releaseTag, sizeof(s_releaseTag), "%s", tag);
            return true;
        }
        asset = jsonSpace(end);
        if (*asset == ',') asset++;
        else if (*asset != ']') return false;
    }
    return false;
}

static int semverCompare(int amaj, int amin, int apatch, int bmaj, int bmin, int bpatch) {
    if (amaj != bmaj) return amaj - bmaj;
    if (amin != bmin) return amin - bmin;
    return apatch - bpatch;
}

Grid0plusUpdate update_check(void) {
    Grid0plusUpdate u = {0};
    s_downloadUrl[0] = s_releaseTag[0] = '\0';
    s_downloadSize = 0;
    s_availableTargets = 0;

    if (!net_ready()) { u.error = NET_ERR_NOT_READY; return u; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = NULL;
    for (int attempt = 0; attempt < 2 && !body; attempt++) {
        body = net_https_get(RELEASES_HOST, 443, RELEASES_PATH, &len, &status);
        if (!body && attempt == 0) svcSleepThread(1000000000ULL);
    }
    if (!body || status != 200) {
        u.error = status != 0 ? status : NET_ERR_UNKNOWN;
        free(body);
        return u;
    }

    char *json = (char *)malloc(len + 1);
    if (!json) { free(body); u.error = NET_ERR_OOM; return u; }
    memcpy(json, body, len);
    json[len] = '\0';
    free(body);

    int maj = 0, min = 0, patch = 0;
    long sz = 0;
    bool found = false;
    const char *release = jsonSpace(json);
    if (*release == '[') {
        release++;
        while (*(release = jsonSpace(release)) != ']') {
            const char *end = jsonEnd(release, 0);
            if (!end) break;
            if (parseReleaseJson(release, &maj, &min, &patch, s_downloadUrl, sizeof(s_downloadUrl), &sz)) { found = true; break; }
            release = jsonSpace(end);
            if (*release == ',') release++;
            else if (*release != ']') break;
        }
    }
    if (!found) {
        u.error = NET_ERR_PROTO;
    } else {
        u.maj = maj; u.min = min; u.patch = patch; u.size = sz;
        snprintf(u.tag, sizeof(u.tag), "%s", s_releaseTag);
        u.toolbox_available = semverCompare(maj, min, patch, GRID0PLUS_VERSION_MAJOR,
                                GRID0PLUS_VERSION_MINOR, GRID0PLUS_VERSION_PATCH) > 0;
        char installed[64] = {0};
        FILE *marker = fopen(SYS_RELEASE_PATH, "r");
        if (marker) { (void)fgets(installed, sizeof(installed), marker); fclose(marker); }
        installed[strcspn(installed, "\r\n")] = '\0';
        u.sysmodule_available = strcmp(installed, s_releaseTag) != 0;
        u.available = u.toolbox_available || u.sysmodule_available;
        s_availableTargets = (u.toolbox_available ? GRID0PLUS_UPDATE_TOOLBOX : 0) |
                             (u.sysmodule_available ? GRID0PLUS_UPDATE_SYSMODULE : 0);
        s_downloadSize = sz;
    }
    free(json);
    return u;
}

static bool zipPathSafe(const char *name) {
    if (!name || !*name) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (strstr(name, "..")) return false;
    return true;
}

static bool mkdirParents(const char *path) {
    char tmp[1024];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return false;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    return true;
}

// Horizon's rename does not provide POSIX overwrite semantics. Move the old
// binary aside first and restore it if placing the staged replacement fails.
static bool installStagedFile(const char *temporary, const char *destination) {
    char backup[1056]; size_t n = strlen(destination);
    if (n + sizeof(".grid0-update.bak") > sizeof(backup)) return false;
    memcpy(backup, destination, n);
    memcpy(backup + n, ".grid0-update.bak", sizeof(".grid0-update.bak"));
    struct stat st;
    bool exists = stat(destination, &st) == 0;
    if (!exists && errno != ENOENT) return false;
    if (exists) {
        if (remove(backup) != 0 && errno != ENOENT) return false;
        if (rename(destination, backup) != 0) return false;
    }
    if (rename(temporary, destination) == 0) return true;
    if (exists) (void)rename(backup, destination);
    return false;
}

static const char *updatePaths[] = {
    "switch/grid0plus-toolbox.nro",
    "atmosphere/contents/4200000000005A54/exefs.nsp",
    "switch/.overlays/sys-GRID0+.ovl",
};
static bool extractZip(const char *zipPath, const char *destRoot, Grid0plusUpdateTarget target) {
    if (target < GRID0PLUS_UPDATE_TOOLBOX || target > GRID0PLUS_UPDATE_BOTH) return false;
    bool wanted[3] = { target & GRID0PLUS_UPDATE_TOOLBOX, target & GRID0PLUS_UPDATE_SYSMODULE, target & GRID0PLUS_UPDATE_SYSMODULE };
    bool staged[3] = {0}; char paths[3][1024], temps[3][1056];
    for (int i = 0; i < 3; i++) {
        if (snprintf(paths[i], sizeof(paths[i]), "%s/%s", destRoot, updatePaths[i]) >= (int)sizeof(paths[i])) return false;
        size_t n = strlen(paths[i]);
        memcpy(temps[i], paths[i], n);
        memcpy(temps[i] + n, ".grid0-update.tmp", sizeof(".grid0-update.tmp"));
    }
    unzFile uf = unzOpen(zipPath);
    if (!uf) return false;
    unz_global_info gi;
    bool ok = unzGetGlobalInfo(uf, &gi) == UNZ_OK;
    char buf[16384]; long done = 0;
    for (uLong i = 0; ok && i < gi.number_entry; i++) {
        unz_file_info fi; char name[512] = {0};
        if (unzGetCurrentFileInfo(uf, &fi, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK ||
            fi.size_filename >= sizeof(name) || strlen(name) != fi.size_filename || !zipPathSafe(name)) { ok = false; break; }
        int slot = -1;
        for (int n = 0; n < 3; n++) if (wanted[n] && !strcmp(name, updatePaths[n])) slot = n;
        // Never copy boot2.flag, settings, identities, or unrelated payloads.
        if (slot >= 0) {
            if (staged[slot] || !fi.uncompressed_size || !mkdirParents(paths[slot]) || unzOpenCurrentFile(uf) != UNZ_OK) { ok = false; break; }
            FILE *f = fopen(temps[slot], "wb"); int count = 0; unsigned long written = 0;
            if (!f) ok = false;
            while (ok && (count = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, (size_t)count, f) != (size_t)count) { ok = false; break; }
                written += (unsigned long)count; done += count;
                if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_INSTALL, done, s_progressTotal);
            }
            if (count < 0 || written != fi.uncompressed_size) ok = false;
            if (f && fclose(f) != 0) ok = false;
            if (unzCloseCurrentFile(uf) != UNZ_OK) ok = false;
            staged[slot] = true;
        }
        if (ok && i + 1 < gi.number_entry && unzGoToNextFile(uf) != UNZ_OK) ok = false;
    }
    unzClose(uf);
    for (int i = 0; i < 3; i++) if (wanted[i] && !staged[i]) ok = false;
    // Check all selected entries and their CRCs before replacing any binary.
    for (int i = 0; ok && i < 3; i++) if (wanted[i] && !installStagedFile(temps[i], paths[i])) ok = false;
    for (int i = 0; i < 3; i++) if (wanted[i]) remove(temps[i]);
    return ok;
}

Grid0plusUpdateResult update_apply(long expectedSize, Grid0plusUpdateTarget target, Grid0plusUpdateProgressFn onProgress) {
    if (target < GRID0PLUS_UPDATE_TOOLBOX || target > GRID0PLUS_UPDATE_BOTH || s_downloadUrl[0] == '\0') return GRID0PLUS_UPDATE_NET_FAIL;
    // Selecting both updates only components that need an update; never
    // replace a newer Toolbox with an older bundle during a release rollout.
    target = (Grid0plusUpdateTarget)(target & s_availableTargets);
    if (!target) return GRID0PLUS_UPDATE_NET_FAIL;
    long expected = expectedSize > 0 ? expectedSize : s_downloadSize;

    s_progressCb = onProgress;
    s_progressTotal = expected;

    mkdir("sdmc:/switch", 0777);
    FILE *f = fopen(UPDATE_ZIP_TMP, "wb");
    if (!f) return GRID0PLUS_UPDATE_WRITE_FAIL;

    if (!net_ready()) { fclose(f); remove(UPDATE_ZIP_TMP); return GRID0PLUS_UPDATE_NET_FAIL; }

    char hostPort[256] = {0}, path[1024] = {0};
    if (sscanf(s_downloadUrl, "https://%255[^/]%1023s", hostPort, path) < 2) {
        fclose(f); remove(UPDATE_ZIP_TMP);
        return GRID0PLUS_UPDATE_NET_FAIL;
    }
    char host[256] = {0};
    int port = 443;
    char *colon = strrchr(hostPort, ':');
    if (colon) {
        size_t hostLen = (size_t)(colon - hostPort);
        if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
        memcpy(host, hostPort, hostLen);
        host[hostLen] = '\0';
        port = atoi(colon + 1);
        if (port <= 0) port = 443;
    } else {
        snprintf(host, sizeof(host), "%s", hostPort);
    }

    int status = 0;
    long len = net_https_get_to_file(host, port, path, f, &status, onProgress ? progressRelay : NULL);
    fclose(f);

    if (len == -2) { remove(UPDATE_ZIP_TMP); return GRID0PLUS_UPDATE_WRITE_FAIL; }
    if (len < 0)   { remove(UPDATE_ZIP_TMP); return GRID0PLUS_UPDATE_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(UPDATE_ZIP_TMP); return GRID0PLUS_UPDATE_NET_FAIL; }
    if (expected > 0 && len != expected) { remove(UPDATE_ZIP_TMP); return GRID0PLUS_UPDATE_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    if (target & GRID0PLUS_UPDATE_TOOLBOX) romfsExit();

    bool placed = extractZip(UPDATE_ZIP_TMP, "sdmc:/", target);

    if (target & GRID0PLUS_UPDATE_TOOLBOX) romfsInit();
    remove(UPDATE_ZIP_TMP);
    fsdevCommitDevice("sdmc");

    if (!placed) return GRID0PLUS_UPDATE_WRITE_FAIL;
    if (target & GRID0PLUS_UPDATE_SYSMODULE) {
        if (!mkdirParents(SYS_RELEASE_PATH)) return GRID0PLUS_UPDATE_WRITE_FAIL;
        FILE *marker = fopen(SYS_RELEASE_PATH, "w");
        if (!marker) return GRID0PLUS_UPDATE_WRITE_FAIL;
        bool saved = fprintf(marker, "%s\n", s_releaseTag) > 0;
        if (fclose(marker) != 0) saved = false;
        fsdevCommitDevice("sdmc");
        if (!saved) return GRID0PLUS_UPDATE_WRITE_FAIL;
    }
    return GRID0PLUS_UPDATE_OK;
}
