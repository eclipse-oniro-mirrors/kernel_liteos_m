#include "litecrab/llm_transport.h"

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LLM_WIRE_RESPONSE_MAX (2U * 1024U * 1024U)
#define LLM_HTTP_HEADER_MAX (32U * 1024U)

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct ssl_method_st SSL_METHOD;
typedef struct x509_verify_param_st X509_VERIFY_PARAM;
extern const SSL_METHOD* TLS_client_method(void);
extern SSL_CTX* SSL_CTX_new(const SSL_METHOD*);
extern void SSL_CTX_free(SSL_CTX*);
extern int SSL_CTX_set_default_verify_paths(SSL_CTX*);
extern void SSL_CTX_set_verify(SSL_CTX*, int, int (*)(int, void*));
extern SSL* SSL_new(SSL_CTX*);
extern void SSL_free(SSL*);
extern int SSL_set_fd(SSL*, int);
extern long SSL_ctrl(SSL*, int, long, void*);
extern X509_VERIFY_PARAM* SSL_get0_param(SSL*);
extern int X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM*, const char*, size_t);
extern int SSL_connect(SSL*);
extern long SSL_get_verify_result(const SSL*);
extern int SSL_shutdown(SSL*);
extern int SSL_write(SSL*, const void*, int);
extern int SSL_read(SSL*, void*, int);
#define SSL_VERIFY_PEER 0x01
#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define TLSEXT_NAMETYPE_host_name 0

static int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int transport_remaining_ms(int64_t deadlineMs) {
    int64_t remaining = deadlineMs - monotonic_ms();
    return remaining > 0 ? (remaining > INT32_MAX ? INT32_MAX : (int) remaining) : 0;
}
typedef struct {
    int fd;
    SSL_CTX* ctx;
    SSL* ssl;
} Conn;
static void conn_close(Conn* c) {
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->ctx)
        SSL_CTX_free(c->ctx);
    if (c->fd >= 0)
        close(c->fd);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}
static int conn_open(Conn* c, const LiteCrabHttpRequest* request, int64_t deadlineMs) {
    memset(c, 0, sizeof *c);
    c->fd = -1;
    char port[16];
    snprintf(port, sizeof port, "%d", request->port);
    struct addrinfo hints = {0}, *res = NULL, *a;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(request->host, port, &hints, &res))
        return -201;
    for (a = res; a; a = a->ai_next) {
        c->fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (c->fd < 0)
            continue;
        int timeout = transport_remaining_ms(deadlineMs);
        if (timeout <= 0) {
            close(c->fd);
            c->fd = -1;
            break;
        }
        if (timeout > 2000)
            timeout = 2000;
        struct timeval tv = {timeout / 1000, (timeout % 1000) * 1000};
        setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (!connect(c->fd, a->ai_addr, a->ai_addrlen))
            break;
        close(c->fd);
        c->fd = -1;
    }
    freeaddrinfo(res);
    if (c->fd < 0)
        return -201;
    if (request->useTls) {
        c->ctx = SSL_CTX_new(TLS_client_method());
        if (!c->ctx) {
            conn_close(c);
            return -201;
        }
        SSL_CTX_set_default_verify_paths(c->ctx);
        SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
        c->ssl = SSL_new(c->ctx);
        if (!c->ssl) {
            conn_close(c);
            return -201;
        }
        SSL_ctrl(c->ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, (void*) request->host);
        X509_VERIFY_PARAM* param = SSL_get0_param(c->ssl);
        if (!param || !X509_VERIFY_PARAM_set1_host(param, request->host, 0)) {
            conn_close(c);
            return -201;
        }
        SSL_set_fd(c->ssl, c->fd);
        if (SSL_connect(c->ssl) != 1 || SSL_get_verify_result(c->ssl) != 0) {
            conn_close(c);
            return -201;
        }
    }
    return 0;
}
static ssize_t io_write(Conn* c, const void* d, size_t n) {
    return c->ssl ? SSL_write(c->ssl, d, (int) n) : send(c->fd, d, n, 0);
}
static ssize_t io_read(Conn* c, void* d, size_t n) {
    return c->ssl ? SSL_read(c->ssl, d, (int) n) : recv(c->fd, d, n, 0);
}
static int send_all(Conn* c, const char* d, size_t n) {
    while (n) {
        ssize_t w = io_write(c, d, n);
        if (w <= 0)
            return -1;
        d += w;
        n -= (size_t) w;
    }
    return 0;
}

