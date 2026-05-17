#!/usr/bin/env bash
set -euo pipefail

fat_type=""
label=""
reserved=""
hidden=""
target=""

while (($#)); do
    case "$1" in
        -F)
            fat_type="${2:-}"
            shift 2
            ;;
        -n)
            label="${2:-}"
            shift 2
            ;;
        -R)
            reserved="${2:-}"
            shift 2
            ;;
        -h)
            hidden="${2:-}"
            shift 2
            ;;
        -*)
            echo "mkfs-fat-mtools: unsupported option '$1'" >&2
            exit 2
            ;;
        *)
            if [[ -n "$target" ]]; then
                echo "mkfs-fat-mtools: multiple targets are not supported" >&2
                exit 2
            fi
            target="$1"
            shift
            ;;
    esac
done

if [[ "$fat_type" != "32" ]]; then
    echo "mkfs-fat-mtools: only FAT32 is supported" >&2
    exit 2
fi

if [[ -z "$label" || -z "$reserved" || -z "$hidden" || -z "$target" ]]; then
    echo "mkfs-fat-mtools: missing required mkfs.fat-style arguments" >&2
    exit 2
fi

exec mformat \
    -i "$target" \
    -F \
    -v "$label" \
    -R "$reserved" \
    -H "$hidden" \
    ::
