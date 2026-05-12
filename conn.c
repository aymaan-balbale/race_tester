/*
 * conn.c — TCP connect + conn_t I/O abstraction
 */

#include "conn.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

ssize_t conn_read(conn_t *c, void *buf, size_t len) {
#ifdef WITH_TLS
    if (c->ssl) {
        int r = SSL_read(c->ssl, buf, (int)len);
        if (r <= 0) {
            int e = SSL_get_error(c->ssl, r);
            return (e == SSL_ERROR_ZERO_RETURN) ? 0 : -1;
        }
        return (ssize_t)r;
    }
#endif
    return recv(c->fd, buf, len, 0);
}

ssize_t conn_write(conn_t *c, const void *buf, size_t len) {
#ifdef WITH_TLS
    if (c->ssl) {
        int r = SSL_write(c->ssl, buf, (int)len);
        return (r <= 0) ? -1 : (ssize_t)r;
    }
#endif
    return send(c->fd, buf, len, MSG_NOSIGNAL);
}

void conn_close(conn_t *c) {
#ifdef WITH_TLS
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
#endif
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}
