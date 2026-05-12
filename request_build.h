#pragma once
/*
 * request_build.h — Template expansion ({{THREAD_ID}}, {{UUID}}), Content-Length,
 *                   optional Connection: keep-alive for pooled sessions.
 */

#include <stddef.h>

/*
 * Expand placeholders in the full raw request template, fix Content-Length to
 * match the body byte length after expansion, and optionally rewrite
 * Connection: close → keep-alive (for HTTP keep-alive across bursts).
 *
 * Returns a newly malloc'd buffer and sets *out_len (exact wire length).
 * Returns NULL on allocation / parse failure.
 */
char *build_staged_request(const char *template,
                           size_t      template_len,
                           int         thread_id,
                           const char *uuid_str,
                           int         force_keep_alive,
                           size_t     *out_len);
