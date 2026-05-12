/*
 * request_build.c — Template expansion + Content-Length / Connection rewriting
 */

#include "request_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOK_THREAD "{{THREAD_ID}}"
#define TOK_THREAD_LEN (sizeof(TOK_THREAD) - 1)
#define TOK_UUID "{{UUID}}"
#define TOK_UUID_LEN (sizeof(TOK_UUID) - 1)

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} buf_t;

static int buf_reserve(buf_t *b, size_t extra) {
    size_t need = b->len + extra;
    if (need <= b->cap) return 0;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < need) ncap *= 2;
    char *p = realloc(b->data, ncap);
    if (!p) return -1;
    b->data = p;
    b->cap  = ncap;
    return 0;
}

static int buf_append_bytes(buf_t *b, const void *src, size_t n) {
    if (buf_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

static int buf_append_cstr(buf_t *b, const char *s) {
    return buf_append_bytes(b, s, strlen(s));
}

static char *expand_tokens(const char *tpl,
                           size_t      tpl_len,
                           int         thread_id,
                           const char *uuid_str,
                           size_t     *out_len) {
    char tidbuf[32];
    snprintf(tidbuf, sizeof tidbuf, "%d", thread_id);

    buf_t b = {0};
    const char *p   = tpl;
    const char *end = tpl + tpl_len;

    while (p < end) {
        if ((size_t)(end - p) >= TOK_THREAD_LEN &&
            memcmp(p, TOK_THREAD, TOK_THREAD_LEN) == 0) {
            if (buf_append_cstr(&b, tidbuf) != 0) goto fail;
            p += TOK_THREAD_LEN;
            continue;
        }
        if ((size_t)(end - p) >= TOK_UUID_LEN &&
            memcmp(p, TOK_UUID, TOK_UUID_LEN) == 0) {
            if (buf_append_cstr(&b, uuid_str) != 0) goto fail;
            p += TOK_UUID_LEN;
            continue;
        }
        if (buf_reserve(&b, 1) != 0) goto fail;
        b.data[b.len++] = *p++;
    }

    if (buf_reserve(&b, 1) != 0) goto fail;
    b.data[b.len] = '\0';
    *out_len = b.len;
    return b.data;

fail:
    free(b.data);
    return NULL;
}

/* Locate "\r\n\r\n"; sets *sep_off to index of first '\r' of that sequence. */
static int find_crlfcrlf(const char *s, size_t len, size_t *sep_off) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (s[i] == '\r' && s[i + 1] == '\n' &&
            s[i + 2] == '\r' && s[i + 3] == '\n') {
            *sep_off = i;
            return 0;
        }
    }
    return -1;
}

static int line_name_is(const char *line, size_t line_len, const char *name_colon) {
    size_t n = strlen(name_colon);
    if (line_len < n) return 0;
    return strncasecmp(line, name_colon, n) == 0;
}

/*
 * Re-stream header lines from expanded[0 .. sep_off), rewriting
 * Content-Length and optionally Connection.
 */
static char *assemble_request(const char *expanded,
                              size_t      sep_off,
                              size_t      body_byte_len,
                              const char *body_ptr,
                              int         force_keep_alive,
                              size_t     *final_len) {
    const char *hdr_start = expanded;
    const char *hdr_end   = expanded + sep_off;

    buf_t out = {0};
    int   saw_cl   = 0;
    int   saw_conn = 0;

    const char *p = hdr_start;
    while (p < hdr_end) {
        const char *nl = memchr(p, '\n', (size_t)(hdr_end - p));
        if (!nl) {
            /* No newline before separator — copy rest as one line */
            size_t rest = (size_t)(hdr_end - p);
            if (rest) {
                if (buf_append_bytes(&out, p, rest) != 0 ||
                    buf_append_cstr(&out, "\r\n") != 0)
                    goto fail;
            }
            break;
        }

        size_t line_len = (size_t)(nl - p);
        if (line_len && nl[-1] == '\r')
            line_len--;

        int skip = 0;

        if (line_name_is(p, line_len, "Content-Length:")) {
            char tmp[96];
            int nw = snprintf(tmp, sizeof tmp,
                              "Content-Length: %zu\r\n", body_byte_len);
            if (nw < 0 || (size_t)nw >= sizeof tmp) goto fail;
            if (buf_append_bytes(&out, tmp, (size_t)nw) != 0) goto fail;
            saw_cl = 1;
            skip   = 1;
        } else if (force_keep_alive && line_name_is(p, line_len, "Connection:")) {
            if (buf_append_cstr(&out, "Connection: keep-alive\r\n") != 0) goto fail;
            saw_conn = 1;
            skip     = 1;
        }

        if (!skip) {
            if (buf_append_bytes(&out, p, line_len) != 0 ||
                buf_append_cstr(&out, "\r\n") != 0)
                goto fail;
        }

        p = nl + 1;
    }

    if (force_keep_alive && !saw_conn) {
        if (buf_append_cstr(&out, "Connection: keep-alive\r\n") != 0) goto fail;
    }

    if (!saw_cl && body_byte_len > 0) {
        char tmp[96];
        int nw = snprintf(tmp, sizeof tmp,
                          "Content-Length: %zu\r\n", body_byte_len);
        if (nw < 0 || (size_t)nw >= sizeof tmp) goto fail;
        if (buf_append_bytes(&out, tmp, (size_t)nw) != 0) goto fail;
    }

    if (buf_append_cstr(&out, "\r\n") != 0 ||
        buf_append_bytes(&out, body_ptr, body_byte_len) != 0)
        goto fail;

    *final_len = out.len;
    return out.data;

fail:
    free(out.data);
    return NULL;
}

char *build_staged_request(const char *template,
                           size_t      template_len,
                           int         thread_id,
                           const char *uuid_str,
                           int         force_keep_alive,
                           size_t     *out_len) {
    if (!uuid_str) uuid_str = "";

    size_t exp_len = 0;
    char  *expanded = expand_tokens(template, template_len,
                                    thread_id, uuid_str, &exp_len);
    if (!expanded) return NULL;

    size_t sep_off = 0;
    if (find_crlfcrlf(expanded, exp_len, &sep_off) != 0) {
        free(expanded);
        return NULL;
    }

    const char *body_ptr = expanded + sep_off + 4;
    size_t      body_len = exp_len - (sep_off + 4);

    char *final = assemble_request(expanded, sep_off, body_len, body_ptr,
                                   force_keep_alive, out_len);
    free(expanded);
    return final;
}
