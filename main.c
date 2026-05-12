/*
 * main.c — Entry point: argument parsing, request loading, burst loop
 *
 * Build (plain HTTP):
 *   gcc -O2 -Wall -pthread -o race_tester \
 *       main.c conn.c http.c worker.c output.c
 *
 * Build (with TLS):
 *   sudo apt install libssl-dev
 *   gcc -O2 -Wall -pthread -DWITH_TLS -o race_tester \
 *       main.c conn.c http.c worker.c output.c -lssl -lcrypto
 *
 * Or just: make / make tls
 *
 * Usage:
 *   ./race_tester request.txt [--threads N] [--repeat N] [--port N] [--tls]
 *                             [--json]
 *
 * Template tokens (expanded per thread before each burst):
 *   {{THREAD_ID}}  decimal worker index
 *   {{UUID}}       fresh RFC-4122-style string per burst & thread
 *
 * request.txt format (CRLF line endings, use printf to generate):
 *   printf 'POST /withdraw HTTP/1.1\r\nHost: localhost:8080\r\n
 *            Content-Type: application/json\r\nConnection: close\r\n
 *            Content-Length: 14\r\n\r\n{"amount": 10}' > request.txt
 */

#include "types.h"
#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Request file loader ─────────────────────────────────────────────── *
 * Reads raw HTTP from disk and extracts the Host: header value for      *
 * use in connect_tcp() and (for TLS) SSL SNI.                           *
 * Returns heap-allocated request string; sets *out_host.                *
 * ────────────────────────────────────────────────────────────────────── */
static char *load_request(const char *path, char **out_host) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nr = fread(buf, 1, (size_t)len, f);
    buf[nr] = '\0';
    fclose(f);

    /* Extract the hostname from "Host: hostname[:port]" */
    *out_host = NULL;
    char *h = strcasestr(buf, "\nHost:");
    if (!h) h = strcasestr(buf, "\r\nHost:");
    if (h) {
        h = strchr(h, ':') + 1;
        while (*h == ' ') h++;
        char *end = h;
        while (*end && *end != '\r' && *end != '\n') end++;
        /* Strip port from "host:port" if present */
        char  *colon = memchr(h, ':', (size_t)(end - h));
        size_t hlen  = colon ? (size_t)(colon - h) : (size_t)(end - h);
        *out_host = strndup(h, hlen);
    }

    return buf;
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <request.txt> [--threads N] [--repeat N]"
            " [--port N] [--tls] [--json]\n", argv[0]);
        return 1;
    }

    int n_threads   = DEFAULT_THREADS;
    int n_repeats   = DEFAULT_REPEATS;
    int port        = DEFAULT_PORT;
    int use_tls     = 0;
    int json_output = 0;

    for (int i = 2; i < argc; i++) {
        if      (!strcmp(argv[i], "--threads") && i+1 < argc) n_threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--repeat")  && i+1 < argc) n_repeats = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--port")    && i+1 < argc) port      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tls"))                    use_tls   = 1;
        else if (!strcmp(argv[i], "--json"))                   json_output = 1;
    }
    if (port == 443) use_tls = 1;   /* auto-enable on standard HTTPS port */

#ifndef WITH_TLS
    if (use_tls) {
        fprintf(stderr,
            "Error: --tls requires recompiling with OpenSSL support.\n"
            "  sudo apt install libssl-dev\n"
            "  make tls\n");
        return 1;
    }
#endif

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    char *host        = NULL;
    char *raw_request = load_request(argv[1], &host);
    if (!raw_request) return 1;
    if (!host) {
        fprintf(stderr, "Error: could not parse Host: header from %s\n", argv[1]);
        free(raw_request);
        return 1;
    }

    if (!json_output) {
        printf(COL_BOLD COL_CYAN
               "┌─────────────────────────────────────────────┐\n"
               "│  TOCTOU Race Condition Tester               │\n"
               "└─────────────────────────────────────────────┘\n"
               COL_RESET);
        printf("  Host    : %s:%s%s\n", host, port_str,
               use_tls ? "  " COL_CYAN "[TLS]" COL_RESET : "");
        printf("  Threads : %d\n", n_threads);
        printf("  Repeats : %d\n\n", n_repeats);
    }

    /* ── Initialise shared state ───────────────────────────────────── */
    shared_t sh = {0};
    sh.host        = host;
    sh.port_str    = port_str;
    sh.raw_request   = raw_request;
    sh.request_len   = strlen(raw_request);
    sh.n_threads     = n_threads;
    sh.use_tls       = use_tls;
    sh.json_output   = json_output;

#ifdef WITH_TLS
    sh.ssl_ctx = NULL;
    if (use_tls) {
        sh.ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!sh.ssl_ctx) {
            ERR_print_errors_fp(stderr);
            free(raw_request); free(host);
            return 1;
        }
        /* VERIFY_NONE for local/dev targets.
         * For authorised prod testing, swap to:
         *   SSL_CTX_set_verify(sh.ssl_ctx, SSL_VERIFY_PEER, NULL);
         *   SSL_CTX_set_default_verify_paths(sh.ssl_ctx);           */
        SSL_CTX_set_verify(sh.ssl_ctx, SSL_VERIFY_NONE, NULL);
        if (!json_output)
            printf(COL_YELLOW
                   "  Note: cert verification disabled (dev mode).\n\n"
                   COL_RESET);
    }
#endif

    sh.response_bodies = calloc((size_t)n_threads, sizeof(char *));
    sh.status_codes    = calloc((size_t)n_threads, sizeof(int));
    sh.send_timestamps = calloc((size_t)n_threads, sizeof(struct timespec));
    sh.errors          = calloc((size_t)n_threads, sizeof(int));
    pthread_mutex_init(&sh.results_mutex, NULL);

    /* ── Session (keep-alive across repeats when --repeat > 1) ─────── */
    int total_hits = run_session(&sh, n_repeats);

    /* ── Final verdict ─────────────────────────────────────────────── */
    if (!json_output) {
        printf("\n" COL_BOLD
               "═══════════════════════════════════════════════\n");
        if (total_hits > 0)
            printf(COL_RED "  ⚠  RACE CONDITION DETECTED in %d / %d burst(s)\n"
                   COL_RESET, total_hits, n_repeats);
        else {
            printf(COL_GREEN "  ✓  No divergence detected across %d burst(s)\n"
                   COL_RESET, n_repeats);
            printf("     (run with --repeat 20+ to increase confidence)\n");
        }
        printf(COL_BOLD "═══════════════════════════════════════════════\n"
               COL_RESET "\n");
    }

    /* ── Cleanup ───────────────────────────────────────────────────── */
    for (int i = 0; i < n_threads; i++) free(sh.response_bodies[i]);
    free(sh.response_bodies);
    free(sh.status_codes);
    free(sh.send_timestamps);
    free(sh.errors);
    pthread_mutex_destroy(&sh.results_mutex);
#ifdef WITH_TLS
    if (sh.ssl_ctx) SSL_CTX_free(sh.ssl_ctx);
#endif
    free(raw_request);
    free(host);

    /* Exit 2 = race found (scriptable in CI: check $? after running)  */
    return total_hits > 0 ? 2 : 0;
}
