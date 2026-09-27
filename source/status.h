// GRID0+ Toolbox — server status screen.
//
// Talks to GRID0+'s own toolbox API (see the switchnet repo's
// internal/toolbox package) to show a coarse per-service status: whether
// dauth, aauth, baas, stubs, npln, dashboard, dns and natcheck are up. No
// credential involved on either side — see net.h/update.c for how
// GRID0+'s toolbox API also keeps the update-checker's GitHub token off
// this app entirely.
#ifndef SWITCHNET_STATUS_H
#define SWITCHNET_STATUS_H

#define STATUS_MAX_SERVICES 16
#define STATUS_NAME_MAX     32
#define STATUS_VALUE_MAX    16
#define STATUS_REASON_MAX   96

typedef struct {
    char name[STATUS_NAME_MAX];
    // "ok", "degraded", "down", or "unknown".
    char status[STATUS_VALUE_MAX];
    // Set only when status is "unknown" — why this service could not be
    // probed at all (e.g. "UDP, not HTTP: no /healthz to ask").
    char reason[STATUS_REASON_MAX];
} StatusRow;

typedef enum {
    STATUS_FETCH_OK = 0,
    STATUS_FETCH_NET_FAIL,
} StatusFetchResult;

// Fetches GRID0+'s current server status. Blocking; manages
// sockets/SSL internally. `rows` must have room for STATUS_MAX_SERVICES
// entries; `*outCount` is set to how many were actually filled (0 on
// failure).
StatusFetchResult status_fetch(StatusRow *rows, int *outCount);

#endif // SWITCHNET_STATUS_H
