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

#include "minizip/unzip.h"

#define RELEASES_HOST "api.github.com"
#define RELEASES_PATH "/repos/GRID0-net/GRID0plus-cfw/releases/latest"
#define RELEASE_ZIP_FMT "GRID0-cfw-v%d.%d.%d.zip"
#define UPDATE_ZIP_TMP "sdmc:/switch/grid0plus-update.zip"

void update_set_self_path(const char *argv0) {
    (void)argv0;
}

static char s_downloadUrl[512] = {0};
static long s_downloadSize = 0;

static Grid0plusUpdateProgressFn s_progressCb = NULL;
static long s_progressTotal = 0;

static void progressRelay(long received, long total) {
    if (total <= 0) total = s_progressTotal;
    if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_DOWNLOAD, received, total);
}

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

    char assetName[64];
    snprintf(assetName, sizeof(assetName), RELEASE_ZIP_FMT, *maj, *min, *patch);
    char key[80];
    snprintf(key, sizeof(key), "\"name\":\"%s\"", assetName);
    char *asset = strstr(json, key);
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

static bool extractZip(const char *zipPath, const char *destRoot) {
    unzFile uf = unzOpen(zipPath);
    if (!uf) return false;

    unz_global_info gi;
    bool ok = unzGetGlobalInfo(uf, &gi) == UNZ_OK;
    char buf[16384];
    long done = 0;

    for (uLong i = 0; ok && i < gi.number_entry; i++) {
        unz_file_info fi;
        char name[512];
        if (unzGetCurrentFileInfo(uf, &fi, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) {
            ok = false;
            break;
        }
        if (!zipPathSafe(name)) {
            ok = false;
            break;
        }
        size_t nl = strlen(name);
        bool isDir = nl > 0 && (name[nl - 1] == '/' || name[nl - 1] == '\\');

        char out[1024];
        int w = snprintf(out, sizeof(out), "%s/%s", destRoot, name);
        if (w <= 0 || (size_t)w >= sizeof(out)) {
            ok = false;
            break;
        }

        if (isDir) {
            mkdir(out, 0777);
        } else {
            if (!mkdirParents(out)) {
                ok = false;
                break;
            }
            if (unzOpenCurrentFile(uf) != UNZ_OK) {
                ok = false;
                break;
            }
            FILE *f = fopen(out, "wb");
            if (!f) {
                unzCloseCurrentFile(uf);
                ok = false;
                break;
            }
            int r;
            while ((r = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
                    ok = false;
                    break;
                }
                done += r;
                if (s_progressCb) s_progressCb(GRID0PLUS_UPDATE_PHASE_INSTALL, done, s_progressTotal);
            }
            if (r < 0) ok = false;
            fclose(f);
            unzCloseCurrentFile(uf);
        }

        if (ok && i + 1 < gi.number_entry && unzGoToNextFile(uf) != UNZ_OK) ok = false;
    }

    unzClose(uf);
    return ok;
}

Grid0plusUpdateResult update_apply(long expectedSize, Grid0plusUpdateProgressFn onProgress) {
    if (s_downloadUrl[0] == '\0') return GRID0PLUS_UPDATE_NET_FAIL;
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

    romfsExit();

    bool placed = extractZip(UPDATE_ZIP_TMP, "sdmc:/");

    romfsInit();
    remove(UPDATE_ZIP_TMP);
    fsdevCommitDevice("sdmc");

    if (!placed) return GRID0PLUS_UPDATE_WRITE_FAIL;
    return GRID0PLUS_UPDATE_OK;
}
