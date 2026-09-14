#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <switch.h>

#define MAX_RESPONSE_SIZE (4 * 1024 * 1024)
#define CONNECT_TIMEOUT_SEC 6

static const char *resolveHost(const char *host) {
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) return NULL;
    return inet_ntoa(*(struct in_addr *)he->h_addr_list[0]);
}

static int tcpConnect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv = { .tv_sec = CONNECT_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr(ip);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno == EINPROGRESS) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        struct timeval ct = { .tv_sec = CONNECT_TIMEOUT_SEC, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wf, NULL, &ct) <= 0) { close(fd); return -1; }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) { close(fd); return -1; }
    } else if (rc < 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    return fd;
}

static int parseStatusLine(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len && i < 64; i++) {
        if (buf[i] == ' ')
            return (buf[i + 1] - '0') * 100 + (buf[i + 2] - '0') * 10 + (buf[i + 3] - '0');
    }
    return NET_ERR_PROTO;
}

// Opens a TLS connection to host:port and completes the handshake. On
// success fills *sslCtx/*sslConn/*rawFd/*sslFd; on failure everything is
// cleaned up and false is returned.
static bool sslConnect(const char *host, int port, SslContext *sslCtx, SslConnection *sslConn,
                        int *rawFd, int *sslFd) {
    const char *ip = resolveHost(host);
    if (!ip) return false;

    int fd = tcpConnect(ip, port);
    if (fd < 0) return false;

    if (R_FAILED(sslCreateContext(sslCtx, SslVersion_Auto))) { close(fd); return false; }
    if (R_FAILED(sslContextCreateConnection(sslCtx, sslConn))) {
        sslContextClose(sslCtx);
        close(fd);
        return false;
    }

    int outFd = socketSslConnectionSetSocketDescriptor(sslConn, fd);
    if (outFd < 0) {
        sslConnectionClose(sslConn);
        sslContextClose(sslCtx);
        close(fd);
        return false;
    }

    bool ok = R_SUCCEEDED(sslConnectionSetHostName(sslConn, host, strlen(host)));

    // The system trust store knows neither GitHub's CA chain (this build's
    // firmware-side trust patches, if installed at all, only cover the
    // SwitchNet CA) nor SwitchNet's own self-issued edge certificate, so
    // verification is skipped for both. A release download's integrity is
    // still checked by size against what /updates/latest reported.
    if (ok) sslConnectionSetOption(sslConn, SslOptionType_SkipDefaultVerify, true);

    // Force HTTP/1.1: this client only speaks HTTP/1.1 request framing, and
    // the system SSL service may otherwise offer h2 via ALPN.
    if (ok) {
        static const uint8_t alpn[] = { 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
        sslConnectionSetNextAlpnProto(sslConn, alpn, sizeof(alpn));
    }

    if (ok) ok = R_SUCCEEDED(sslConnectionDoHandshake(sslConn, NULL, NULL, NULL, 0));

    if (!ok) {
        sslConnectionClose(sslConn);
        sslContextClose(sslCtx);
        if (outFd >= 0) close(outFd);
        return false;
    }

    *rawFd = fd;
    *sslFd = outFd;
    return true;
}

static void sslDisconnect(SslConnection *sslConn, SslContext *sslCtx, int sslFd) {
    sslConnectionClose(sslConn);
    sslContextClose(sslCtx);
    if (sslFd >= 0) close(sslFd);
}

static int sendHttpGet(SslConnection *sslConn, const char *host, const char *path) {
    char req[2560];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: SwitchNetToolbox\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n",
                      path, host);
    uint32_t written = 0;
    Result rc = sslConnectionWrite(sslConn, req, rl, &written);
    if (R_FAILED(rc) || written != (uint32_t)rl) return -1;
    return 0;
}

