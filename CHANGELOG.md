# Changelog

## 0.2.1 - 2026-10-12

Patch for correctness. The transaction journal now records a txn id on every record and writes a commit marker when a txn finishes. Replay reads the journal in two passes and only applies operations that belong to a committed transaction, so a crash during an in progress transaction no longer applies half of it. Snapshots now save a full copy of the inode table when created, restore that copy on restore instead of only adjusting generations, and delete frees exactly the blocks that snapshot owned using the saved copy rather than the live table. The copy on write path decompresses compressed extents fully, fails on a decompress error instead of copying compressed bytes as raw, and clears the compression flag since the new block holds uncompressed data. Read and write paths now propagate block read, decompress, and write errors instead of zero filling or continuing, and the inline to extent conversion path checks its write result. Large 4K stack buffers were moved to the heap in the read, write, extent, copy on write, and scrub paths to keep kernel stack use low, and the super block now frees all per mount allocations and snapshot copies on unmount.

## 0.2 - 2026-10-11

Core sources including the header, btree, compression, checksums, extents, image handling, inodes, journal, reflink, scrub, snapshots, space accounting, and transactions were updated. The FUSE mount sources and C++ helpers were removed and replaced with new kernel sources. Build layout also changed: output now goes to build, kernel objects land in build/obj, the module and tools land in build. The source tree stays clean after a build. Kbuild bookkeeping files are ignored.

## 0.1 - 2026-10-11

Initial release.

The first build includes the core filesystem. CoW storage keeps old versions of data alive. Snapshots allow the whole tree to be frozen and later restored. A history list tracks snapshots in order. Block checksums and background verification detect silent corruption. File clones are created by sharing data instead of copying it. Transactions allow several writes to be committed together or not at all. Space accounting reports how much storage is used directly and how much is shared between snapshots. The disk is divided into zones for metadata, data, the journal, and snapshots. Each zone keeps a spare copy of the superblock at the end. The mount path uses FUSE and accepts the device before the mount point.

