// SwitchNet Toolbox — minimal HTTPS client (raw BSD sockets + the console's
// native SSL service), used by the updater and the server-status screen to
// talk to GitHub and to SwitchNet's own toolbox API. No libcurl or other
// portlib dependency.
#ifndef SWITCHNET_NET_H
#define SWITCHNET_NET_H

#include <stddef.h>
#include <stdio.h>

#define NET_ERR_UNKNOWN -1   // unspecified failure
#define NET_ERR_CONNECT -2   // TCP connect failed
#define NET_ERR_TLS     -3   // TLS handshake / socket setup failed
#define NET_ERR_PROTO   -4   // malformed HTTP response
#define NET_ERR_OOM     -5   // allocation failure

// HTTPS GET https://host:port/path. Requires socketInitializeDefault() and
// sslInitialize() to already be active. `host` may be a hostname (resolved
// normally) or a literal dotted-quad IP — SwitchNet's own toolbox API is
// reached by IP, on its own port, never by a redirected Nintendo hostname.
// Returns the response body (malloc'd, caller frees) and its length, or NULL
// on failure (*out_status carries a NET_ERR_* code or the HTTP status).
unsigned char *net_https_get(const char *host, int port, const char *path,
                              size_t *out_len, int *out_status);

// Progress callback for a streaming download: `received` bytes of body so
// far, out of `total` (0 if the server didn't advertise a size).
typedef void (*net_progress_fn)(long received, long total);

// HTTPS GET streamed directly to `out`, following at most one 3xx redirect
// (the redirect target's own host:port is used for the follow-up request,
// falling back to `port` if the target names no explicit port). Returns the
// number of body bytes written, -1 on network/protocol failure, -2 on a
// write failure. `onProgress` may be NULL.
long net_https_get_to_file(const char *host, int port, const char *path, FILE *out,
                            int *out_status, net_progress_fn onProgress);

#endif // SWITCHNET_NET_H
