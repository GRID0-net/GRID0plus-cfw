#include "update.h"
#include "config.h"
#include "net.h"
#include "version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

// Releases come straight from GitHub's public API for this repository.
// build.yml cuts one release per toolbox version, tagged v<APP_VERSION>, with
// the SD zip and grid0plus-toolbox.nro attached; the updater takes the .nro.
// Until the repository was public this went through GRID0+'s own
// /updates/latest relay, which held a GitHub token.
#define RELEASES_HOST "api.github.com"
#define RELEASES_PATH "/repos/GRID0-net/GRID0plus-cfw/releases/latest"
#define RELEASE_ASSET "grid0plus-toolbox.nro"

#define LEGACY_NRO_FILE "sdmc:/switch/grid0plus-toolbox.nro"
#define LEGACY_TMP_FILE "sdmc:/switch/grid0plus-toolbox.nro.new"

static char s_selfNro[512] = {0};
static char s_selfTmp[520] = {0};

void update_set_self_path(const char *argv0) {
    if (!argv0 || !*argv0) return;
    size_t n = strlen(argv0);
    // Leave room for the "sdmc:" prefix the '/'-prefixed branch below may add,
    // plus the terminator, so the snprintf into s_selfNro can never truncate.
    if (n < 5 || n >= sizeof(s_selfNro) - 6) return;
    if (strcasecmp(argv0 + n - 4, ".nro") != 0) return;

    if (strncmp(argv0, "sdmc:/", 6) == 0)
        snprintf(s_selfNro, sizeof(s_selfNro), "%s", argv0);
    else if (argv0[0] == '/')
        snprintf(s_selfNro, sizeof(s_selfNro), "sdmc:%s", argv0);
    else
        return;

    // The temp file lands next to the target so the final rename()/copy never
    // crosses a filesystem/volume boundary.
    snprintf(s_selfTmp, sizeof(s_selfTmp), "%s.new", s_selfNro);
}

static const char *selfNroPath(void) { return s_selfNro[0] ? s_selfNro : LEGACY_NRO_FILE; }
static const char *selfTmpPath(void) { return s_selfTmp[0] ? s_selfTmp : LEGACY_TMP_FILE; }

static char s_downloadUrl[512] = {0};
static long s_downloadSize = 0;

static Grid0plusUpdateProgressFn s_progressCb = NULL;
static long s_progressTotal = 0;

static void progressRelay(long received, long total) {
    if (total <= 0) total = s_progressTotal;
    if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_DOWNLOAD, received, total);
}

// Finds a JSON string value for `key` (e.g. "\"tag_name\""), tolerating
// optional whitespace around ':'. Returns a pointer just past the opening
// quote, or NULL.
static char *jsonStringValue(const char *haystack, const char *key) {
    char *p = strstr(haystack, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    return p + 1;
}

static bool parseReleaseJson(const char *json, int *maj, int *min, int *patch,
                              char *url, size_t urlCap, long *size) {
    char *tp = jsonStringValue(json, "\"tag_name\"");
    if (!tp) return false;
    if (*tp == 'v' || *tp == 'V') tp++;
    char *end;
    *maj = (int)strtol(tp, &end, 10);
    if (end == tp) return false;
    tp = end;
    if (*tp == '.') tp++;
    *min = (int)strtol(tp, &tp, 10);
    if (*tp == '.') tp++;
    *patch = (int)strtol(tp, NULL, 10);

    // A release carries several assets (the SD zip too), so find the .nro's
    // own entry: its size and download URL follow its name in the object.
    char *asset = strstr(json, "\"name\":\"" RELEASE_ASSET "\"");
    if (!asset) return false;
    char *sp = strstr(asset, "\"size\":");
    if (!sp) return false;
    *size = strtol(sp + 7, NULL, 10);
    char *up = jsonStringValue(asset, "\"browser_download_url\"");
    if (!up) return false;
    char *ue = strchr(up, '"');
    if (!ue || (size_t)(ue - up) >= urlCap) return false;
    memcpy(url, up, (size_t)(ue - up));
    url[ue - up] = '\0';
    return true;
}

static int semverCompare(int amaj, int amin, int apatch, int bmaj, int bmin, int bpatch) {
    if (amaj != bmaj) return amaj - bmaj;
    if (amin != bmin) return amin - bmin;
    return apatch - bpatch;
}

Grid0plusUpdate update_check(void) {
    Grid0plusUpdate u = { false, 0, 0, 0, 0, 0 };

    if (!net_ready()) { u.error = NET_ERR_NOT_READY; return u; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = NULL;
    // One retry: the first request after launch can race DNS/network bring-up.
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
    if (!parseReleaseJson(json, &maj, &min, &patch, s_downloadUrl, sizeof(s_downloadUrl), &sz)) {
        u.error = NET_ERR_PROTO;
    } else {
        u.maj = maj; u.min = min; u.patch = patch;
        if (semverCompare(maj, min, patch, GRID0PLUS_VERSION_MAJOR, GRID0PLUS_VERSION_MINOR,
                          GRID0PLUS_VERSION_PATCH) > 0) {
            if (sz > 4096) {
                u.available = true;
                u.size = sz;
                s_downloadSize = sz;
            } else {
                u.error = NET_ERR_PROTO;
            }
        }
    }
    free(json);
    return u;
}

// Overwrites dst with src's contents without removing dst first (works even
// if dst can't be unlinked but can still be opened for writing).
static bool copyOver(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_INSTALL, 0, s_progressTotal);
    FILE *out = fopen(dst, "wb");
    if (!out) { int e = errno; fclose(in); errno = e; return false; }

    char buf[16384];
    size_t n;
    long copied = 0;
    bool ok = true;
    int err = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { err = errno; ok = false; break; }
        copied += (long)n;
        if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_INSTALL, copied, s_progressTotal);
    }
    fclose(in);
    if (fclose(out) != 0) { if (ok) err = errno; ok = false; }
    if (!ok) errno = err;
    return ok;
}

