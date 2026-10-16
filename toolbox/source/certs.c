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
            if (!grid0plus_ensure_dir(dp)) { ok = false; continue; }
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

// Recursively deletes everything under dir (an sdmc:/ path), then the
// directory itself. Used only for the LEGACY_OWNED_DIRS below, which are
// exefs_patches/nro_patches leaves named after opaque per-firmware-build-ID
// hashes -- there is no fixed filename list to remove one at a time, but the
// directory name itself is exclusively ours, so it's safe to take everything
// under it.
void grid0plus_remove_dir_and_contents(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { rmdir(dir); return; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[FS_MAX_PATH];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) grid0plus_remove_dir_and_contents(p);
        else remove(p);
    }
    closedir(d);
    rmdir(dir);
}

// Every absolute SD-card path a RELEASED version of this app has ever
// written outside of /atmosphere/hosts (hosts.c's job) and GRID0PLUS_DIR's
// own config/backup/trace files (config.c/backup.c's job -- backup_restore()
// needs GRID0PLUS_BACKUP_DIR to still exist when apply_default() calls it
// moments after this file's caller runs, and wiping config.cfg here would
// discard the user's saved IP and toggle prefs on every single reset).
//
// This list is append-only. A path leaves it only when a comment right here
// explains why it's certain no released build ever wrote there -- removing
// an entry quietly is exactly how the bug this list exists to prevent comes
// back: a console that installed an old GRID0+ version keeps orphaned
// files a newer version's own romfs:/sd/ tree (see copyTree/removeTree
// above) no longer mentions, so it can never see them to clean up on its
// own. Pre-v0.3.0 (before commit e3f7a62 restructured certs.c into a tree
// mirror) wrote CA files at these same four paths under a hardcoded list,
// so nothing here predates the mirror -- but the exefs_patches/nro_patches
// directories were ADDED in that same commit, meaning anyone who last
// touched this app on v0.2.x has a CA installed but none of the patches
// that make the system SSL service and browser applet actually trust it
// (error 2123-0308 during account linking) until they both update the .nro
// itself AND re-run "Switch to GRID0+" -- reapplying alone won't remove
// anything, since nothing here has ever changed CONTENT across a version,
// only gained new files. This list matters for the opposite direction:
// guaranteeing "Switch to Default" fully cleans up regardless of which
// version put things there.
static const char *const LEGACY_OWNED_FILES[] = {
    "sdmc:/rootCA.pem",
    "sdmc:/GRID0plus/certs/grid0plus_root_ca.pem",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/browser/RootCaEtc.pem",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/browser/RootCaSdkAdditional.pem",
};
#define LEGACY_OWNED_FILE_COUNT (sizeof(LEGACY_OWNED_FILES) / sizeof(LEGACY_OWNED_FILES[0]))

static const char *const LEGACY_OWNED_DIRS[] = {
    "sdmc:/atmosphere/exefs_patches/disable_ca_verification",
    "sdmc:/atmosphere/exefs_patches/s3certpin_bypass",
    "sdmc:/atmosphere/exefs_patches/s3verifyoption_bypass",
    // The hand-installed test build of s3smallmatch_bypass: same patch,
    // removed so a console does not carry it twice.
    "sdmc:/atmosphere/exefs_patches/s3_wait_fullmember_test",
    // Hand-installed test builds of the Tricolor pair records (now in
    // s3smallmatch_bypass) and of s3moveless_bypass.
    "sdmc:/atmosphere/exefs_patches/s3tricolor_test",
    "sdmc:/atmosphere/exefs_patches/s3tricolor_pair_test",
    "sdmc:/atmosphere/exefs_patches/s3moveless_test",
    "sdmc:/atmosphere/nro_patches/disable_browser_ca_verification",
    "sdmc:/GRID0plus/certs",
    // Bundled in the romfs until patches moved to patches-<serial> releases
    // (patches.c). A console that never ran a newer Toolbox in GRID0+ mode
    // has them with no patches-installed.txt entry to remove them by.
    "sdmc:/atmosphere/exefs_patches/s3grpcpeer_bypass",
    "sdmc:/atmosphere/exefs_patches/s3grpcverify_bypass",
    "sdmc:/atmosphere/exefs_patches/s3moveless_bypass",
    "sdmc:/atmosphere/exefs_patches/s3smallmatch_bypass",
    "sdmc:/atmosphere/exefs_patches/bcat_signature_bypass",
    "sdmc:/atmosphere/exefs_patches/account_delete_user_fix",
};
#define LEGACY_OWNED_DIR_COUNT (sizeof(LEGACY_OWNED_DIRS) / sizeof(LEGACY_OWNED_DIRS[0]))

static void purgeLegacyOwnedPaths(void) {
    for (size_t i = 0; i < LEGACY_OWNED_FILE_COUNT; i++) remove(LEGACY_OWNED_FILES[i]);
    for (size_t i = 0; i < LEGACY_OWNED_DIR_COUNT; i++) grid0plus_remove_dir_and_contents(LEGACY_OWNED_DIRS[i]);

    // Best-effort: 0100000000000803 is LibraryAppletWeb's real title ID, so
    // other homebrew could legitimately have its own override living
    // alongside ours under the same folder -- rmdir() only succeeds on an
    // already-empty directory, so anything else there is left untouched.
    rmdir("sdmc:/atmosphere/contents/0100000000000803/romfs/browser");
    rmdir("sdmc:/atmosphere/contents/0100000000000803/romfs");
    rmdir("sdmc:/atmosphere/contents/0100000000000803");
}

bool certs_provision(void) {
    bool ok = copyTree(PROVISION_SRC_ROOT, PROVISION_DST_ROOT);
    fsdevCommitDevice("sdmc");
    grid0plus_trace(ok ? "certs: provisioned (CA)"
                       : "certs: some files failed to provision");
    return ok;
}

void certs_remove(void) {
    removeTree(PROVISION_SRC_ROOT, PROVISION_DST_ROOT);
    purgeLegacyOwnedPaths();
    fsdevCommitDevice("sdmc");
    grid0plus_trace("certs: removed");
}
