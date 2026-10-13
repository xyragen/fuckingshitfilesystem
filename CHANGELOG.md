# Changelog

## 0.2 - 2026-10-13

Core sources including the header, btree, compression, checksums, extents, image handling, inodes, journal, reflink, scrub, snapshots, space accounting, and transactions were updated. The FUSE mount sources and C++ helpers were removed and replaced with new kernel sources. Build layout also changed: output now goes to build, kernel objects land in build/obj, the module and tools land in build. The source tree stays clean after a build. Kbuild bookkeeping files are ignored.

## 0.1 - 2026-10-11

Initial release.

The first build includes the core filesystem. CoW storage keeps old versions of data alive. Snapshots allow the whole tree to be frozen and later restored. A history list tracks snapshots in order. Block checksums and background verification detect silent corruption. File clones are created by sharing data instead of copying it. Transactions allow several writes to be committed together or not at all. Space accounting reports how much storage is used directly and how much is shared between snapshots. The disk is divided into zones for metadata, data, the journal, and snapshots. Each zone keeps a spare copy of the superblock at the end. The mount path uses FUSE and accepts the device before the mount point.

