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
        self.qemu_binary = qemu_binary
        self.image_path = image_path
        self.memory_mb = memory_mb
        self.proc = None
        self._spawn()
        self._wait_for_monitor()

    def _spawn(self) -> None:
        self.proc = subprocess.Popen(
            [
                self.qemu_binary,
                *QEMU_OPTS,
                "-m", str(self.memory_mb),
                "-drive", f"format=raw,file={self.image_path}",
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

    def restart(self) -> None:
        """Cold-restart the guest: with -no-reboot, HMP system_reset would exit
        the process, and the stage2 boot menu is the only place the rescue
        selection can be made. Relaunch a fresh QEMU and truncate the serial
        log so the caller's marker checks only observe the new boot."""
        self.close()
        try:
            self.serial_log.unlink()
        except FileNotFoundError:
            pass
        self._spawn()
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


def scenario_boot_rescue_shell(session: QemuSession, attempts: int = 4, window_s: float = 25.0) -> None:
    """Drive the stage2 boot menu to RESCUE SHELL with closed-loop feedback.

    The old selector fired `s,s,ret` three times on blind fixed delays and
    then waited 60s for markers that could never arrive if all attempts
    landed outside the menu window (the flake that made this harness — and
    audio/abi-compat — unreliable as gates). Now: keep pulsing the selection
    until the KERNEL confirms the choice in the serial log (`boot mode:
    rescue shell`); if the boot instead confirms a WRONG mode (`boot mode:
    safe mode`, or the autoboot desktop launching), cold-restart QEMU for a
    fresh menu window and try again. Each burst is `s,s,<ret>` because the
    menu maps the S-key scancode to move-down and RESCUE is two rows below
    the default entry (see boot/stage2.asm classify path).
    """
    for attempt in range(attempts):
        log = session.read_log()
        if "boot mode: rescue shell" in log and "shell: ready" in log:
            return
        deadline = time.time() + window_s
        while time.time() < deadline:
            session.send_key("s", pause=0.12)
            session.send_key("s", pause=0.12)
            session.send_key("ret", pause=0.25)
            log = session.read_log()
            if "boot mode: rescue shell" in log:
                break
            if ("boot mode: safe mode" in log or
                    "desktop.app: launch startx" in log or
                    "host: argv startx" in log):
                break  # wrong entry confirmed; restart for a fresh menu
            time.sleep(1.0)
        log = session.read_log()
        if "boot mode: rescue shell" in log:
            session.wait_for_all(["shell: ready"], timeout=240.0)
            return
        session.restart()
    raise RuntimeError(
        "Timed out selecting rescue shell: 'boot mode: rescue shell' never "
        f"appeared after {attempts} menu attempts (with cold restarts)")


def run_command_expect(session: QemuSession, command: str, markers: List[str], timeout: float = 60.0) -> None:
    """Typed command + COMPLETION gate.

    Two bugs fixed here. (1) The old gate waited for `shell: ready`, which
    the shell prints at the TOP of every loop iteration — before it even
    reads the line — so a command that never returned still passed as soon
    as its echo marker appeared. (2) The shell EXITS its loop on any rc!=0
    (userland/modules/shell.c), so a failing command silently ends all later
    markers. The gate now requires the shell's own exit marker
    (`shell: command exit <cmd> rc=0`), which proves the command actually
    ran to completion with success, and only then the prompt return. And
    60s is honest patience: launching the app host behind netmgrd/netctl
    (task create + AppFS load + service round-trips) is slow under TCG.
    """
    start = len(session.read_log())
    command_marker = command.split(" ", 1)[0]
    session.type_text(command)
    session.send_key("ret", pause=0.15)
    wait_for_all_since(session,
                       [f"shell: command {command_marker}",
                        f"shell: command exit {command_marker} rc=0",
                        "shell: ready",
                        *markers],
                       timeout=timeout,
                       start_offset=start)


def validate_network_surface(qemu_binary: str, image_path: Path, memory_mb: int, report_path: Path) -> bool:
    commands = [
        ("netmgrd status", ["backend:", "datapath executor:", "socket rx capacity:"]),
        ("netmgrd export-state /runtime/netmgrd-surface.txt", ["state exported to /runtime/netmgrd-surface.txt"]),
        ("netmgrd monitor-events 3", ["event[0]: type=", "event[1]: type=", "event[2]: type="]),
        ("netmgrd dhcp", ["dhcp", "dns"]),
        ("netmgrd dns", ["dns "]),
        ("netctl status", ["packet path:", "event stream:", "socket rx capacity:"]),
        ("netctl events 3", ["event[0]: type=", "event[1]: type=", "event[2]: type="]),
        ("netctl ifconfig", ["state:", "ip:"]),
        ("netctl route", ["default via"]),
        ("netctl dhcp", ["connected", "dns:"]),
        ("netctl dns", ["dns "]),
        ("ifconfig", ["status:"]),
        ("route", ["Routing tables"]),
        ("netstat", ["netstat:", "sockets:", "backend:"]),
        ("ping localhost", ["1 packets transmitted", "1 packets received"]),
        ("ping 10.0.2.2", ["bytes from 10.0.2.2", "1 packets transmitted"]),
        ("host localhost", ["localhost", "127.0.0.1"]),
        ("dig localhost", ["localhost", "127.0.0.1"]),
        ("curl file:///runtime/netmgrd-surface.txt", ["manager=", "lease_state="]),
        ("curl file:///runtime/netmgrd-status.txt", ["state="]),
        ("ftp example.com", ["ftp transport unavailable"]),
        ("netctl socket-smoke", ["socket smoke", "ping"]),
    ]
    lines: List[str] = ["# Network Surface Validation", ""]

    with tempfile.TemporaryDirectory(prefix="vibe-network-surface-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)
        session = QemuSession(qemu_binary, scenario_image, memory_mb, workspace)
        try:
            scenario_boot_rescue_shell(session)
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
