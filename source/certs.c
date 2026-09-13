#include "certs.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

typedef struct {
    const char *src;   // romfs:/...
    const char *dst;   // sdmc:/...
} CertFile;

// Keep this list and its ordering (children before the parent directories
// they live in) in sync with the rmdir cleanup below.
static const CertFile CERT_FILES[] = {
    { "romfs:/certs/switchnet_root_ca.pem", "sdmc:/switchnet/certs/switchnet_root_ca.pem" },
    { "romfs:/certs/rootCA.pem",            "sdmc:/rootCA.pem" },
    { "romfs:/certs/browser/RootCaEtc.pem",
      "sdmc:/atmosphere/contents/0100000000000803/romfs/browser/RootCaEtc.pem" },
    { "romfs:/certs/browser/RootCaSdkAdditional.pem",
      "sdmc:/atmosphere/contents/0100000000000803/romfs/browser/RootCaSdkAdditional.pem" },
};
#define CERT_FILE_COUNT (sizeof(CERT_FILES) / sizeof(CERT_FILES[0]))

// Directories to try to remove after the files above are gone, listed
// deepest-first. rmdir() only succeeds on an empty directory, so anything the
// user (or another homebrew) placed alongside these files is left untouched.
static const char *const CERT_DIRS[] = {
    "sdmc:/atmosphere/contents/0100000000000803/romfs/browser",
    "sdmc:/atmosphere/contents/0100000000000803/romfs",
    "sdmc:/atmosphere/contents/0100000000000803",
    "sdmc:/switchnet/certs",
};
#define CERT_DIR_COUNT (sizeof(CERT_DIRS) / sizeof(CERT_DIRS[0]))

static bool dirnameOf(const char *path, char *out, size_t outCap) {
    const char *slash = strrchr(path, '/');
    if (!slash) return false;
    size_t len = (size_t)(slash - path);
    if (len >= outCap) return false;
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

static bool copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    if (!ok) remove(dst);
    return ok;
}

bool certs_provision(void) {
    bool allOk = true;
    for (size_t i = 0; i < CERT_FILE_COUNT; i++) {
        char dir[FS_MAX_PATH];
        if (dirnameOf(CERT_FILES[i].dst, dir, sizeof(dir))) switchnet_ensure_dir(dir);
        if (!copyFile(CERT_FILES[i].src, CERT_FILES[i].dst)) {
            allOk = false;
            char msg[300];
            snprintf(msg, sizeof(msg), "certs: failed to install %s", CERT_FILES[i].dst);
            switchnet_trace(msg);
        }
    }
    fsdevCommitDevice("sdmc");
    switchnet_trace(allOk ? "certs: provisioned (stub files)" : "certs: some files failed to provision");
    return allOk;
}

void certs_remove(void) {
    for (size_t i = 0; i < CERT_FILE_COUNT; i++) remove(CERT_FILES[i].dst);
    for (size_t i = 0; i < CERT_DIR_COUNT; i++) rmdir(CERT_DIRS[i]);
    fsdevCommitDevice("sdmc");
    switchnet_trace("certs: removed");
}
