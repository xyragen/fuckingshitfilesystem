#ifndef FSFS_H
#define FSFS_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <uuid/uuid.h>
typedef uint64_t __u64;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint16_t __u16;
typedef uint8_t __u8;
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define FSFS_MAGIC 0x4653465346532142ULL
#define FSFS_VERSION 7
#define FSFS_BLOCK 4096
#define FSFS_MAX_INODES 8192
#define FSFS_MAX_SNAPS 256
#define FSFS_MAX_EXTS 24
#define FSFS_INLINE_SZ 256
#define FSFS_NAME_MAX 224
#define FSFS_JOURNAL_BLOCKS 512
#define FSFS_PART_SLOTS 16
#define FSFS_ZONE_META 1
#define FSFS_ZONE_DATA 2
#define FSFS_ZONE_JOURNAL 3
#define FSFS_ZONE_SNAP 4
#define FSFS_COMP_NONE 0
#define FSFS_COMP_LZ4 1
#define FSFS_COMP_ZSTD 2
#define FSFS_COMP_ZLIB 3
#define FSFS_XMODE_LAT 0
#define FSFS_XMODE_CAP 1
#define FSFS_XMODE_CPU 2
#define FSFS_TXN_MAX_OPS 64
#define FSFS_TXN_ACTIVE 1
#define FSFS_TXN_COMMITTED 2
#define FSFS_TXN_ABORTED 3
#define FSFS_IOC_MAGIC 0xCF
#define FSFS_IOC_SNAP_CREATE _IOWR(FSFS_IOC_MAGIC, 1, struct fsfs_ioc_snap)
#define FSFS_IOC_SNAP_DELETE _IOW(FSFS_IOC_MAGIC, 2, __u64)
#define FSFS_IOC_SNAP_RESTORE _IOW(FSFS_IOC_MAGIC, 3, __u64)
#define FSFS_IOC_SNAP_DIFF _IOWR(FSFS_IOC_MAGIC, 4, struct fsfs_ioc_text)
#define FSFS_IOC_SPACE _IOR(FSFS_IOC_MAGIC, 5, struct fsfs_ioc_space)
#define FSFS_IOC_DEDUP _IO(FSFS_IOC_MAGIC, 6)
#define FSFS_IOC_SCRUB _IO(FSFS_IOC_MAGIC, 7)
#define FSFS_IOC_SCRUB_BAD _IOR(FSFS_IOC_MAGIC, 8, __u64)
#define FSFS_IOC_TXN_BEGIN _IOR(FSFS_IOC_MAGIC, 9, __u64)
#define FSFS_IOC_TXN_COMMIT _IOW(FSFS_IOC_MAGIC, 10, __u64)
#define FSFS_IOC_TXN_ABORT _IOW(FSFS_IOC_MAGIC, 11, __u64)
#define FSFS_IOC_TXN_WRITE _IOW(FSFS_IOC_MAGIC, 12, struct fsfs_ioc_txnwrite)
#define FSFS_IOC_HIST_LIST _IOR(FSFS_IOC_MAGIC, 13, struct fsfs_ioc_text)
#define FSFS_IOC_SPACE_PINNED _IOWR(FSFS_IOC_MAGIC, 14, struct fsfs_ioc_text)
#define FSFS_IOC_SPACE_EXCL _IOWR(FSFS_IOC_MAGIC, 15, struct fsfs_ioc_space)
#define FSFS_IOC_SNAP_EST _IOWR(FSFS_IOC_MAGIC, 16, struct fsfs_ioc_space)
#define FSFS_IOC_REFlink _IOWR(FSFS_IOC_MAGIC, 17, struct fsfs_ioc_space)
struct fsfs_ioc_snap {
  __u64 id;
  __u64 out;
  char label[64];
};
struct fsfs_ioc_space {
  __u64 ino;
  __u64 id;
  __u64 logical;
  __u64 phys;
  __u64 shared;
  __u64 exclusive;
  __s64 est;
};
struct fsfs_ioc_text {
  __u64 a;
  __u64 b;
  __u32 len;
  char buf[4096];
};
struct fsfs_ioc_txnwrite {
  __u64 txn;
  __u64 ino;
  __u64 off;
  __u32 len;
  char data[4000];
};
typedef struct {
  __u64 l_off;
  __u64 p_blk;
  __u32 len;
  __u32 flags;
  __u32 refs;
  __u32 crc;
  __u8 comp;
  __u8 verified;
  __u8 reserved[2];
  __u8 merkle[32];
} fsfs_extent_t;
typedef struct {
  __u64 ino;
  __u32 mode;
  __u32 nlink;
  __u32 uid;
  __u32 gid;
  __u64 size;
  __u64 atime;
  __u64 mtime;
  __u64 ctime;
  __u64 gen;
  __u64 seq;
  __u32 comp_pol;
  __u32 xmode;
  __u32 n_exts;
  __u32 snap_id;
  __u8 inline_used;
  __u8 is_dir;
  __u8 dead;
  __u8 pad1;
  __u32 crc_meta;
  __u8 sha_all[32];
  fsfs_extent_t exts[FSFS_MAX_EXTS];
  __u8 inline_buf[FSFS_INLINE_SZ];
  __u64 quota;
  __u64 reserv;
} fsfs_inode_t;
typedef struct {
  __u64 ino;
  __u64 parent;
  __u8 type;
  __u8 nlen;
  __u8 pad[6];
  char name[FSFS_NAME_MAX];
} fsfs_dirent_t;
typedef struct {
  __u64 id;
  __u64 gen;
  __u64 stamp;
  __u64 root_copy;
  __u64 blocks_used;
  __u64 blocks_shared;
  __u8 active;
  __u8 auto_expire;
  __u8 pad[6];
  char label[64];
  __u8 root_hash[32];
} fsfs_snap_t;

