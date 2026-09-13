// SwitchNet Toolbox — entry point and menu.
//
// A small text menu (no custom graphics — this app only needs to be
// readable, not pretty):
//
//   Switch to SwitchNet
//   Switch to Default (restore original hosts)
//   Set custom SwitchNet IP
//   Reset IP to default
//   Back up hosts folder now
//   Restore backup on Default mode: ON/OFF
//   Check for updates
//   Exit
//
// Switching modes writes Atmosphère's DNS-MITM hosts files and reboots (the
// hosts files are only read at boot). Update checks are on demand — this app
// never phones home unless the user presses the button for it.
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "apply.h"
#include "backup.h"
#include "certs.h"
#include "config.h"
#include "hosts.h"
#include "update.h"
#include "version.h"

enum {
    MENU_APPLY_SWITCHNET,
    MENU_APPLY_DEFAULT,
    MENU_SET_IP,
    MENU_RESET_IP,
    MENU_BACKUP_NOW,
    MENU_TOGGLE_RESTORE,
    MENU_CHECK_UPDATE,
    MENU_EXIT,
    MENU_COUNT,
};

static const char *menuLabel(int i, char *buf, size_t cap) {
    switch (i) {
        case MENU_APPLY_SWITCHNET: return "Switch to SwitchNet";
        case MENU_APPLY_DEFAULT:   return "Switch to Default (restore original hosts)";
        case MENU_SET_IP:          return "Set custom SwitchNet IP";
        case MENU_RESET_IP:        return "Reset IP to default (" SWITCHNET_SERVER_IP_DEFAULT ")";
        case MENU_BACKUP_NOW:      return "Back up hosts folder now";
        case MENU_TOGGLE_RESTORE:
            snprintf(buf, cap, "Restore backup on Default mode: %s",
                     config_get_restore_on_default() ? "ON" : "OFF");
            return buf;
        case MENU_CHECK_UPDATE:    return "Check for updates";
        case MENU_EXIT:            return "Exit";
        default:                   return "";
    }
}

static void drawHeader(void) {
    printf("SwitchNet Toolbox  v%d.%d.%d\n",
           SWITCHNET_VERSION_MAJOR, SWITCHNET_VERSION_MINOR, SWITCHNET_VERSION_PATCH);
    printf("========================================\n\n");
    printf("Current mode : %s\n",
           apply_current_mode() == SWITCHNET_MODE_SWITCHNET ? "SWITCHNET" : "DEFAULT");
    printf("SwitchNet IP : %s\n\n", g_server_ip);
}

static bool isValidIPv4(const char *s) {
    int octets = 0, digits = 0, value = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            digits++;
            if (digits > 3 || value > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0) return false;
            octets++;
            value = 0;
            digits = 0;
            if (*p == '\0') break;
        } else {
            return false;
        }
    }
    return octets == 4;
}

// Blocking yes/no prompt. Returns true on A, false on B or +.
static bool confirmScreen(PadState *pad, const char *title, const char *body) {
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 k = padGetButtonsDown(pad);
        consoleClear();
        drawHeader();
        printf("%s\n\n%s\n\n", title, body);
        printf("(A) Yes   (B) No\n");
        consoleUpdate(NULL);
        if (k & HidNpadButton_A) return true;
        if (k & (HidNpadButton_B | HidNpadButton_Plus)) return false;
        svcSleepThread(16000000ULL);
    }
    return false;
}

static void messageScreen(PadState *pad, const char *title, const char *body) {
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 k = padGetButtonsDown(pad);
        consoleClear();
        drawHeader();
        printf("%s\n\n%s\n\n", title, body);
        printf("(A/B/+) Continue\n");
        consoleUpdate(NULL);
        if (k & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) return;
        svcSleepThread(16000000ULL);
    }
}

static bool promptIpInput(char *out, size_t cap) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetInitialText(&kbd, g_server_ip);
    swkbdConfigSetGuideText(&kbd, "SwitchNet server IP, e.g. 9.205.104.23");
    swkbdConfigSetStringLenMax(&kbd, (int)cap - 1);

    char buf[64] = {0};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);

    if (R_FAILED(rc) || buf[0] == '\0') return false;
    strncpy(out, buf, cap - 1);
    out[cap - 1] = '\0';
    return true;
}

static int s_updLastPct = -1;

static void updateProgressCb(SwitchnetUpdatePhase phase, long done, long total) {
    int pct = (total > 0) ? (int)((done * 100) / total) : 0;
    if (pct > 100) pct = 100;
    if (pct == s_updLastPct) return;
    s_updLastPct = pct;

    consoleClear();
    drawHeader();
    printf("%s...\n\n", phase == SWITCHNET_UPDATE_PHASE_INSTALL ? "Installing update" : "Downloading update");
    if (total > 0)
        printf("%d%%  (%ld / %ld KiB)\n", pct, done / 1024, total / 1024);
    else
        printf("%ld KiB\n", done / 1024);
    consoleUpdate(NULL);
}

