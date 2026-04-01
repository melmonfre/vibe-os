#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List


BOOT_MARKERS = ["userland.app: shell start", "shell: ready"]
QEMU_OPTS = [
    "-machine", "pc",
    "-cpu", "core2duo",
    "-smp", "2,sockets=1,cores=2,threads=1,maxcpus=2",
    "-vga", "std",
    "-rtc", "base=localtime",
    "-usb",
]


class QemuSession:
    def __init__(self, qemu_binary: str, image_path: Path, memory_mb: int, workspace: Path):
        self.serial_log = workspace / "serial.log"
        self.monitor_socket = workspace / "monitor.sock"
        self.proc = subprocess.Popen(
            [
                qemu_binary,
                *QEMU_OPTS,
                "-m", str(memory_mb),
                "-drive", f"format=raw,file={image_path}",
                "-netdev", "user,id=net0",
                "-device", "virtio-net-pci,netdev=net0",
                "-boot", "c",
                "-display", "none",
                "-serial", f"file:{self.serial_log}",
                "-monitor", f"unix:{self.monitor_socket},server,nowait",
                "-no-reboot",
                "-no-shutdown",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._wait_for_monitor()

    def _wait_for_monitor(self) -> None:
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if self.monitor_socket.exists():
                return
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited before monitor socket was created")
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for QEMU monitor socket")

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.hmp("quit", timeout=0.5)
            except Exception:
                pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5.0)

    def hmp(self, command: str, timeout: float = 1.0) -> str:
        chunks: List[bytes] = []
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(timeout)
            client.connect(str(self.monitor_socket))
            try:
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    if b"(qemu)" in chunk:
                        break
            except socket.timeout:
                pass

            client.sendall(command.encode("utf-8") + b"\n")
            if command.strip() == "quit":
                return ""

            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    chunk = client.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
                if b"(qemu)" in chunk:
                    break
        return b"".join(chunks).decode("utf-8", errors="replace")

    def read_log(self) -> str:
        if not self.serial_log.exists():
            return ""
        return self.serial_log.read_text(encoding="utf-8", errors="replace")

    def wait_for_all(self, markers: List[str], timeout: float) -> None:
        deadline = time.time() + timeout
        pending = list(markers)
        while time.time() < deadline:
            log = self.read_log()
            pending = [marker for marker in pending if marker not in log]
            if not pending:
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))

    def type_text(self, text: str, pause: float = 0.06) -> None:
        for ch in text:
            if ch == " ":
                key = "spc"
            elif ch == "/":
                key = "slash"
            elif ch == "-":
                key = "minus"
            elif ch == ".":
                key = "dot"
            elif ch == ":":
                self.hmp("sendkey shift-semicolon")
                time.sleep(pause)
                continue
            else:
                key = ch
            self.hmp(f"sendkey {key}")
            time.sleep(pause)

    def send_key(self, key: str, pause: float = 0.1) -> None:
        self.hmp(f"sendkey {key}")
        time.sleep(pause)


def wait_for_all_since(session: QemuSession, markers: List[str], timeout: float, start_offset: int) -> None:
    deadline = time.time() + timeout
    pending = list(markers)
    while time.time() < deadline:
        log = session.read_log()[start_offset:]
        pending = [marker for marker in pending if marker not in log]
        if not pending:
            return
        if session.proc.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))


def run_command_expect(session: QemuSession, command: str, markers: List[str], timeout: float = 8.0) -> None:
    start = len(session.read_log())
    command_marker = command.split(" ", 1)[0]
    session.type_text(command)
    session.send_key("ret", pause=0.15)
    wait_for_all_since(session,
                       [f"shell: command {command_marker}", "shell: ready", *markers],
                       timeout=timeout,
                       start_offset=start)


def validate_network_surface(qemu_binary: str, image_path: Path, memory_mb: int, report_path: Path) -> bool:
    commands = [
        ("netmgrd status", ["backend:", "datapath executor:"]),
        ("ifconfig", ["status:"]),
        ("route", ["Routing tables"]),
        ("netstat", ["netstat: state="]),
        ("ping localhost", ["1 packets transmitted, 1 packets received", "ping: loopback-ok"]),
        ("host localhost", ["localhost has address 127.0.0.1", "host: loopback-ok"]),
        ("dig localhost", ["localhost.\t0\tIN\tA\t127.0.0.1", "dig: loopback-ok"]),
        ("curl file:///runtime/netmgrd-status.txt", ["state=", "curl: file-ok"]),
        ("ftp example.com", ["ftp: transport unsupported"]),
        ("netctl socket-smoke", ["socket smoke ok: ping"]),
    ]
    lines: List[str] = ["# Network Surface Validation", ""]

    with tempfile.TemporaryDirectory(prefix="vibe-network-surface-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)
        session = QemuSession(qemu_binary, scenario_image, memory_mb, workspace)
        try:
            session.wait_for_all(BOOT_MARKERS, timeout=60.0)
            for command, markers in commands:
                run_command_expect(session, command, markers)
                lines.append(f"- PASS `{command}`")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return True
        except Exception as exc:
            lines.append("")
            lines.append(f"FAIL: {exc}")
            lines.append("")
            lines.append("```")
            lines.append(session.read_log()[-12000:])
            lines.append("```")
            report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return False
        finally:
            session.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeOS network command surface under QEMU")
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=256)
    args = parser.parse_args()

    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    ok = validate_network_surface(args.qemu, Path(args.image), args.memory_mb, report_path)
    if not ok:
        print(f"network-surface: validation failed; see {report_path}", file=sys.stderr)
        return 1
    print(f"network-surface: validation passed; see {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
