#pragma once
/*
 * conn.h — TCP connect + conn_t I/O abstraction (plain + TLS)
 */

#include "types.h"
#include <sys/types.h>
#include <sys/socket.h>

/* Open a TCP connection to host:port. Returns fd or -1 on failure. */
int connect_tcp(const char *host, const char *port);

/* Unified read/write/close that work on both plain and TLS connections. */
ssize_t conn_read (conn_t *c, void       *buf, size_t len);
ssize_t conn_write(conn_t *c, const void *buf, size_t len);
void    conn_close(conn_t *c);
