#!/usr/bin/env python3
"""Non-interactive IPv4 datapath gate.

Unlike tools/validate_network_surface.py (which drives the rescue shell with
HMP sendkeys and is inherently timing-flaky), this validator only needs to
observe boot markers and the wire: it boots the image with a virtio-net NIC on
QEMU user-net, then requires

  * `network: dhcp bound`                      (DHCP offer/ack consumed in RX)
  * `network: icmp gateway roundtrip ok`       (kernel echo to the gateway and
                                                its reply back through RX)

and, when a packet capture is available, an on-wire ARP reply plus an ICMP
echo request/echo reply pair. It is deterministic (no input injection) and is
the acceptance gate for the P3 "rede real" datapath.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Tuple


BOOT_MARKERS = [
    "network: dhcp bound",
    "network: icmp gateway roundtrip ok",
    "network: arp request answered",
    "network: icmp echo-reply sent",
    "network: loopback smoke ok",
    "network: arp cache multi ok",
]

# Peer MAC used by the in-guest responder self-test (10.0.2.77).
SELFTEST_PEER_MAC = "52:54:00:aa:bb:cc"

QEMU_OPTS = [
    "-machine", "pc",
    "-cpu", "core2duo",
    "-vga", "std",
    "-rtc", "base=localtime",
]


def parse_pcap(pcap_path: Path) -> List[Tuple[str, str, int]]:
    """Return (kind, dst_mac, length) tuples for the frames the NIC moved."""
    if not pcap_path.exists():
        return []
    data = pcap_path.read_bytes()
    if len(data) < 24:
        return []
    frames: List[Tuple[str, str, int]] = []
    off = 24
    while off + 16 <= len(data):
        _ts, _tu, incl, _orig = struct.unpack("<IIII", data[off:off + 16])
        off += 16
        pkt = data[off:off + incl]
        off += incl
        if len(pkt) < 14:
            continue
        dst_mac = pkt[0:6].hex(":")
        ethertype = struct.unpack("!H", pkt[12:14])[0]
        if ethertype == 0x0806 and len(pkt) >= 22:
            op = struct.unpack("!H", pkt[20:22])[0]
            frames.append((f"arp-{'reply' if op == 2 else 'request'}", dst_mac, len(pkt)))
        elif ethertype == 0x0800 and len(pkt) >= 35:
            proto = pkt[23]
            if proto == 1:
                frames.append((f"icmp-type{pkt[34]}", dst_mac, len(pkt)))
            elif proto == 17:
                sp, dp = struct.unpack("!HH", pkt[34:38])
                frames.append((f"udp-{sp}-{dp}", dst_mac, len(pkt)))
            else:
                frames.append((f"ip-proto{proto}", dst_mac, len(pkt)))
    return frames


def run(qemu_binary: str, image_path: Path, memory_mb: int, report_path: Path) -> bool:
    lines: List[str] = ["# Network Datapath Validation", ""]
    with tempfile.TemporaryDirectory(prefix="vibe-network-datapath-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        serial_log = workspace / "serial.log"
        pcap_path = workspace / "net.pcap"
        shutil.copyfile(image_path, scenario_image)

        proc = subprocess.Popen(
            [
                qemu_binary,
                *QEMU_OPTS,
                "-m", str(memory_mb),
                "-drive", f"format=raw,file={scenario_image},snapshot=on",
                "-boot", "c",
                "-display", "none",
                "-serial", f"file:{serial_log}",
                "-monitor", "none",
                "-no-reboot",
                "-netdev", "user,id=net0",
                "-device", "virtio-net-pci,netdev=net0",
                "-object", f"filter-dump,id=d0,netdev=net0,file={pcap_path}",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        log = ""
        deadline = time.time() + 90.0
        try:
            while time.time() < deadline:
                if serial_log.exists():
                    log = serial_log.read_text(encoding="utf-8", errors="replace")
                missing = [m for m in BOOT_MARKERS if m not in log]
                if not missing:
                    break
                if proc.poll() is not None:
                    break
                time.sleep(0.2)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5.0)

        if serial_log.exists():
            log = serial_log.read_text(encoding="utf-8", errors="replace")

        missing = [m for m in BOOT_MARKERS if m not in log]
        frames = parse_pcap(pcap_path)
        kinds = [k for k, _dst, _len in frames]

        arp_reply_gateway = any(k == "arp-reply" for k in kinds)
        arp_reply_gateway = any(k == "arp-reply" for k in kinds)
        icmp_req = kinds.count("icmp-type8")
        icmp_rep = kinds.count("icmp-type0")
        selftest_arp = any(k == "arp-reply" and dst == SELFTEST_PEER_MAC for k, dst, _l in frames)
        selftest_icmp = any(k == "icmp-type0" and dst == SELFTEST_PEER_MAC for k, dst, _l in frames)
        # TX-ring proof: the boot bursts ARP-reply + echo-reply back-to-back
        # inside one drain pass; drop=0 means the 8-deep queue never refused a
        # frame the old 1-in-flight design would have silently lost.
        tx_drop = 0
        for line in log.splitlines():
            if "icmp gateway roundtrip ok" in line and "drop=" in line:
                try:
                    tx_drop = int(line.split("drop=")[1].split()[0])
                except (IndexError, ValueError):
                    tx_drop = -1

        lines.append("## Boot markers")
        lines.append("")
        for marker in BOOT_MARKERS:
            lines.append(f"- `{marker}`: {'yes' if marker in log else 'NO'}")
        lines.append(f"- `network: responder self-test sent`: "
                     f"{'yes' if 'network: responder self-test sent' in log else 'NO'}")
        lines.append("")
        lines.append("## Wire (packet capture on netdev)")
        lines.append("")
        lines.append(f"- frames: {len(frames)}")
        lines.append(f"- ARP replies: {kinds.count('arp-reply')}")
        lines.append(f"- ICMP echo requests (type 8): {icmp_req}")
        lines.append(f"- ICMP echo replies (type 0): {icmp_rep}")
        lines.append("")
        lines.append("## Responder reachability (self-test frames to "
                     f"`{SELFTEST_PEER_MAC}`)")
        lines.append("")
        lines.append(f"- ARP reply to self-test peer: {'yes' if selftest_arp else 'NO'}")
        lines.append(f"- ICMP echo reply to self-test peer: {'yes' if selftest_icmp else 'NO'}")
        lines.append(f"- TX ring drops during boot (roundtrip line): {tx_drop} (expected 0)")
        lines.append("")
        for marker in ("dhcp bound", "icmp gateway roundtrip ok", "arp request answered",
                       "icmp echo-reply sent", "responder self-test sent", "loopback smoke ok",
                       "arp cache FAILED", "arp cache multi ok"):
            for entry in log.splitlines():
                if marker in entry:
                    lines.append("```text")
                    lines.append(entry.strip())
                    lines.append("```")
                    break

        # Gateway roundtrip requires our outbound echo (type 8) and a reply
        # (type 0); reachability requires the responder replies to the peer.
        wire_ok = icmp_req >= 1 and icmp_rep >= 1
        responders_ok = selftest_arp and selftest_icmp and tx_drop == 0
        if missing:
            lines.append("")
            lines.append(f"FAIL: timed out waiting for markers: {', '.join(missing)}")
            lines.append("")
            lines.append("```")
            lines.append(log[-12000:])
            lines.append("```")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return False
        if not wire_ok:
            lines.append("")
            lines.append("FAIL: boot markers seen but the wire capture lacks the ICMP echo pair")
            lines.append("")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return False
        if not responders_ok:
            lines.append("")
            lines.append("FAIL: responder self-test / TX ring not clean on the wire")
            lines.append("")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return False

        lines.append("")
        lines.append("PASS: DHCP bound + gateway ICMP roundtrip + inbound ARP/ICMP responders "
                     "confirmed on the wire.")
        report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the VibeOS IPv4 datapath under QEMU user-net")
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    args = parser.parse_args()

    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    if run(args.qemu, Path(args.image), args.memory_mb, report_path):
        print(f"network-datapath: validation passed; see {report_path}")
        return 0
    print(f"network-datapath: validation failed; see {report_path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
