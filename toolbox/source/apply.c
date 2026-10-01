#include "apply.h"
#include "backup.h"
#include "certs.h"
#include "config.h"
#include "hosts.h"

#include <stdio.h>

Grid0plusMode apply_current_mode(void) {
    return hosts_is_grid0plus_active() ? GRID0PLUS_MODE_GRID0PLUS : GRID0PLUS_MODE_DEFAULT;
}

bool apply_grid0plus(const char *ip) {
    grid0plus_trace("apply_grid0plus: start");

    if (!grid0plus_ensure_dir(GRID0PLUS_HOSTS_DIR)) {
        grid0plus_trace("apply_grid0plus: could not create hosts dir");
        return false;
    }

    // One-time safety net: back up whatever the user already had before we
    // ever write to /atmosphere/hosts for the first time. After that, backups
    // are only made when the user explicitly asks for one.
    if (!config_get_backup_done()) {
        backup_create();
        config_set_backup_done(true);
    }

    bool certsOk = certs_provision();
    bool hostsOk = hosts_write_all(ip);
    // add_defaults_to_dns_hosts=true merges Atmosphère's own default entries
    // (telemetry null-routing) with ours, so nothing about stock privacy
    // protection regresses just because GRID0+ mode is active.
    bool iniOk = hosts_set_dns_mitm(true, true);
    // false: this emuMMC's real PRODINFO, not a blanked one. It never leaves
    // the emuMMC for real Nintendo (DNS-MITM keeps it fully isolated to
    // GRID0+'s own server), and the account/BAAS auth flow that reads
    // PRODINFO-derived device identity needs the real thing -- see
    // hosts_set_blank_prodinfo_emummc's own doc comment.
    bool prodinfoOk = hosts_set_blank_prodinfo_emummc(false);

    fsdevCommitDevice("sdmc");
    grid0plus_trace((hostsOk && iniOk && prodinfoOk) ? "apply_grid0plus: done" : "apply_grid0plus: FAILED");
    return hostsOk && iniOk && certsOk && prodinfoOk;
}

bool apply_default(void) {
    grid0plus_trace("apply_default: start");

    hosts_clear_own();

    if (config_get_restore_on_default() && backup_exists()) {
        backup_restore();
    }

    certs_remove();

    // If our redirections are really gone, keep DNS-MITM on with Atmosphère's
    // own defaults merged in, that's strictly more private than turning
    // DNS-MITM off outright, and it's what a stock Atmosphère install does.
    // Fall back to disabling DNS-MITM only if our hosts somehow survived the
    // removal above, so Default mode is never silently still redirected.
    bool hostsGone = !hosts_is_grid0plus_active();
    bool iniOk = hostsGone ? hosts_set_dns_mitm(true, true) : hosts_set_dns_mitm(false, false);
    // true: blank this emuMMC's PRODINFO again. The anti-ban reasoning is the
    // same as returning to real Nintendo -- this console's real device
    // identity should never be presented on a boot that might reach
    // Nintendo's actual servers, even by mistake.
    bool prodinfoOk = hosts_set_blank_prodinfo_emummc(true);

    fsdevCommitDevice("sdmc");
    grid0plus_trace((iniOk && prodinfoOk) ? "apply_default: done" : "apply_default: FAILED");
    return iniOk && prodinfoOk;
}

Result grid0plus_reboot(void) {
    Result rc = bpcInitialize();
    if (R_FAILED(rc)) return rc;
    rc = bpcRebootSystem();
    bpcExit();
    return rc;
}
