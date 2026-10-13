#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <crypto/sha2.h>
void fsfs_sha256(void *d, size_t n, __u8 out[32]) {
  struct sha256_ctx c;
  if (!d || n == 0) {
    memset(out, 0, 32);
    return;
  }
  sha256_init(&c);
  sha256_update(&c, d, n);
  sha256_final(&c, out);
}
#else
#include <string.h>
#include <openssl/sha.h>
void fsfs_sha256(void *d, size_t n, uint8_t out[32]) {
  if (!d || n == 0) {
    memset(out, 0, 32);
    return;
  }
  SHA256((unsigned char *)d, n, out);
}
#endif