unsigned char *net_https_get(const char *host, int port, const char *path, size_t *out_len, int *out_status) {
    *out_len = 0;
    *out_status = NET_ERR_UNKNOWN;

    SslContext sslCtx;
    SslConnection sslConn;
    int rawFd = -1, sslFd = -1;
    if (!sslConnect(host, port, &sslCtx, &sslConn, &rawFd, &sslFd)) {
        *out_status = NET_ERR_TLS;
        return NULL;
    }

    if (sendHttpGet(&sslConn, host, path) < 0) {
        sslDisconnect(&sslConn, &sslCtx, sslFd);
        *out_status = NET_ERR_CONNECT;
        return NULL;
    }

    size_t cap = 1 << 16, len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { sslDisconnect(&sslConn, &sslCtx, sslFd); *out_status = NET_ERR_OOM; return NULL; }

    for (;;) {
        if (len + 8192 > cap) {
            if (cap >= MAX_RESPONSE_SIZE) { free(buf); sslDisconnect(&sslConn, &sslCtx, sslFd); *out_status = NET_ERR_PROTO; return NULL; }
            cap = cap * 2 > MAX_RESPONSE_SIZE ? MAX_RESPONSE_SIZE : cap * 2;
            unsigned char *nb = (unsigned char *)realloc(buf, cap);
            if (!nb) { free(buf); sslDisconnect(&sslConn, &sslCtx, sslFd); *out_status = NET_ERR_OOM; return NULL; }
            buf = nb;
        }
        uint32_t got = 0;
        Result rc = sslConnectionRead(&sslConn, buf + len, (uint32_t)(cap - len), &got);
        if (R_FAILED(rc) || got == 0) break;
        len += got;
    }
    sslDisconnect(&sslConn, &sslCtx, sslFd);

    int status = parseStatusLine(buf, len);
    *out_status = status;

    size_t bodyOff = 0;
    bool found = false;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            bodyOff = i + 4;
            found = true;
            break;
        }
    }
    if (!found) { free(buf); *out_status = NET_ERR_PROTO; return NULL; }

    size_t blen = len - bodyOff;
    unsigned char *body = (unsigned char *)malloc(blen ? blen : 1);
    if (!body) { free(buf); *out_status = NET_ERR_OOM; return NULL; }
    memcpy(body, buf + bodyOff, blen);
    free(buf);

    *out_len = blen;
    return body;
}

static void extractLocationHeader(const unsigned char *hdr, size_t hlen, char *out, size_t cap) {
    out[0] = '\0';
    for (size_t i = 0; i + 9 < hlen; i++) {
        if ((hdr[i] | 0x20) == 'l' && (hdr[i+1] | 0x20) == 'o' && (hdr[i+2] | 0x20) == 'c' &&
            (hdr[i+3] | 0x20) == 'a' && (hdr[i+4] | 0x20) == 't' && (hdr[i+5] | 0x20) == 'i' &&
            (hdr[i+6] | 0x20) == 'o' && (hdr[i+7] | 0x20) == 'n' && hdr[i+8] == ':') {
            size_t k = i + 9;
            while (k < hlen && (hdr[k] == ' ' || hdr[k] == '\t')) k++;
            size_t start = k;
            while (k < hlen && hdr[k] != '\r' && hdr[k] != '\n') k++;
            size_t vlen = k - start;
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, hdr + start, vlen);
            out[vlen] = '\0';
            return;
        }
    }
}

// Splits "host" or "host:port" into a bare host and a port, defaulting to
// `defaultPort` when the input carries none. Used only for a redirect's
// Location header, which may or may not name an explicit port.
static void splitHostPort(const char *hostport, char *hostOut, size_t hostCap,
                           int defaultPort, int *portOut) {
    const char *colon = strrchr(hostport, ':');
    size_t hostLen = colon ? (size_t)(colon - hostport) : strlen(hostport);
    if (hostLen >= hostCap) hostLen = hostCap - 1;
    memcpy(hostOut, hostport, hostLen);
    hostOut[hostLen] = '\0';
    *portOut = colon ? atoi(colon + 1) : defaultPort;
    if (*portOut <= 0) *portOut = defaultPort;
}

