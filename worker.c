/*
 * worker.c — Persistent workers, dual barriers, staged templates, keep-alive
 */

#include "worker.h"
#include "conn.h"
#include "http.h"
#include "output.h"
#include "request_build.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#  include <sched.h>
#endif

#ifdef WITH_TLS
#  include <openssl/err.h>
#endif

static void make_uuid(char out[37], int tid, int burst_r) {
    FILE           *f = fopen("/dev/urandom", "rb");
    unsigned char   b[16];

    if (!f || fread(b, 1, 16, f) != 16) {
        if (f) fclose(f);
        uint64_t x = (uint64_t)(unsigned)tid * 0xD6E8FEB866B58C7DULL
                   ^ (uint64_t)(unsigned)burst_r * 0x9E3779B97F4A7C15ULL
                   ^ (uint64_t)(uintptr_t)pthread_self();
        for (int i = 0; i < 16; i++)
            b[i] = (unsigned char)(x >> ((i & 7) * 8));
    } else {
        fclose(f);
    }

    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void park_after_failure(thread_arg_t *ta) {
    shared_t *sh = ta->shared;
    for (int r = 0; r < sh->n_repeats; r++) {
        pthread_barrier_wait(ta->barrier_start);
        pthread_barrier_wait(ta->barrier_end);
    }
}

static void *worker_session(void *arg) {
    thread_arg_t *ta = arg;
    shared_t     *sh = ta->shared;
    int           id = ta->id;

#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#endif

    conn_t conn;
    conn.fd = -1;
#ifdef WITH_TLS
    conn.ssl = NULL;
#endif

    conn.fd = connect_tcp(sh->host, sh->port_str);
    if (conn.fd < 0) {
        sh->errors[id] = 1;
        park_after_failure(ta);
        return NULL;
    }

#ifdef WITH_TLS
    if (sh->use_tls) {
        conn.ssl = SSL_new(sh->ssl_ctx);
        if (!conn.ssl) {
            sh->errors[id] = 1;
            close(conn.fd);
            conn.fd = -1;
            park_after_failure(ta);
            return NULL;
        }
        SSL_set_fd(conn.ssl, conn.fd);
        SSL_set_tlsext_host_name(conn.ssl, sh->host);

        if (SSL_connect(conn.ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            sh->errors[id] = 1;
            conn_close(&conn);
            park_after_failure(ta);
            return NULL;
        }
    }
#endif

    int force_pool = (sh->n_repeats > 1);

    for (int r = 0; r < sh->n_repeats; r++) {
        char   uuid[40];
        char  *staged;
        size_t staged_len = 0;

        sh->errors[id] = 0;

        make_uuid(uuid, id, r);
        staged = build_staged_request(sh->raw_request, sh->request_len,
                                      id, uuid, force_pool, &staged_len);
        if (!staged) {
            sh->errors[id] = 1;
            pthread_barrier_wait(ta->barrier_start);
            pthread_barrier_wait(ta->barrier_end);
            continue;
        }

        pthread_barrier_wait(ta->barrier_start);

        get_mono_time(&sh->send_timestamps[id]);

        int    write_ok = 1;
        size_t sent     = 0;

        while (sent < staged_len) {
            ssize_t w = conn_write(&conn, staged + sent, staged_len - sent);
            if (w <= 0) {
                write_ok = 0;
                sh->errors[id] = 1;
                break;
            }
            sent += (size_t)w;
        }

        free(staged);

        int   status = 0;
        char *resp   = NULL;

        if (write_ok)
            resp = recv_http_response(&conn, &status);

        if (write_ok && !resp)
            sh->errors[id] = 1;

        pthread_mutex_lock(&sh->results_mutex);
        free(sh->response_bodies[id]);
        sh->response_bodies[id] = resp;
        sh->status_codes[id]    = status;
        pthread_mutex_unlock(&sh->results_mutex);

        pthread_barrier_wait(ta->barrier_end);
    }

    conn_close(&conn);
    return NULL;
}

int run_session(shared_t *sh, int n_repeats) {
    pthread_barrier_t barrier_start;
    pthread_barrier_t barrier_end;
    int               total_hits = 0;

    if (n_repeats < 1)
        n_repeats = 1;

    sh->n_repeats = n_repeats;

    pthread_barrier_init(&barrier_start, NULL, (unsigned)sh->n_threads + 1);
    pthread_barrier_init(&barrier_end, NULL, (unsigned)sh->n_threads + 1);

    pthread_t    *threads = calloc((size_t)sh->n_threads, sizeof(pthread_t));
    thread_arg_t *args    = calloc((size_t)sh->n_threads, sizeof(thread_arg_t));

    if (!threads || !args) {
        free(threads);
        free(args);
        pthread_barrier_destroy(&barrier_start);
        pthread_barrier_destroy(&barrier_end);
        return 0;
    }

    for (int i = 0; i < sh->n_threads; i++) {
        args[i].shared        = sh;
        args[i].id            = i;
        args[i].barrier_start = &barrier_start;
        args[i].barrier_end   = &barrier_end;
        pthread_create(&threads[i], NULL, worker_session, &args[i]);
    }

    for (int r = 0; r < n_repeats; r++) {
        pthread_barrier_wait(&barrier_start);

        struct timespec t0;
        get_mono_time(&t0);

        pthread_barrier_wait(&barrier_end);

        if (!sh->json_output)
            printf(COL_BOLD "\n▶ Burst %d / %d\n" COL_RESET, r + 1, n_repeats);

        total_hits += diff_responses(sh, &t0, r + 1, n_repeats);
    }

    for (int i = 0; i < sh->n_threads; i++)
        pthread_join(threads[i], NULL);

    pthread_barrier_destroy(&barrier_start);
    pthread_barrier_destroy(&barrier_end);

    free(threads);
    free(args);
    return total_hits;
}