static void runUpdateFlow(PadState *pad) {
    consoleClear();
    drawHeader();
    printf("Checking for updates...\n");
    consoleUpdate(NULL);

    SwitchnetUpdate upd = update_check();

    if (!upd.available) {
        messageScreen(pad, "Up to date", "You already have the latest version.");
        return;
    }

    char body[128];
    snprintf(body, sizeof(body), "Version %d.%d.%d is available. Download and install now?",
             upd.maj, upd.min, upd.patch);
    if (!confirmScreen(pad, "Update available", body)) return;

    s_updLastPct = -1;
    SwitchnetUpdateResult res = update_apply(upd.size, updateProgressCb);

    switch (res) {
        case SWITCHNET_UPDATE_OK:
            messageScreen(pad, "Update installed",
                          "Close this app and relaunch it to run the new version.");
            break;
        case SWITCHNET_UPDATE_SIZE_FAIL:
            messageScreen(pad, "Update failed", "Downloaded file size did not match. Try again.");
            break;
        case SWITCHNET_UPDATE_WRITE_FAIL:
            messageScreen(pad, "Update failed", "Could not write to the SD card.");
            break;
        default:
            messageScreen(pad, "Update failed", "Network error. Check your connection and try again.");
            break;
    }
}

int main(int argc, char **argv) {
    // hbmenu passes the running .nro's real path in argv[0] — the updater
    // needs it to replace the correct file (not just a fixed default path).
    update_set_self_path((argc > 0 && argv) ? argv[0] : NULL);

    config_load();

    consoleInit(NULL);
    romfsInit();   // keeps the cert stub files in romfs:/certs/ available

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    int sel = 0;
    char status[192] = {0};

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        consoleClear();
        drawHeader();
        for (int i = 0; i < MENU_COUNT; i++) {
            char buf[64];
            printf("%s %s\n", i == sel ? ">" : " ", menuLabel(i, buf, sizeof(buf)));
        }
        if (status[0]) printf("\n%s\n", status);
        printf("\n(Up/Down) Move   (A) Select   (+) Exit\n");
        consoleUpdate(NULL);

        if (k & HidNpadButton_AnyUp)   { sel = (sel + MENU_COUNT - 1) % MENU_COUNT; status[0] = '\0'; }
        if (k & HidNpadButton_AnyDown) { sel = (sel + 1) % MENU_COUNT; status[0] = '\0'; }
        if (k & HidNpadButton_Plus) break;

        if (k & HidNpadButton_A) {
            status[0] = '\0';
            switch (sel) {
                case MENU_APPLY_SWITCHNET: {
                    char body[160];
                    snprintf(body, sizeof(body),
                             "This will redirect Nintendo online traffic to %s and reboot the console. Continue?",
                             g_server_ip);
                    if (confirmScreen(&pad, "Switch to SwitchNet", body)) {
                        bool ok = apply_switchnet(g_server_ip);
                        if (ok) {
                            consoleClear(); drawHeader();
                            printf("Applied. Rebooting...\n");
                            consoleUpdate(NULL);
                            svcSleepThread(1200000000ULL);
                            switchnet_reboot();
                            snprintf(status, sizeof(status), "Reboot failed — please restart manually.");
                        } else {
                            snprintf(status, sizeof(status), "Failed to write to the SD card.");
                        }
                    }
                    break;
                }
                case MENU_APPLY_DEFAULT: {
                    if (confirmScreen(&pad, "Switch to Default",
                                       "This will remove SwitchNet's redirections (restoring your backup, "
                                       "if any) and reboot the console. Continue?")) {
                        bool ok = apply_default();
                        if (ok) {
                            consoleClear(); drawHeader();
                            printf("Applied. Rebooting...\n");
                            consoleUpdate(NULL);
                            svcSleepThread(1200000000ULL);
                            switchnet_reboot();
                            snprintf(status, sizeof(status), "Reboot failed — please restart manually.");
                        } else {
                            snprintf(status, sizeof(status), "Failed to write to the SD card.");
                        }
                    }
                    break;
                }
                case MENU_SET_IP: {
                    char ip[64];
                    if (promptIpInput(ip, sizeof(ip))) {
                        if (isValidIPv4(ip)) {
                            config_set_server_ip(ip);
                            snprintf(status, sizeof(status), "SwitchNet IP set to %s.", g_server_ip);
                        } else {
                            snprintf(status, sizeof(status), "\"%s\" is not a valid IPv4 address.", ip);
                        }
                    }
                    break;
                }
                case MENU_RESET_IP:
                    config_set_server_ip(SWITCHNET_SERVER_IP_DEFAULT);
                    snprintf(status, sizeof(status), "SwitchNet IP reset to default.");
                    break;
                case MENU_BACKUP_NOW: {
                    int n = backup_create();
                    snprintf(status, sizeof(status),
                             n > 0 ? "Backed up %d file(s) from /atmosphere/hosts."
                                   : "Nothing to back up (folder empty, or already SwitchNet's own files).",
                             n);
                    break;
                }
                case MENU_TOGGLE_RESTORE:
                    config_set_restore_on_default(!config_get_restore_on_default());
                    break;
                case MENU_CHECK_UPDATE:
                    runUpdateFlow(&pad);
                    break;
                case MENU_EXIT:
                    goto done;
            }
        }

        svcSleepThread(16000000ULL);
    }

done:
    romfsExit();
    consoleExit(NULL);
    return 0;
}