long net_https_get_to_file(const char *host, int port, const char *path, FILE *out,
                            int *out_status, net_progress_fn onProgress) {
    *out_status = 0;

    char curHost[256], curPath[2048];
    int curPort = port;
    strncpy(curHost, host, sizeof(curHost) - 1); curHost[sizeof(curHost) - 1] = '\0';
    strncpy(curPath, path, sizeof(curPath) - 1); curPath[sizeof(curPath) - 1] = '\0';

    for (int attempt = 0; attempt < 2; attempt++) {
        SslContext sslCtx;
        SslConnection sslConn;
        int rawFd = -1, sslFd = -1;
        if (!sslConnect(curHost, curPort, &sslCtx, &sslConn, &rawFd, &sslFd)) { *out_status = NET_ERR_TLS; return -1; }

        if (sendHttpGet(&sslConn, curHost, curPath) < 0) {
            sslDisconnect(&sslConn, &sslCtx, sslFd);
            *out_status = NET_ERR_CONNECT;
            return -1;
        }

        unsigned char rbuf[32768];
        unsigned char hdr[8192];
        size_t hlen = 0;
        int status = 0;
        bool inBody = false, isRedirect = false;
        long bodyBytes = 0, contentLen = -1;

        for (;;) {
            uint32_t got = 0;
            Result rc = sslConnectionRead(&sslConn, rbuf, sizeof(rbuf), &got);
            if (R_FAILED(rc) || got == 0) break;

            if (inBody) {
                if (!isRedirect) {
                    if (fwrite(rbuf, 1, got, out) != got) {
                        sslDisconnect(&sslConn, &sslCtx, sslFd);
                        *out_status = status;
                        return -2;
                    }
                    bodyBytes += (long)got;
                    if (onProgress) onProgress(bodyBytes, contentLen > 0 ? contentLen : 0);
                }
                continue;
            }

            size_t i = 0;
            while (i < got && hlen < sizeof(hdr)) {
                hdr[hlen++] = rbuf[i++];
                if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n')
                    break;
            }
            if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n') {
                status = parseStatusLine(hdr, hlen);
                isRedirect = (status >= 300 && status < 400);
                inBody = true;
                if (!isRedirect) {
                    for (size_t j = 0; j + 15 < hlen; j++) {
                        if ((hdr[j]|0x20)=='c' && (hdr[j+1]|0x20)=='o' && (hdr[j+2]|0x20)=='n' &&
                            (hdr[j+3]|0x20)=='t' && (hdr[j+4]|0x20)=='e' && (hdr[j+5]|0x20)=='n' &&
                            (hdr[j+6]|0x20)=='t' && hdr[j+7]=='-' && (hdr[j+8]|0x20)=='l' &&
                            (hdr[j+9]|0x20)=='e' && (hdr[j+10]|0x20)=='n' && (hdr[j+11]|0x20)=='g' &&
                            (hdr[j+12]|0x20)=='t' && (hdr[j+13]|0x20)=='h' &&
                            (hdr[j+14]==':' || hdr[j+14]==' ')) {
                            long v = 0;
                            for (size_t k = j + 14; k < hlen && hdr[k] != '\r'; k++)
                                if (hdr[k] >= '0' && hdr[k] <= '9') v = v * 10 + (hdr[k] - '0');
                            contentLen = v;
                            break;
                        }
                    }
                    if (got > i) {
                        size_t rem = got - i;
                        if (fwrite(rbuf + i, 1, rem, out) != rem) {
                            sslDisconnect(&sslConn, &sslCtx, sslFd);
                            *out_status = status;
                            return -2;
                        }
                        bodyBytes += (long)rem;
                    }
                    if (onProgress) onProgress(bodyBytes, contentLen > 0 ? contentLen : 0);
                }
            }
        }

        char location[2048] = {0};
        if (isRedirect) extractLocationHeader(hdr, hlen, location, sizeof(location));
        sslDisconnect(&sslConn, &sslCtx, sslFd);
        *out_status = status;

        if (isRedirect && attempt == 0 && location[0]) {
            char hostPort[256] = {0}, newPath[2048] = {0};
            if (sscanf(location, "https://%255[^/]%2047s", hostPort, newPath) < 2) return -1;
            splitHostPort(hostPort, curHost, sizeof(curHost), 443, &curPort);
            strncpy(curPath, newPath, sizeof(curPath) - 1); curPath[sizeof(curPath) - 1] = '\0';
            rewind(out);
            ftruncate(fileno(out), 0);
            continue;
        }

        if (!inBody) return -1;
        if (contentLen >= 0 && bodyBytes != contentLen) return -1;
        return bodyBytes;
    }

    return -1;
}
