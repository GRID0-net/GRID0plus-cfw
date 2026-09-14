#include "certs.h"
#include "config.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

#define PROVISION_SRC_ROOT "romfs:/sd"
#define PROVISION_DST_ROOT "sdmc:"

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

// Recursively mirrors srcDir (a romfs:/ path) into dstDir (an sdmc:/ path),
// creating directories as needed and overwriting existing files.
static bool copyTree(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return false;
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!switchnet_ensure_dir(dp)) { ok = false; continue; }
            if (!copyTree(sp, dp)) ok = false;
        } else if (!copyFile(sp, dp)) {
            ok = false;
        }
    }
    closedir(d);
    return ok;
}

// Mirror image of copyTree(): for every path that exists under srcDir,
// removes the corresponding path under dstDir, then rmdir()s directories
// left empty by that removal. rmdir() fails harmlessly on a non-empty
// directory, so anything else sharing that directory (other exefs_patches,
// other contents) is left alone.
static void removeTree(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            removeTree(sp, dp);
            rmdir(dp);
        } else {
            remove(dp);
        }
    }
    closedir(d);
}

bool certs_provision(void) {
    bool ok = copyTree(PROVISION_SRC_ROOT, PROVISION_DST_ROOT);
    fsdevCommitDevice("sdmc");
    switchnet_trace(ok ? "certs: provisioned (CA + CA-bypass patches)"
                       : "certs: some files failed to provision");
    return ok;
}

void certs_remove(void) {
    removeTree(PROVISION_SRC_ROOT, PROVISION_DST_ROOT);
    fsdevCommitDevice("sdmc");
    switchnet_trace("certs: removed");
}
