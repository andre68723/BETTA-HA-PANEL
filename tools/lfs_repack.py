#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Copy the files of a LittleFS partition dump into a new LittleFS image of another size.

A partition table change that moves or resizes the `storage` partition leaves the old filesystem unreadable at its new
place, and the first boot formats a fresh one. This carries layout.json, settings.json, themes and graph history over:

  esptool read-flash <old offset> <old size> old.bin           (the old `storage` partition)
  tools/lfs_repack.py old.bin <new size> new.img               (sizes in bytes, 0x... accepted)
  esptool write-flash <new offset> new.img                     (together with the new table and app)

Needs `pip install littlefs-python`. The geometry (4 KiB blocks, 128 byte read/program/lookahead) is what esp_littlefs
uses here; a dump that does not mount with it is refused. Nothing is written to a device.
"""
import argparse
import sys

from littlefs import LittleFS

BLOCK = 4096


def geometry(size):
    return dict(block_size=BLOCK, block_count=size // BLOCK, read_size=128, prog_size=128, cache_size=128,
                lookahead_size=128)


def copy_tree(src, dst, path="/"):
    count = 0
    for name in src.listdir(path):
        child = path.rstrip("/") + "/" + name
        if src.stat(child).type == 2:
            dst.makedirs(child, exist_ok=True)
            count += copy_tree(src, dst, child)
        else:
            with src.open(child, "rb") as f:
                data = f.read()
            with dst.open(child, "wb") as f:
                f.write(data)
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("old_dump", help="dump of the old storage partition")
    parser.add_argument("new_size", type=lambda v: int(v, 0), help="size of the new storage partition in bytes")
    parser.add_argument("out", help="image to write")
    args = parser.parse_args()
    if args.new_size % BLOCK:
        sys.exit("the new size must be a multiple of 4096")
    old = open(args.old_dump, "rb").read()
    try:
        src = LittleFS(mount=False, **geometry(len(old)))
        src.context.buffer[:] = old
        src.mount()
    except Exception as exc:  # littlefs-python raises its own error types
        sys.exit(f"{args.old_dump} does not mount as a LittleFS of {len(old)} bytes: {exc}")
    dst = LittleFS(**geometry(args.new_size))
    count = copy_tree(src, dst)
    open(args.out, "wb").write(bytes(dst.context.buffer))
    print(f"{count} files copied, {args.out}: {args.new_size} bytes")


if __name__ == "__main__":
    main()