/* 锚定的标准响应头查找：仅当头名位于行首时匹配，避免把网关转发的
 * 厂商前缀头（如 litellm 的 "llm_provider-transfer-encoding"）误判为
 * 标准头。headerEnd 为头块结束位置（"\r\n\r\n" 起点）。 */
static const char* header_line_value(const char* resp, const char* headerEnd, const char* name) {
    size_t nameLen = strlen(name);
    const char* line = resp;
    while (line < headerEnd) {
        const char* eol = memchr(line, '\r', (size_t) (headerEnd - line));
        if (!eol || eol + 1 >= headerEnd || eol[1] != '\n')
            break;
        if ((size_t) (eol - line) > nameLen && !strncasecmp(line, name, nameLen) &&
            line[nameLen] == ':') {
            const char* v = line + nameLen + 1;
            while (v < eol && (*v == ' ' || *v == '\t'))
                v++;
            return v;
        }
        line = eol + 2;
    }
    return NULL;
}

/* 头块内标准头 name 的值是否包含 needle（大小写不敏感，限定本行）。 */
static int header_value_contains(const char* resp,
                                 const char* headerEnd,
                                 const char* name,
                                 const char* needle) {
    const char* v = header_line_value(resp, headerEnd, name);
    if (!v)
        return 0;
    const char* eol = memchr(v, '\r', (size_t) (headerEnd - v));
    size_t len = eol ? (size_t) (eol - v) : (size_t) (headerEnd - v);
    size_t nl = strlen(needle);
    if (len < nl)
        return 0;
    for (size_t i = 0; i + nl <= len; i++)
        if (!strncasecmp(v + i, needle, nl))
            return 1;
    return 0;
}

