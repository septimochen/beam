// Pairing workers can run concurrently in one application. Protect the PSA
// global key registry and entropy state with the library's pthread backend.
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
