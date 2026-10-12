# fuckingshitfilesystem

FSFS is a copy-on-write filesystem built on top of FUSE. Instead of overwriting data in place, it copies the blocks that change and keeps the old ones available. This makes snapshots, rollback, and history cheap to keep around. The goal is to make storage behavior predictable.

## Build

Run make to compile. The build requires fuse3, zlib, lz4, zstd, openssl, and uuid headers to be present on the system.

Two binaries are produced. The first, mkfs.fsfs, formats a device or an image file. The second, mount.fsfs, mounts a filesystem.

## Usage

To format a disk:

    mkfs.fsfs /dev/sdX

To format an image file for testing:

    mkfs.fsfs /tmp/fsfs.img 128

To mount a device:

    mount.fsfs /dev/sdX /mnt/fsfs

To mount an image file:

    mount.fsfs /tmp/fsfs.img /tmp/mnt

## File layout

The code is organized into a few areas. The header file defines how data is laid out on disk. The mkfs source cuts the image into separate zones for metadata, file data, the journal, and snapshots. The mount source handles startup and FUSE call dispatch. The image source reads and writes blocks. The copy-on-write layer manages shared extents. The btree layer handles directory entries. The inode layer handles file metadata and contents. The snapshot source manages versioned states. The transaction source batches writes into atomic groups. The scrub source verifies data integrity. The journal source replays state after a crash. The reflink source shares file data between clones. The compress source selects compression by data type. The space source tracks how much storage is used and shared. The history source keeps an ordered list of snapshots. The scrub daemon source runs integrity checks in the background.

The code is written in C and C++. The main filesystem code is in C, with small C++ helpers only where needed.

