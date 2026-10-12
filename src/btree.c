#include "fsfs.h"
#include <string.h>
int fsfs_btree_insert(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t child, uint8_t type) {
  fsfs_inode_t *d;
  uint8_t *base;
  int cap;
  int n;
  int i;
  int pos;
  fsfs_dirent_t e;
  fsfs_dirent_t *arr;
  if (!m || !name) return -1;
  if (dino >= FSFS_MAX_INODES) return -1;
  d = &m->inodes[dino];
  if (d->ino == 0 || !d->is_dir) return -1;
  base = d->inline_buf;
  cap = FSFS_INLINE_SZ / sizeof(fsfs_dirent_t);
  n = d->size / sizeof(fsfs_dirent_t);
  if (n >= cap)
    return -1;
  arr = (fsfs_dirent_t *)base;
  for (i = 0; i < n; i++) {
    if (strncmp(arr[i].name, name, FSFS_NAME_MAX) == 0) return -1;
  }
  pos = n;
  for (i = 0; i < n; i++) {
    if (strcmp(arr[i].name, name) > 0) { pos = i; break; }
  }
  memset(&e, 0, sizeof(e));
  e.ino = child;
  e.parent = dino;
  e.type = type;
  e.nlen = (uint8_t)strlen(name);
  strncpy(e.name, name, FSFS_NAME_MAX - 1);
  for (i = n; i > pos; i--) arr[i] = arr[i - 1];
  arr[pos] = e;
  d->size += sizeof(fsfs_dirent_t);
  d->mtime = (uint64_t)time(0);
  d->seq++;
  d->crc_meta = fsfs_crc32c(d, 128);
  return 0;
}
int fsfs_btree_remove(fsfs_mount_t *m, uint64_t dino, char *name) {
  fsfs_inode_t *d;
  fsfs_dirent_t *arr;
  int n;
  int i;
  int f = -1;
  if (!m || !name) return -1;
  if (dino >= FSFS_MAX_INODES) return -1;
  d = &m->inodes[dino];
  if (d->ino == 0 || !d->is_dir) return -1;
  arr = (fsfs_dirent_t *)d->inline_buf;
  n = d->size / sizeof(fsfs_dirent_t);
  for (i = 0; i < n; i++) {
    if (strncmp(arr[i].name, name, FSFS_NAME_MAX) == 0) { f = i; break; }
  }
  if (f < 0) return -1;
  for (i = f; i < n - 1; i++) arr[i] = arr[i + 1];
  memset(&arr[n - 1], 0, sizeof(fsfs_dirent_t));
  d->size -= sizeof(fsfs_dirent_t);
  d->seq++;
  return 0;
}
int fsfs_btree_lookup(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t *child, uint8_t *type) {
  fsfs_inode_t *d;
  fsfs_dirent_t *arr;
  int n;
  int lo = 0;
  int hi = 0;
  int i;
  if (!m || !name) return -1;
  if (dino >= FSFS_MAX_INODES) return -1;
  d = &m->inodes[dino];
  if (d->ino == 0 || !d->is_dir) return -1;
  arr = (fsfs_dirent_t *)d->inline_buf;
  n = d->size / sizeof(fsfs_dirent_t);
  hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(arr[mid].name, name);
    if (c == 0) {
      if (child) *child = arr[mid].ino;
      if (type) *type = arr[mid].type;
      return 0;
    }
    if (c < 0) lo = mid + 1;
    else hi = mid - 1;
  }
  for (i = 0; i < n; i++) {
    if (strncmp(arr[i].name, name, FSFS_NAME_MAX) == 0) {
      if (child) *child = arr[i].ino;
      if (type) *type = arr[i].type;
      return 0;
    }
  }
  return -1;
}
int fsfs_btree_list(fsfs_mount_t *m, uint64_t dino, fsfs_dirent_t *out, int max, int *got) {
  fsfs_inode_t *d;
  int n;
  int i;
  if (!m || !out || !got) return -1;
  if (dino >= FSFS_MAX_INODES) return -1;
  d = &m->inodes[dino];
  if (d->ino == 0 || !d->is_dir) return -1;
  n = d->size / sizeof(fsfs_dirent_t);
  if (n > max) n = max;
  for (i = 0; i < n; i++) out[i] = ((fsfs_dirent_t *)d->inline_buf)[i];
  *got = n;
  return 0;
}
