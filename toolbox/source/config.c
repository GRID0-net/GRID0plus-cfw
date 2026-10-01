#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

char g_server_ip[GRID0PLUS_SERVER_IP_MAX] = GRID0PLUS_SERVER_IP_DEFAULT;

static bool s_backupDone = false;
static bool s_restoreOnDefault = true;

static Mutex s_traceMtx;

void grid0plus_trace(const char *step) {
    mutexLock(&s_traceMtx);
    grid0plus_ensure_dir(GRID0PLUS_DIR);
    FILE *f = fopen(GRID0PLUS_TRACE_PATH, "a");
    if (f) {
        fputs(step, f);
        fputc('\n', f);
        fclose(f);
    }
    fsdevCommitDevice("sdmc");
    mutexUnlock(&s_traceMtx);
}

bool grid0plus_ensure_dir(const char *path) {
    char tmp[FS_MAX_PATH];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    char *p = strchr(tmp, ':');   // skip the "sdmc:" prefix
    p = p ? p + 1 : tmp;
    if (*p == '/') p++;

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

// Simple "key=value" text config, one entry per line, no section headers needed,
// there's only ever one app's settings in this file.
void config_load(void) {
    strncpy(g_server_ip, GRID0PLUS_SERVER_IP_DEFAULT, sizeof(g_server_ip) - 1);
    g_server_ip[sizeof(g_server_ip) - 1] = '\0';
    s_backupDone = false;
    s_restoreOnDefault = true;

    FILE *f = fopen(GRID0PLUS_CONFIG_FILE, "rb");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "server_ip") == 0 && val[0]) {
            if (strcmp(val, GRID0PLUS_SERVER_IP_PREVIOUS) == 0) val = GRID0PLUS_SERVER_IP_DEFAULT;
            strncpy(g_server_ip, val, sizeof(g_server_ip) - 1);
            g_server_ip[sizeof(g_server_ip) - 1] = '\0';
        } else if (strcmp(key, "backup_done") == 0) {
            s_backupDone = atoi(val) != 0;
        } else if (strcmp(key, "restore_on_default") == 0) {
            s_restoreOnDefault = atoi(val) != 0;
        }
    }
    fclose(f);
}

bool config_save(void) {
    if (!grid0plus_ensure_dir(GRID0PLUS_DIR)) return false;
    FILE *f = fopen(GRID0PLUS_CONFIG_FILE, "wb");
    if (!f) return false;
    fprintf(f, "server_ip=%s\n", g_server_ip);
    fprintf(f, "backup_done=%d\n", s_backupDone ? 1 : 0);
    fprintf(f, "restore_on_default=%d\n", s_restoreOnDefault ? 1 : 0);
    fclose(f);
    fsdevCommitDevice("sdmc");
    return true;
}

bool config_set_server_ip(const char *ip) {
    strncpy(g_server_ip, ip, sizeof(g_server_ip) - 1);
    g_server_ip[sizeof(g_server_ip) - 1] = '\0';
    return config_save();
}

bool config_get_backup_done(void) { return s_backupDone; }

void config_set_backup_done(bool done) {
    s_backupDone = done;
    config_save();
}

bool config_get_restore_on_default(void) { return s_restoreOnDefault; }

void config_set_restore_on_default(bool on) {
    s_restoreOnDefault = on;
    config_save();
}
