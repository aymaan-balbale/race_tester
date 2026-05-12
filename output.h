#pragma once
/*
 * output.h — Human-readable diff or JSON records per burst
 */

#include "types.h"

/*
 * Compare every thread's response body against thread 0's (baseline).
 * When sh->json_output is set, prints one JSON object for this burst.
 * burst_idx is 1-based; n_repeats is total bursts in the session.
 * Returns 1 if any divergence was detected, 0 otherwise.
 */
int diff_responses(shared_t *sh, struct timespec *t0,
                   int burst_idx, int n_repeats);
