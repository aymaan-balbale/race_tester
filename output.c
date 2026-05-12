/*
 * output.c — Coloured diff output or JSON records per burst
 */

#include "output.h"
#include "http.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

int diff_responses(shared_t *sh, struct timespec *t0,
                   int burst_idx, int n_repeats) {
    int         divergence = 0;
    const char *baseline   = sh->response_bodies[0]
                             ? extract_body(sh->response_bodies[0]) : "";

    if (sh->json_output) {
        long long t0_ns = (long long)t0->tv_sec * 1000000000LL
                        + (long long)t0->tv_nsec;

        printf("{\"burst\":%d,\"repeat_total\":%d,\"t0_ns\":%lld,\"threads\":[",
               burst_idx, n_repeats, t0_ns);

        for (int i = 0; i < sh->n_threads; i++) {
            long long d = timespec_diff_ns(t0, &sh->send_timestamps[i]);

            if (i)
                printf(",");

            if (sh->errors[i] || !sh->response_bodies[i]) {
                printf("{\"id\":%d,\"delta_ns\":%lld,\"status\":0,"
                       "\"body_sha256\":null,\"error\":true}",
                       i, d);
                continue;
            }

            const char *body = extract_body(sh->response_bodies[i]);
            size_t      blen  = strlen(body);
            char        hex[65];

            sha256_hex_digest((const unsigned char *)body, blen, hex);

            int diff_b = (i > 0) && (strcmp(body, baseline) != 0);
            if (diff_b)
                divergence = 1;

            printf("{\"id\":%d,\"delta_ns\":%lld,\"status\":%d,"
                   "\"body_sha256\":\"%s\",\"error\":false,\"differs_from_baseline\":%s}",
                   i, d, sh->status_codes[i], hex,
                   diff_b ? "true" : "false");
        }

        printf("],\"divergence\":%s}\n",
               divergence ? "true" : "false");
        fflush(stdout);
        return divergence;
    }

    printf("\n" COL_BOLD "── Results ─────────────────────────────────────"
           COL_RESET "\n");

    for (int i = 0; i < sh->n_threads; i++) {
        long long d = timespec_diff_ns(t0, &sh->send_timestamps[i]);

        if (sh->errors[i] || !sh->response_bodies[i]) {
            printf(COL_RED "[%2d] ERROR  Δ%+9lld ns  (connect/send failed)\n"
                   COL_RESET, i, d);
            continue;
        }

        const char *body   = extract_body(sh->response_bodies[i]);
        int         status = sh->status_codes[i];
        int         diff   = (i > 0) && (strcmp(body, baseline) != 0);
        if (diff) divergence = 1;

        printf("%s[%2d] HTTP %d  Δ%+9lld ns  %s\n" COL_RESET,
               diff ? COL_RED : COL_GREEN,
               i, status, d,
               diff ? "DIFFERS ← potential race!" : "matches baseline");
    }

    if (divergence) {
        printf("\n" COL_BOLD COL_YELLOW
               "── Body Diff (baseline vs. first divergent) ────"
               COL_RESET "\n");
        printf(COL_GREEN "  [0] %s\n" COL_RESET, baseline);
        for (int i = 1; i < sh->n_threads; i++) {
            if (!sh->response_bodies[i]) continue;
            const char *body = extract_body(sh->response_bodies[i]);
            if (strcmp(body, baseline) != 0) {
                printf(COL_RED "  [%d] %s\n" COL_RESET, i, body);
                break;
            }
        }
    }

    return divergence;
}
