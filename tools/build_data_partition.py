#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def run(cmd):
    subprocess.run(cmd, check=True)


def write_asset(image_path, source_path, logical_lba, max_sectors):
    with open(source_path, "rb") as handle:
        blob = handle.read()

    sectors = (len(blob) + 511) // 512
    if sectors == 0:
        return 0, 0
    if logical_lba < 0 or logical_lba + sectors > max_sectors:
        raise SystemExit(f"asset does not fit in raw data image: {source_path}")

    with open(image_path, "r+b") as image:
        image.seek(logical_lba * 512)
        image.write(blob)
        padding = (sectors * 512) - len(blob)
        if padding > 0:
            image.write(b"\0" * padding)

    return sectors, len(blob)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--image-total-sectors", type=int, required=True)
    parser.add_argument("--directory-lba", type=int, required=True)
    parser.add_argument("--directory-sectors", type=int, required=True)
    parser.add_argument("--app-area-sectors", type=int, required=True)
    parser.add_argument("--persist-sectors", type=int, required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--asset", action="append", default=[])
    parser.add_argument("apps", nargs="*")
    args = parser.parse_args()

    image_size = args.image_total_sectors * 512
    with open(args.image, "wb") as image:
        image.truncate(image_size)

    run(
        [
            sys.executable,
            os.path.join(os.path.dirname(__file__), "build_appfs.py"),
            "--image",
            args.image,
            "--partition-base-lba",
            "0",
            "--directory-lba",
            str(args.directory_lba),
            "--directory-sectors",
            str(args.directory_sectors),
            "--app-area-sectors",
            str(args.app_area_sectors),
            *args.apps,
        ]
    )

    manifest_lines = [
        "# vibeOS legacy raw data partition",
        f"total_sectors={args.image_total_sectors}",
        f"appfs_directory_lba={args.directory_lba}",
        f"appfs_directory_sectors={args.directory_sectors}",
        f"appfs_app_area_sectors={args.app_area_sectors}",
        f"persist_start_lba={args.directory_lba + args.directory_sectors + args.app_area_sectors}",
        f"persist_sectors={args.persist_sectors}",
    ]

    for spec in args.asset:
        source_path, separator, rest = spec.partition(":")
        logical_lba_text, separator2, label = rest.partition(":")

        if separator == "" or separator2 == "" or not source_path or not logical_lba_text or not label:
            raise SystemExit(f"invalid --asset spec: {spec!r}")

        logical_lba = int(logical_lba_text, 0)
        sectors, size = write_asset(args.image, source_path, logical_lba, args.image_total_sectors)
        manifest_lines.append(
            f"asset {label} lba={logical_lba} sectors={sectors} bytes={size} src={source_path}"
        )

    if args.manifest:
        with open(args.manifest, "w", encoding="utf-8") as handle:
            handle.write("\n".join(manifest_lines) + "\n")


if __name__ == "__main__":
    main()