/* 解析标准 Content-Length 头（限定头块内）；成功返回 1 并写 *out。 */
static int header_content_length(const char* resp,
                                 const char* headerEnd,
                                 unsigned long long* out) {
    const char* v = header_line_value(resp, headerEnd, "Content-Length");
    if (!v)
        return 0;
    errno = 0;
    char* end = NULL;
    unsigned long long value = strtoull(v, &end, 10);
    if (errno || end == v)
        return 0;
    *out = value;
    return 1;
}
static int decode_chunked(const char* in, size_t n, char** out) {
    size_t pos = 0, cap = n + 1, len = 0;
    char* b = malloc(cap);
    if (!b)
        return -1;
    while (pos < n) {
        const char* e = strstr(in + pos, "\r\n");
        if (!e) {
            free(b);
            return -1;
        }
        char s[32];
        size_t sn = (size_t) (e - (in + pos));
        if (sn >= sizeof s) {
            free(b);
            return -1;
        }
        memcpy(s, in + pos, sn);
        s[sn] = 0;
        char* semi = strchr(s, ';');
        if (semi)
            *semi = 0;
        char* end;
        unsigned long chunk = strtoul(s, &end, 16);
        if (end == s) {
            free(b);
            return -1;
        }
        pos = (size_t) (e - in) + 2;
        if (!chunk)
            break;
        if (chunk > n - pos || len + chunk >= cap) {
            free(b);
            return -1;
        }
        memcpy(b + len, in + pos, chunk);
        len += chunk;
        pos += chunk;
        if (pos + 2 > n || in[pos] != '\r' || in[pos + 1] != '\n') {
            free(b);
            return -1;
        }
        pos += 2;
    }
    b[len] = 0;
    *out = b;
    return 0;
}
int LlmOpenSslTransportPost(const LiteCrabHttpRequest* request, char** out) {
    if (!request || !request->host || !request->path || !request->apiKey || !request->body ||
        request->timeoutMs <= 0 || !out)
        return -204;
    const char* body = request->body;
    int64_t deadlineMs = monotonic_ms() + request->timeoutMs;
    Conn c;
    int rc = conn_open(&c, request, deadlineMs);
    if (rc)
        return rc;
    size_t hz = strlen(body) + strlen(request->apiKey) + strlen(request->host) + strlen(request->path) + 512;
    char* req = malloc(hz);
    if (!req) {
        conn_close(&c);
        return -200;
    }
    int rn =
        snprintf(req,
                 hz,
                 "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nAuthorization: "
                 "Bearer %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                 request->path,
                 request->host,
                 request->apiKey,
                 strlen(body),
                 body);
    if (rn < 0 || (size_t) rn >= hz || send_all(&c, req, (size_t) rn)) {
        free(req);
        conn_close(&c);
        return -201;
    }
    free(req);
    size_t cap = 65536, n = 0;
    char* resp = malloc(cap);
    if (!resp) {
        conn_close(&c);
        return -200;
    }
    for (;;) {
        if (request->isCancelled && request->isCancelled(request->cancelContext)) {
            free(resp);
            conn_close(&c);
            return -205;
        }
        if (!transport_remaining_ms(deadlineMs)) {
            free(resp);
            conn_close(&c);
            return -205;
        }
        if (cap - n < 8192) {
            if (cap >= LLM_WIRE_RESPONSE_MAX) {
                free(resp);
                conn_close(&c);
                return -206;
            }
            cap *= 2;
            if (cap > LLM_WIRE_RESPONSE_MAX)
                cap = LLM_WIRE_RESPONSE_MAX;
            char* q = realloc(resp, cap);
            if (!q) {
                free(resp);
                conn_close(&c);
                return -200;
            }
            resp = q;
        }
        ssize_t got = io_read(&c, resp + n, cap - n - 1);
        if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (got <= 0)
            break;
        n += (size_t) got;
        resp[n] = 0;
        char* headerEnd = strstr(resp, "\r\n\r\n");
        if (!headerEnd && n > LLM_HTTP_HEADER_MAX) {
            free(resp);
            conn_close(&c);
            return -206;
        }
        if (headerEnd) {
            size_t headerBytes = (size_t) (headerEnd + 4 - resp);
            if (headerBytes > LLM_HTTP_HEADER_MAX) {
                free(resp);
                conn_close(&c);
                return -206;
            }
            unsigned long long declared = 0;
            if (header_content_length(resp, headerEnd, &declared) &&
                declared > LLM_WIRE_RESPONSE_MAX - headerBytes) {
                free(resp);
                conn_close(&c);
                return -206;
            }
        }
    }
    conn_close(&c);
    resp[n] = 0;
    char* sep = strstr(resp, "\r\n\r\n");
    if (!sep) {
        free(resp);
        return -203;
    }
    int status = 0;
    sscanf(resp, "HTTP/%*s %d", &status);
    if (status < 200 || status >= 300) {
        free(resp);
        if (status == 413) return -206;
        if (status == 429 || status == 503) return -208;
        if (status == 500 || status == 502 || status == 504) return -209;
        return -207;
    }
    char* payload = sep + 4;
    size_t available = n - (size_t) (payload - resp);
    if (header_value_contains(resp, sep, "Transfer-Encoding", "chunked")) {
        char* decoded = NULL;
        if (decode_chunked(payload, available, &decoded)) {
            free(resp);
            return -203;
        }
        free(resp);
        *out = decoded;
        return 0;
    }
    size_t wanted = available;
    unsigned long long declared = 0;
    if (header_content_length(resp, sep, &declared)) {
        if (declared > SIZE_MAX || declared > available) {
            free(resp);
            return -203;
        }
        wanted = (size_t) declared;
    }
    char* copy = malloc(wanted + 1);
    if (!copy) {
        free(resp);
        return -200;
    }
    memcpy(copy, payload, wanted);
    copy[wanted] = 0;
    free(resp);
    *out = copy;
    return 0;
}
