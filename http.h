#pragma once
/*
 * http.h — HTTP response parsing (Content-Length, chunked, EOF drain)
 */

#include "types.h"
#include "conn.h"

/*
 * Read a complete HTTP response from conn, parse the status code,
 * and return the full raw response as a heap-allocated string.
 * Caller must free(). Priority order:
 *   A. Content-Length present       → read exactly N body bytes
 *   B. Transfer-Encoding: chunked   → drain to terminal "0\r\n\r\n"
 *   C. Connection: close / HTTP/1.0 → drain to EOF
 *   D. Fallback (204, empty body)   → return what we have
 */
char *recv_http_response(conn_t *c, int *out_status);

/* Return pointer to the body portion of a raw HTTP response (after \r\n\r\n). */
const char *extract_body(const char *response);
