#include "fsfs.h"
#include <string.h>
#include <openssl/sha.h>
void fsfs_sha256(void *d, size_t n, uint8_t out[32]) {
  if (!d || n == 0) {
    memset(out, 0, 32);
    return;
  }
  SHA256((unsigned char *)d, n, out);
}
