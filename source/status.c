#include "status.h"
#include "config.h"
#include "net.h"

#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define TOOLBOX_STATUS_PATH "/status"

// Finds a JSON string value for `key` (e.g. "\"name\""), tolerating optional
// whitespace around ':'. Returns a pointer just past the opening quote, or
// NULL. Same pragmatic approach update.c uses for the release JSON — the
// shape here is just as fixed and small, so a real JSON parser buys nothing.
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

static void copyJsonString(const char *src, char *dst, size_t cap) {
    dst[0] = '\0';
    if (!src) return;
    const char *end = strchr(src, '"');
    if (!end) return;
    size_t len = (size_t)(end - src);
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

StatusFetchResult status_fetch(StatusRow *rows, int *outCount) {
    *outCount = 0;

    socketInitializeDefault();
    if (R_FAILED(sslInitialize(4))) { socketExit(); return STATUS_FETCH_NET_FAIL; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = net_https_get(g_server_ip, SWITCHNET_TOOLBOX_PORT,
                                         TOOLBOX_STATUS_PATH, &len, &status);
    sslExit();
    socketExit();

    if (!body || status != 200) {
        free(body);
        return STATUS_FETCH_NET_FAIL;
    }

    char *json = (char *)malloc(len + 1);
    if (!json) { free(body); return STATUS_FETCH_NET_FAIL; }
    memcpy(json, body, len);
    json[len] = '\0';
    free(body);

    // Walk each {...} object inside the "services" array in encounter order.
    // Scoping each lookup to its own object (by temporarily NUL-terminating
    // at its closing brace) keeps a later object's fields from ever being
    // picked up for an earlier one.
    char *cursor = strstr(json, "\"services\"");
    while (cursor && *outCount < STATUS_MAX_SERVICES) {
        char *objStart = strchr(cursor, '{');
        if (!objStart) break;
        char *objEnd = strchr(objStart, '}');
        if (!objEnd) break;

        char saved = *objEnd;
        *objEnd = '\0';

        StatusRow *row = &rows[*outCount];
        copyJsonString(jsonStringValue(objStart, "\"name\""), row->name, sizeof(row->name));
        copyJsonString(jsonStringValue(objStart, "\"status\""), row->status, sizeof(row->status));
        copyJsonString(jsonStringValue(objStart, "\"reason\""), row->reason, sizeof(row->reason));

        *objEnd = saved;
        if (row->name[0]) (*outCount)++;
        cursor = objEnd + 1;
    }

    free(json);
    return STATUS_FETCH_OK;
}
