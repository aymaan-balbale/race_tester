#pragma once
/*
 * types.h — Shared types, constants, and compile-time tunables
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#ifdef WITH_TLS
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#endif

/* ── tunables ────────────────────────────────────────────────────────── */
#define DEFAULT_THREADS   10
#define DEFAULT_REPEATS    1
#define DEFAULT_PORT      80
#define RECV_BUF_SIZE  32768

/* ── ANSI colours ────────────────────────────────────────────────────── */
#define COL_RED     "\033[0;31m"
#define COL_GREEN   "\033[0;32m"
#define COL_YELLOW  "\033[0;33m"
#define COL_CYAN    "\033[0;36m"
#define COL_BOLD    "\033[1m"
#define COL_RESET   "\033[0m"

/* ── Connection wrapper (plain fd or TLS) ────────────────────────────── */
typedef struct {
    int fd;
#ifdef WITH_TLS
    SSL *ssl;
#endif
} conn_t;

/* ── Shared state passed to every worker thread ──────────────────────── */
typedef struct {
    /* config — read-only after init */
    const char *host;
    const char *port_str;
    const char *raw_request;
    size_t      request_len;
    int         n_threads;
    int         n_repeats;
    int         use_tls;
    int         json_output;

#ifdef WITH_TLS
    SSL_CTX *ssl_ctx;   /* one context shared across all threads         */
#endif

    /* per-thread results */
    pthread_mutex_t   results_mutex;
    char            **response_bodies;   /* [n_threads] heap strings      */
    int              *status_codes;      /* [n_threads]                   */
    struct timespec  *send_timestamps;   /* [n_threads]                   */
    int              *errors;            /* [n_threads]                   */
} shared_t;

/* ── Per-thread argument ─────────────────────────────────────────────── */
typedef struct {
    shared_t           *shared;
    int                 id;
    pthread_barrier_t  *barrier_start;
    pthread_barrier_t  *barrier_end;
} thread_arg_t;

/* ── Timing helpers ──────────────────────────────────────────────────── */
static inline void get_mono_time(struct timespec *ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static inline long long timespec_diff_ns(const struct timespec *a,
                                         const struct timespec *b) {
    return ((long long)(b->tv_sec  - a->tv_sec)  * 1000000000LL)
         +  (long long)(b->tv_nsec - a->tv_nsec);
}
