#pragma once
/*
 * sha256.h — Hex-encoded SHA-256 digests (portable, no OpenSSL required).
 */

#include <stddef.h>

/* Writes 64 hex chars + terminating NUL into out_hex (65 bytes). */
void sha256_hex_digest(const unsigned char *data, size_t len, char out_hex[65]);