typedef struct {
  __u64 magic;
  __u32 version;
  __u32 block_sz;
  __u64 total_blocks;
  __u64 meta_blocks;
  __u64 data_blocks;
  __u64 journal_blocks;
  __u64 snap_blocks;
  __u64 free_blocks;
  __u64 next_ino;
  __u64 next_gen;
  __u64 journal_head;
  __u64 journal_tail;
  __u64 root_ino;
  __u64 snap_count;
  __u64 scrub_seq;
  __u64 txn_seq;
  __u8 fsuuid[16];
  __u8 volhash[32];
  __u32 crc_super;
  __u32 part_crc;
  __u16 n_parts;
  __u16 flags;
  __u64 part_off[FSFS_PART_SLOTS];
  __u64 part_len[FSFS_PART_SLOTS];
  __u8 part_type[FSFS_PART_SLOTS];
  __u8 part_zone[FSFS_PART_SLOTS];
  __u8 pad[64];
} fsfs_super_t;
typedef struct {
  __u64 seq;
  __u32 op;
  __u32 len;
  __u64 a;
  __u64 b;
  __u8 data[4000];
  __u32 crc;
} fsfs_jrec_t;
typedef struct {
  __u64 id;
  int state;
  int n_ops;
  fsfs_jrec_t ops[FSFS_TXN_MAX_OPS];
  __u64 stamp;
} fsfs_txn_t;
#ifdef __KERNEL__
struct fsfs_sb_info {
  fsfs_super_t super;
  __u8 *blk_bmp;
  __u32 *blk_ref;
  fsfs_inode_t *inodes;
  fsfs_snap_t *snaps;
  __u8 *journal;
  struct mutex lock;
  __u64 cur_gen;
  int scrub_stop;
  __u64 bytes_logical;
  __u64 bytes_phys;
  __u64 bytes_shared;
  __u64 scrub_bad;
  __u64 hist_n;
  __u64 hist[FSFS_MAX_SNAPS];
};
static inline struct fsfs_sb_info *FSFS_SB(struct super_block *sb) {
  return (struct fsfs_sb_info *)sb->s_fs_info;
}
uint32_t fsfs_crc32c(void *p, size_t n);
uint32_t fsfs_crc32c2(void *a, size_t na, void *b, size_t nb);
void fsfs_sha256(void *d, size_t n, __u8 out[32]);
uint32_t fsfs_crc32c_fast(const void *p, size_t n);
int fsfs_blk_read(struct super_block *sb, __u64 blk, void *out);
int fsfs_blk_write(struct super_block *sb, __u64 blk, void *in);
__s64 fsfs_blk_alloc(struct super_block *sb, int zone);
int fsfs_blk_free(struct super_block *sb, __u64 blk);
int fsfs_blk_incref(struct super_block *sb, __u64 blk);
int fsfs_blk_decref(struct super_block *sb, __u64 blk);
int fsfs_compress_pick(__u8 *d, size_t n, int pol);
int fsfs_compress_blk(__u8 *in, size_t il, __u8 *out, size_t os, int algo);
int fsfs_decompress_blk(__u8 *in, size_t il, __u8 *out, size_t os, int algo);
__u64 fsfs_space_logical(struct super_block *sb);
__u64 fsfs_space_phys(struct super_block *sb);
__u64 fsfs_space_shared(struct super_block *sb);
__u64 fsfs_space_exclusive(struct super_block *sb, __u64 ino);
__s64 fsfs_space_snap_delete_estimate(struct super_block *sb, __u64 snapid);
void fsfs_space_why_pinned(struct super_block *sb, __u64 blk, char *out, size_t osz);
int fsfs_extent_alloc_for(struct super_block *sb, __u64 ino, __u64 loff, __u32 len, int comp);
int fsfs_extent_free_inode(struct super_block *sb, __u64 ino);
int fsfs_extent_cow(struct super_block *sb, __u64 ino, __u64 loff);
int fsfs_btree_insert(struct super_block *sb, __u64 dino, char *name, __u64 child, __u8 type);
int fsfs_btree_remove(struct super_block *sb, __u64 dino, char *name);
int fsfs_btree_lookup(struct super_block *sb, __u64 dino, char *name, __u64 *child, __u8 *type);
int fsfs_btree_list(struct super_block *sb, __u64 dino, fsfs_dirent_t *out, int max, int *got);
__u64 fsfs_inode_create(struct super_block *sb, __u32 mode, int is_dir);
int fsfs_inode_read(struct super_block *sb, __u64 ino, __u64 off, void *buf, size_t n);
int fsfs_inode_write(struct super_block *sb, __u64 ino, __u64 off, void *buf, size_t n);
int fsfs_inode_resize(struct super_block *sb, __u64 ino, __u64 sz);
int fsfs_inode_unlink(struct super_block *sb, __u64 ino);
int fsfs_dir_add(struct super_block *sb, __u64 dino, char *name, __u64 c, __u8 t);
int fsfs_dir_del(struct super_block *sb, __u64 dino, char *name);
int fsfs_snap_create(struct super_block *sb, char *label, __u64 *out);
int fsfs_snap_delete(struct super_block *sb, __u64 id);
int fsfs_snap_restore(struct super_block *sb, __u64 id);
int fsfs_snap_diff(struct super_block *sb, __u64 a, __u64 b, char *out, size_t osz);
int fsfs_snap_expire(struct super_block *sb);
int fsfs_txn_begin(struct super_block *sb, fsfs_txn_t *t);
int fsfs_txn_add_write(fsfs_txn_t *t, __u64 ino, __u64 off, void *d, size_t n);
int fsfs_txn_add_create(fsfs_txn_t *t, __u64 dino, char *name, __u32 mode);
int fsfs_txn_commit(struct super_block *sb, fsfs_txn_t *t);
int fsfs_txn_abort(struct super_block *sb, fsfs_txn_t *t);
int fsfs_scrub_block(struct super_block *sb, __u64 blk);
int fsfs_scrub_inode(struct super_block *sb, __u64 ino);
int fsfs_scrub_all(struct super_block *sb);
int fsfs_journal_append(struct super_block *sb, fsfs_jrec_t *r);
int fsfs_journal_replay(struct super_block *sb);
int fsfs_reflink_file(struct super_block *sb, __u64 src, __u64 dst_dir, char *nm, __u64 *newino);
int fsfs_reflink_dedup(struct super_block *sb);
int fsfs_hist_record(struct super_block *sb, __u64 snapid, char *label);
int fsfs_hist_list(struct super_block *sb, char *out, size_t osz);
int fsfs_hist_gc(struct super_block *sb);
struct inode *fsfs_iget(struct super_block *sb, __u64 ino);
void fsfs_fill_inode(struct super_block *sb, struct inode *inode);
int fsfs_load_inodes(struct super_block *sb);
extern const struct inode_operations fsfs_dir_inode_ops;
extern const struct inode_operations fsfs_file_inode_ops;
extern const struct file_operations fsfs_dir_ops;
extern const struct file_operations fsfs_file_ops;
long fsfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int fsfs_getattr(struct mnt_idmap *idmap, const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags);
int fsfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *attr);
#else
typedef struct {
  int fd;
  char path[1024];
  fsfs_super_t super;
  uint8_t *blk_bmp;
  uint32_t *blk_ref;
  fsfs_inode_t *inodes;
  fsfs_snap_t *snaps;
  uint8_t *journal;
  int mounted;
  uint64_t cur_gen;
  int scrub_stop;
  uint64_t bytes_logical;
  uint64_t bytes_phys;
  uint64_t bytes_shared;
} fsfs_mount_t;
uint32_t fsfs_crc32c(void *p, size_t n);
uint32_t fsfs_crc32c2(void *a, size_t na, void *b, size_t nb);
void fsfs_sha256(void *d, size_t n, uint8_t out[32]);
#endif
#ifdef __cplusplus
}
#endif
#endif
