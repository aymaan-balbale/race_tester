/*
 * http.c — HTTP response parsing
 */

#include "http.h"
#include <string.h>
#include <stdlib.h>

char *recv_http_response(conn_t *c, int *out_status) {
    char   *buf  = malloc(RECV_BUF_SIZE);
    size_t  used = 0;
    ssize_t n;
    if (!buf) return NULL;

    /* Accumulate until the full header block (\r\n\r\n) is received */
    const char *hdr_end = NULL;
    while (used < RECV_BUF_SIZE - 1) {
        n = conn_read(c, buf + used, RECV_BUF_SIZE - 1 - used);
        if (n <= 0) break;
        used += (size_t)n;
        buf[used] = '\0';
        if ((hdr_end = strstr(buf, "\r\n\r\n")) != NULL) break;
    }
    buf[used] = '\0';

    /* Parse status code from "HTTP/x.x NNN ..." */
    *out_status = 0;
    if (used > 12 && strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) *out_status = atoi(sp + 1);
    }
    if (!hdr_end) return buf;

    size_t hdr_bytes = (size_t)(hdr_end - buf) + 4;  /* +4 for \r\n\r\n */

    /* A: Content-Length — read exactly N body bytes */
    const char *cl = strcasestr(buf, "\r\nContent-Length:");
    if (cl) {
        cl = strchr(cl + 2, ':') + 1;
        while (*cl == ' ') cl++;
        size_t clen = (size_t)atol(cl);
        size_t got  = used - hdr_bytes;
        while (got < clen && used < RECV_BUF_SIZE - 1) {
            n = conn_read(c, buf + used, RECV_BUF_SIZE - 1 - used);
            if (n <= 0) break;
            used += (size_t)n;
            got  += (size_t)n;
        }
        buf[used] = '\0';
        return buf;
    }

    /* B: Transfer-Encoding: chunked — drain to terminal "0\r\n\r\n" */
    const char *te = strcasestr(buf, "\r\nTransfer-Encoding:");
    if (te && strcasestr(te, "chunked")) {
        while (used < RECV_BUF_SIZE - 1) {
            n = conn_read(c, buf + used, RECV_BUF_SIZE - 1 - used);
            if (n <= 0) break;
            used += (size_t)n;
            buf[used] = '\0';
            if (strstr(buf + hdr_bytes, "\r\n0\r\n\r\n")) break;
        }
        buf[used] = '\0';
        return buf;
    }

    /* C: Connection: close or HTTP/1.0 — drain to EOF */
    if (strncmp(buf, "HTTP/1.0", 8) == 0 ||
        strcasestr(buf, "\r\nConnection: close") != NULL) {
        while (used < RECV_BUF_SIZE - 1) {
            n = conn_read(c, buf + used, RECV_BUF_SIZE - 1 - used);
            if (n <= 0) break;
            used += (size_t)n;
        }
    }

    /* D: Fallback — return what we have (handles 204 No Content, etc.) */
    buf[used] = '\0';
    return buf;
}

const char *extract_body(const char *response) {
    const char *sep = strstr(response, "\r\n\r\n");
    return sep ? sep + 4 : response;
}
