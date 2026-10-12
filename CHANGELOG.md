# Changelog

## 0.1 - 2026-10-11

Initial release.

The first build includes the core filesystem. Copy-on-write storage keeps old versions of data alive. Snapshots allow the whole tree to be frozen and later restored. A history list tracks snapshots in order. Block checksums and background verification detect silent corruption. File clones are created by sharing data instead of copying it. Transactions allow several writes to be committed together or not at all. Space accounting reports how much storage is used directly and how much is shared between snapshots. The disk is divided into zones for metadata, data, the journal, and snapshots. Each zone keeps a spare copy of the superblock at the end. The mount path uses FUSE and accepts the device before the mount point.

