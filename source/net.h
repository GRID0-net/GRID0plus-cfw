// SwitchNet Toolbox — minimal HTTPS client (raw BSD sockets + the console's
// native SSL service), used only by the updater to talk to GitHub. No libcurl
// or other portlib dependency.
#ifndef SWITCHNET_NET_H
#define SWITCHNET_NET_H

#include <stddef.h>
#include <stdio.h>

#define NET_ERR_UNKNOWN -1   // unspecified failure
#define NET_ERR_CONNECT -2   // TCP connect failed
#define NET_ERR_TLS     -3   // TLS handshake / socket setup failed
#define NET_ERR_PROTO   -4   // malformed HTTP response
#define NET_ERR_OOM     -5   // allocation failure

// HTTPS GET https://host/path. Requires socketInitializeDefault() and
// sslInitialize() to already be active. Returns the response body (malloc'd,
// caller frees) and its length, or NULL on failure (*out_status carries a
// NET_ERR_* code or the HTTP status).
unsigned char *net_https_get(const char *host, const char *path, size_t *out_len, int *out_status);

// Progress callback for a streaming download: `received` bytes of body so
// far, out of `total` (0 if the server didn't advertise a size).
typedef void (*net_progress_fn)(long received, long total);

// HTTPS GET streamed directly to `out`, following at most one 3xx redirect
// (GitHub release assets redirect to a presigned CDN URL). Returns the number
// of body bytes written, -1 on network/protocol failure, -2 on a write
// failure. `onProgress` may be NULL.
long net_https_get_to_file(const char *host, const char *path, FILE *out,
                            int *out_status, net_progress_fn onProgress);

#endif // SWITCHNET_NET_H
