#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
extern uint32_t fsfs_crc32c_asm(const void *p, size_t n);
static uint32_t g_tab[256];
static int g_init = 0;
static void lazy_init(void) {
  int i, j;
  uint32_t c;
  if (g_init) return;
  for (i = 0; i < 256; i++) {
    c = (uint32_t)i;
    for (j = 0; j < 8; j++) {
      if (c & 1) c = 0x82F63B78U ^ (c >> 1);
      else c >>= 1;
    }
    g_tab[i] = c;
  }
  g_init = 1;
}
uint32_t fsfs_crc32c_fast(const void *p, size_t n) {
  if (!p && n) return 0;
  return fsfs_crc32c_asm(p, n);
}
uint32_t fsfs_crc32c(void *p, size_t n) {
  uint8_t *b = (uint8_t *)p;
  uint32_t c = 0xFFFFFFFFU;
  size_t k = 0;
  if (!p && n) return 0;
  if (n >= 64) return fsfs_crc32c_asm(p, n);
  lazy_init();
  for (k = 0; k < n; k++)
    c = g_tab[(c ^ b[k]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFU;
}
uint32_t fsfs_crc32c2(void *a, size_t na, void *b, size_t nb) {
  uint32_t c = 0xFFFFFFFFU;
  size_t k;
  uint8_t *pa = (uint8_t *)a;
  uint8_t *pb = (uint8_t *)b;
  lazy_init();
  for (k = 0; k < na; k++) c = g_tab[(c ^ pa[k]) & 0xFF] ^ (c >> 8);
  for (k = 0; k < nb; k++)
    c = g_tab[(c ^ pb[k]) & 0xFF] ^ (c >> 8);
  c ^= 0xFFFFFFFFU;
  return c;
}