Grid0plusUpdateResult update_apply(long expectedSize, Grid0plusUpdateProgressFn onProgress) {
    if (s_downloadUrl[0] == '\0') return GRID0PLUS_UPDATE_NET_FAIL;
    long expected = expectedSize > 0 ? expectedSize : s_downloadSize;

    s_progressCb = onProgress;
    s_progressTotal = expected;

    FILE *f = fopen(selfTmpPath(), "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(selfTmpPath(), "wb");
    }
    if (!f) { grid0plus_trace("update: could not create .new file"); return GRID0PLUS_UPDATE_WRITE_FAIL; }

    if (!net_ready()) { fclose(f); return GRID0PLUS_UPDATE_NET_FAIL; }

    char hostPort[256] = {0}, path[1024] = {0};
    if (sscanf(s_downloadUrl, "https://%255[^/]%1023s", hostPort, path) < 2) {
        fclose(f); remove(selfTmpPath());
        return GRID0PLUS_UPDATE_NET_FAIL;
    }
    // A download URL may name its port explicitly; a bare host is 443.
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

    if (len == -2) { grid0plus_trace("update: write to .new interrupted (SD full?)"); remove(selfTmpPath()); return GRID0PLUS_UPDATE_WRITE_FAIL; }
    if (len < 0)   { remove(selfTmpPath()); return GRID0PLUS_UPDATE_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(selfTmpPath()); return GRID0PLUS_UPDATE_NET_FAIL; }
    if (expected > 0 && len != expected) { remove(selfTmpPath()); return GRID0PLUS_UPDATE_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    // romfsInit() keeps an FS handle open on the .nro this app is running
    // from for as long as the session lives (romfs contents are read on
    // demand, the cert stub files, in our case). That's exactly the file
    // the update needs to replace, so it has to be released first or every
    // attempt below fails no matter what the SD card allows.
    romfsExit();
    grid0plus_trace("update: romfs released for replacement");

    bool placed = false;

    // 1) Overwrite in place without removing first.
    if (copyOver(selfTmpPath(), selfNroPath())) {
        placed = true;
        remove(selfTmpPath());
        grid0plus_trace("update: replaced in place");
    }

    // 2) remove + rename.
    if (!placed) {
        remove(selfNroPath());
        if (rename(selfTmpPath(), selfNroPath()) == 0) {
            placed = true;
            grid0plus_trace("update: replaced via rename");
        } else if (copyOver(selfTmpPath(), selfNroPath())) {
            placed = true;
            remove(selfTmpPath());
            grid0plus_trace("update: replaced via copy after remove");
        }
    }

    // 3) Last resort: the historical fixed path, so the user at least has
    //    the update on the card even if it needs to be moved by hand.
    if (!placed && strcmp(selfNroPath(), LEGACY_NRO_FILE) != 0) {
        mkdir("sdmc:/switch", 0777);
        if (copyOver(selfTmpPath(), LEGACY_NRO_FILE)) {
            placed = true;
            remove(selfTmpPath());
            grid0plus_trace("update: WARN target locked, wrote to switch/grid0plus-toolbox.nro instead");
        }
    }

    if (!placed) {
        remove(selfTmpPath());
        grid0plus_trace("update: ERROR could not write the update anywhere");
        romfsInit();
        return GRID0PLUS_UPDATE_WRITE_FAIL;
    }

    romfsInit();

    // Clean up an orphan left by a hypothetical earlier fixed-path install,
    // but only if we didn't just write to that exact path ourselves.
    if (strcmp(selfNroPath(), LEGACY_NRO_FILE) != 0) {
        remove(LEGACY_NRO_FILE);
        remove(LEGACY_TMP_FILE);
    }

    fsdevCommitDevice("sdmc");
    return GRID0PLUS_UPDATE_OK;
}
