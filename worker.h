#pragma once
/*
 * worker.h — Persistent session workers (keep-alive across repeats)
 */

#include "types.h"

/*
 * Run n_repeats bursts over pre-connected keep-alive sockets.
 * Returns the number of bursts where response divergence was detected.
 */
int run_session(shared_t *sh, int n_repeats);
