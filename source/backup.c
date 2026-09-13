#include "backup.h"
#include "config.h"
#include "hosts.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

static bool copyFileRaw(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
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

static bool fileContains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

bool backup_exists(void) {
    DIR *d = opendir(SWITCHNET_BACKUP_DIR);
    if (!d) return false;
    struct dirent *e;
    bool any = false;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        any = true;
        break;
    }
    closedir(d);
    return any;
}

int backup_create(void) {
    if (!switchnet_ensure_dir(SWITCHNET_DIR)) return 0;
    if (!switchnet_ensure_dir(SWITCHNET_BACKUP_DIR)) return 0;

    DIR *d = opendir(SWITCHNET_HOSTS_DIR);
    if (!d) { switchnet_trace("backup: hosts folder does not exist yet, nothing to save"); return 0; }

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

        char src[FS_MAX_PATH], dst[FS_MAX_PATH];
        snprintf(src, sizeof(src), "%s/%s", SWITCHNET_HOSTS_DIR, e->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", SWITCHNET_BACKUP_DIR, e->d_name);

        struct stat st;
        if (stat(src, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        // Never back up a file we generated ourselves: it would let a
        // SwitchNet redirection quietly come back through "restore".
        if (fileContains(src, SWITCHNET_HOSTS_HEADER_MARK)) continue;

        if (copyFileRaw(src, dst)) n++;
    }
    closedir(d);

    fsdevCommitDevice("sdmc");
    char msg[64];
    snprintf(msg, sizeof(msg), "backup: %d file(s) saved", n);
    switchnet_trace(msg);
    return n;
}

int backup_restore(void) {
    if (!switchnet_ensure_dir(SWITCHNET_HOSTS_DIR)) return 0;

    // Drop our own redirections first so restored files aren't shadowed by a
    // "last matching line wins" entry left over from SwitchNet mode.
    hosts_clear_own();

    DIR *d = opendir(SWITCHNET_BACKUP_DIR);
    if (!d) return 0;

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

        char src[FS_MAX_PATH], dst[FS_MAX_PATH];
        snprintf(src, sizeof(src), "%s/%s", SWITCHNET_BACKUP_DIR, e->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", SWITCHNET_HOSTS_DIR, e->d_name);

        struct stat st;
        if (stat(src, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        // Re-validate at restore time too: a backup made by a much older,
        // differently-behaved build could in principle carry our own marker.
        if (fileContains(src, SWITCHNET_HOSTS_HEADER_MARK)) continue;

        if (copyFileRaw(src, dst)) n++;
    }
    closedir(d);

    fsdevCommitDevice("sdmc");
    char msg[64];
    snprintf(msg, sizeof(msg), "restore: %d file(s) restored", n);
    switchnet_trace(msg);
    return n;
}
