# TOCTOU Race Condition Tester

A high-performance, multithreaded C CLI designed to exploit Time-of-Check to Time-of-Use (TOCTOU) vulnerabilities in web APIs. 

Standard race condition scripts written in Python or Go suffer from interpreter locks, garbage collection overhead, and OS scheduler jitter. This tool is built in C to guarantee exact microsecond synchronization across threads.

## Features

* **Silicon-Level Synchronization:** Uses `pthread_barrier_t` and CPU core affinity (`pthread_setaffinity_np`) to park all worker threads on a single core, dropping the gate so all requests hit the network interface in the exact same scheduler timeslice.
* **TLS Pre-Handshaking:** Performs RSA/ECDH cryptographic handshakes *before* the synchronization barrier. Threads wait with fully established, encrypted tunnels to eliminate asymmetric crypto overhead from the race window.
* **Idempotency Bypass:** Features a zero-allocation template engine. Inject `{{UUID}}` or `{{THREAD_ID}}` into your JSON payload, and the tool dynamically expands them per-thread, recalculating the `Content-Length` on the fly to bypass WAF replay protections.
* **Session Pooling (Keep-Alive):** Uses dual-barrier synchronization to maintain warm TCP/TLS sockets across multiple burst repeats, preventing connection exhaustion and IP blacklisting from Cloudflare/load balancers.
* **Machine-Readable Output:** Supports `--json` for automated CI/CD pipelines, outputting SHA-256 hashes of response bodies to cryptographically prove state divergence.

## Build

Requires OpenSSL development headers for HTTPS support.

```bash
sudo apt install libssl-dev
make clean
make tls
```

# Usage
1. Capture your target API request (raw HTTP) and save it as request.txt.

2. Use {{UUID}} for values that must be unique per request (e.g., nonces, transaction IDs).
```
POST /api/v1/withdraw HTTP/1.1
Host: api.target.com
Content-Type: application/json
X-Idempotency-Key: {{UUID}}

{"amount": 10}
```
3. Fire the payload:
```
# 20 concurrent threads, repeated 5 times over HTTPS
./race_tester request.txt --threads 20 --repeat 5 --port 443
```
```bash
git add README.md LICENSE
git commit -m "Add README and MIT License"
git push origin main
```
