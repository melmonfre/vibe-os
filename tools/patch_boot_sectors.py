#!/usr/bin/env python3
import argparse
from pathlib import Path


SECTOR_SIZE = 512
FAT32_BPB_END = 90
MBR_PARTITION_TABLE_OFFSET = 446


def read_exact(path: Path, size: int) -> bytes:
    data = path.read_bytes()
    if len(data) != size:
        raise SystemExit(f"{path} must be exactly {size} bytes, got {len(data)}")
    return data


def write_at(handle, offset: int, data: bytes) -> None:
    handle.seek(offset)
    handle.write(data)


def patch_mbr(handle, mbr_path: Path, dry_run: bool) -> int:
    custom = read_exact(mbr_path, SECTOR_SIZE)
    handle.seek(0)
    current = bytearray(handle.read(SECTOR_SIZE))
    if len(current) != SECTOR_SIZE:
        raise SystemExit("target is smaller than one sector")

    current[0:MBR_PARTITION_TABLE_OFFSET] = custom[0:MBR_PARTITION_TABLE_OFFSET]
    current[510:512] = custom[510:512]
    if not dry_run:
        write_at(handle, 0, current)
    return 1


def patch_vbr(handle, vbr_path: Path, boot_partition_start_lba: int, dry_run: bool) -> int:
    custom = read_exact(vbr_path, SECTOR_SIZE)
    offset = boot_partition_start_lba * SECTOR_SIZE

    handle.seek(offset)
    current = bytearray(handle.read(SECTOR_SIZE))
    if len(current) != SECTOR_SIZE:
        raise SystemExit("target is smaller than boot partition VBR sector")

    current[0:3] = custom[0:3]
    current[FAT32_BPB_END:510] = custom[FAT32_BPB_END:510]
    current[510:512] = custom[510:512]
    if not dry_run:
        write_at(handle, offset, current)
    return 1


def patch_stage2(handle,
                 stage2_path: Path,
                 boot_partition_start_lba: int,
                 boot_stage2_start_sector: int,
                 boot_kernel_start_sector: int,
                 dry_run: bool) -> int:
    stage2 = stage2_path.read_bytes()
    slot_sectors = boot_kernel_start_sector - boot_stage2_start_sector
    if slot_sectors <= 0:
        raise SystemExit("boot kernel sector must be after boot stage2 sector")

    slot_bytes = slot_sectors * SECTOR_SIZE
    if len(stage2) > slot_bytes:
        raise SystemExit(
            f"stage2 is too large for reserved slot ({len(stage2)} > {slot_bytes} bytes)"
        )

    offset = (boot_partition_start_lba + boot_stage2_start_sector) * SECTOR_SIZE
    payload = stage2 + (b"\0" * (slot_bytes - len(stage2)))
    if not dry_run:
        write_at(handle, offset, payload)
    return slot_sectors


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch only the BIOS boot sectors on an existing raw image/device."
    )
    parser.add_argument("--target", required=True, help="raw image or block device")
    parser.add_argument("--mbr", help="optional replacement MBR boot sector")
    parser.add_argument("--vbr", help="optional replacement FAT32 VBR boot sector")
    parser.add_argument("--stage2", help="optional replacement stage2 binary")
    parser.add_argument("--boot-partition-start-lba", type=int, default=2048)
    parser.add_argument("--boot-stage2-start-sector", type=int, default=8)
    parser.add_argument("--boot-kernel-start-sector", type=int, default=32)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not args.mbr and not args.vbr and not args.stage2:
        raise SystemExit("nothing to patch; pass at least one of --mbr, --vbr, or --stage2")

    target = Path(args.target)
    if not target.exists():
        raise SystemExit(f"target not found: {target}")

    sectors_written = 0
    with target.open("r+b") as handle:
        if args.mbr:
            sectors_written += patch_mbr(handle, Path(args.mbr), args.dry_run)
        if args.vbr:
            sectors_written += patch_vbr(
                handle,
                Path(args.vbr),
                args.boot_partition_start_lba,
                args.dry_run,
            )
        if args.stage2:
            sectors_written += patch_stage2(
                handle,
                Path(args.stage2),
                args.boot_partition_start_lba,
                args.boot_stage2_start_sector,
                args.boot_kernel_start_sector,
                args.dry_run,
            )

        if not args.dry_run:
            handle.flush()

    bytes_written = sectors_written * SECTOR_SIZE
    action = "would write" if args.dry_run else "wrote"
    print(f"{action} {sectors_written} sectors ({bytes_written} bytes) to {target}")
    if args.stage2 and not args.vbr:
        print("warning: patching stage2 without VBR is only safe if stage2 sector count did not change")


if __name__ == "__main__":
    main()
