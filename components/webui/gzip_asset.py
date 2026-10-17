#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Gzip one web asset for embedding: gzip_asset.py <input> <output>.

Reproducible output (mtime 0, no embedded file name) so an unchanged asset
yields a byte-identical .gz and does not trigger a relink.
"""
import gzip
import os
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
            gz.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
