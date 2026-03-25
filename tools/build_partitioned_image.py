#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import tempfile


def run(cmd):
    subprocess.run(cmd, check=True)


def write_file(path, data, offset=0):
    with open(path, "r+b") as handle:
        handle.seek(offset)
        handle.write(data)


def ensure_mtools_path(part_path, destination, mmd_tool):
    mmd = shutil.which(mmd_tool) or mmd_tool
    normalized = destination.strip()

    if not normalized:
        raise SystemExit("boot file destination cannot be empty")
    normalized = normalized.replace("\\", "/")
    if not normalized.startswith("/"):
        normalized = "/" + normalized

    components = [component for component in normalized.split("/")[:-1] if component]
    current = ""
    for component in components:
        current += "/" + component
        subprocess.run(
            [mmd, "-i", part_path, f"::{current}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def copy_boot_file(part_path, source_path, destination, mcopy_tool, mmd_tool):
    mcopy = shutil.which(mcopy_tool) or mcopy_tool
    normalized = destination.strip().replace("\\", "/")

    if not normalized:
        raise SystemExit("boot file destination cannot be empty")
    if not normalized.startswith("/"):
        normalized = "/" + normalized

    ensure_mtools_path(part_path, normalized, mmd_tool)
    run([mcopy, "-i", part_path, "-o", source_path, f"::{normalized}"])


def mkfs_fat_command(args, part_path):
    tool = shutil.which(args.mkfs_fat) or args.mkfs_fat
    if os.path.basename(tool) == "newfs_msdos":
        return [
            tool,
            "-F",
            "32",
            "-L",
            "VIBEBOOT",
            "-o",
            str(args.boot_partition_start_lba),
            "-r",
            str(args.boot_partition_reserved_sectors),
            part_path,
        ]

    return [
        tool,
        "-F",
        "32",
        "-n",
        "VIBEBOOT",
        "-R",
        str(args.boot_partition_reserved_sectors),
        "-h",
        str(args.boot_partition_start_lba),
        part_path,
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--mkfs-fat", default="mkfs.fat")
    parser.add_argument("--mcopy", default="mcopy")
    parser.add_argument("--mmd", default="mmd")
    parser.add_argument("--mbr", required=True)
    parser.add_argument("--vbr", required=True)
    parser.add_argument("--stage2", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--image-total-sectors", type=int, required=True)
    parser.add_argument("--boot-partition-start-lba", type=int, required=True)
    parser.add_argument("--boot-partition-sectors", type=int, required=True)
    parser.add_argument("--boot-partition-reserved-sectors", type=int, required=True)
    parser.add_argument("--boot-stage2-start-sector", type=int, required=True)
    parser.add_argument("--boot-kernel-start-sector", type=int, required=True)
    parser.add_argument("--data-partition-start-lba", type=int, default=0)
    parser.add_argument("--data-partition-sectors", type=int, default=0)
    parser.add_argument("--data-partition-image")
    parser.add_argument("--boot-file", action="append", default=[])
    args = parser.parse_args()

    image_size = args.image_total_sectors * 512
    with open(args.image, "wb") as image:
        image.truncate(image_size)

    with open(args.mbr, "rb") as handle:
        write_file(args.image, handle.read(), 0)

    with tempfile.TemporaryDirectory() as tmpdir:
        part_path = os.path.join(tmpdir, "bootpart.img")
        with open(part_path, "wb") as part:
            part.truncate(args.boot_partition_sectors * 512)

        run(mkfs_fat_command(args, part_path))

        with open(part_path, "r+b") as part, open(args.vbr, "rb") as vbr_handle:
            vbr = bytearray(part.read(512))
            custom = vbr_handle.read(512)
            # Keep the FAT32 BPB/EBPB fields, but replace the VBR entry jump
            # and boot code with our loader.
            vbr[0:3] = custom[0:3]
            vbr[90:510] = custom[90:510]
            vbr[510:512] = custom[510:512]
            part.seek(0)
            part.write(vbr)

        with open(args.stage2, "rb") as stage2_handle:
            stage2 = stage2_handle.read()
        stage2_sectors = (len(stage2) + 511) // 512
        if stage2_sectors + args.boot_stage2_start_sector > args.boot_kernel_start_sector:
            raise SystemExit("stage2 overlaps reserved kernel slot")

        with open(args.kernel, "rb") as kernel_handle:
            kernel = kernel_handle.read()
        kernel_sectors = (len(kernel) + 511) // 512
        if kernel_sectors + args.boot_kernel_start_sector > args.boot_partition_reserved_sectors:
            raise SystemExit("kernel does not fit inside FAT32 reserved sectors")

        with open(part_path, "r+b") as part:
            part.seek(args.boot_stage2_start_sector * 512)
            part.write(stage2)
            stage2_padding = (stage2_sectors * 512) - len(stage2)
            if stage2_padding > 0:
                part.write(b"\0" * stage2_padding)

            part.seek(args.boot_kernel_start_sector * 512)
            part.write(kernel)
            padding = (kernel_sectors * 512) - len(kernel)
            if padding > 0:
                part.write(b"\0" * padding)

        boot_files = list(args.boot_file)
        if not boot_files:
            boot_files.append(f"{args.kernel}::/KERNEL.BIN")

        for spec in boot_files:
            source_path, separator, destination = spec.partition("::")

            if separator == "" or not source_path or not destination:
                raise SystemExit(f"invalid --boot-file spec: {spec!r}")
            copy_boot_file(part_path, source_path, destination, args.mcopy, args.mmd)

        with open(part_path, "rb") as part:
            write_file(args.image, part.read(), args.boot_partition_start_lba * 512)

    if args.data_partition_image:
        if args.data_partition_start_lba <= 0 or args.data_partition_sectors <= 0:
            raise SystemExit("data partition geometry is required with --data-partition-image")

        with open(args.data_partition_image, "rb") as handle:
            data_partition = handle.read()
        max_size = args.data_partition_sectors * 512
        if len(data_partition) > max_size:
            raise SystemExit("data partition image does not fit inside the target partition")
        write_file(args.image, data_partition, args.data_partition_start_lba * 512)


if __name__ == "__main__":
    main()
