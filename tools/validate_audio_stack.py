#!/usr/bin/env python3
import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional


@dataclass
class AudioScenarioResult:
    passed: bool
    log: str
    missing_markers: List[str]
    error: str = ""


class QemuMonitorSession:
    def __init__(self,
                 qemu_binary: str,
                 image_path: Path,
                 memory_mb: int,
                 workspace: Path,
                 machine: Optional[str],
                 audio_devices: List[str]):
        self.serial_log = workspace / "serial.log"
        self.monitor_socket = workspace / "monitor.sock"
        qemu_cmd = [
            qemu_binary,
            "-m",
            str(memory_mb),
            "-drive",
            f"format=raw,file={image_path}",
            "-boot",
            "c",
            "-display",
            "none",
            "-serial",
            f"file:{self.serial_log}",
            "-monitor",
            f"unix:{self.monitor_socket},server,nowait",
        ]
        if machine:
            qemu_cmd.extend(["-machine", machine])
        for device in audio_devices:
            qemu_cmd.extend(["-device", device])
        qemu_cmd.extend(
            [
                "-no-reboot",
                "-no-shutdown",
            ]
        )
        self.proc = subprocess.Popen(
            qemu_cmd,
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
        while time.time() < deadline and pending:
            log = self.read_log()
            pending = [marker for marker in pending if marker not in log]
            if not pending:
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("Timed out waiting for markers: " + ", ".join(pending))

    def send_key(self, key: str, pause: float = 0.08) -> None:
        self.hmp(f"sendkey {key}")
        time.sleep(pause)

    def type_text(self, text: str, pause: float = 0.08) -> None:
        key_map = {
            " ": "spc",
            "\n": "ret",
            "-": "minus",
            ".": "dot",
            "/": "slash",
            "_": "shift-minus",
        }
        for ch in text:
            self.send_key(key_map.get(ch, ch.lower()), pause=pause)


def run_audio_capture_smoke(qemu_binary: str,
                            image_path: Path,
                            memory_mb: int,
                            machine: Optional[str],
                            audio_devices: List[str],
                            expected_backend: str,
                            require_capture: bool,
                            record_ms: int,
                            verify_capture_playback: bool,
                            verify_playback_path: str,
                            require_path_programmed: bool,
                            require_hardware_diag: bool,
                            require_desktop_startup_sound: bool,
                            require_boot_startup_sound: bool) -> AudioScenarioResult:
    backend_marker = f"audiosvc: backend={expected_backend}"
    required_markers = [
        "desktop.app: launch startx",
        "desktop: session start",
        "audiosvc: export ok",
        backend_marker,
        "audiosvc: transport=",
        "audiosvc: codecready-quirk=",
        "audiosvc: multichannel=",
        "audiosvc: status ok",
        "desktop: open-new w=0 t=1 i=0",
        "runtime: ",
        "soundctl: transport=",
        "soundctl: codecready-quirk=",
        "soundctl: multichannel=",
    ]
    if require_capture:
        required_markers.extend(
            [
                "audiosvc: capture-dma=",
                "soundctl: record begin",
                "soundctl: record ok",
                "soundctl: capture-dma=",
                "service: storage request type=17",
            ]
        )
        if verify_capture_playback:
            required_markers.extend(
                [
                    "soundctl: play begin",
                    "soundctl: play ok",
                ]
            )
    elif verify_playback_path:
        required_markers.extend(
            [
                "soundctl: play begin",
                "soundctl: play ok",
            ]
        )
    if require_path_programmed:
        required_markers.extend(
            [
                "audiosvc: path-programmed=1",
                "soundctl: path-programmed=1",
            ]
        )
    if require_hardware_diag:
        required_markers.extend(
            [
                "audiosvc: pci=",
                "audiosvc: codec=",
                "audiosvc: route=",
                "soundctl: pci=",
                "soundctl: codec=",
                "soundctl: route=",
            ]
        )
    if require_desktop_startup_sound:
        required_markers.extend(
            [
                "desktop: startup sound begin",
                "audio: begin desktop-session",
                "audio: started desktop-session",
                "audio: done desktop-session",
                "desktop: startup sound returned",
            ]
        )
    if require_boot_startup_sound:
        required_markers.extend(
            [
                "audio: begin boot",
                "audio: started boot",
                "audio: done boot",
                "init: boot sound returned",
            ]
        )

    with tempfile.TemporaryDirectory(prefix="vibe-audio-smoke-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        session = QemuMonitorSession(qemu_binary, scenario_image, memory_mb, workspace, machine, audio_devices)
        try:
            if require_boot_startup_sound:
                session.wait_for_all(
                    [
                        "audio: begin boot",
                        "audio: started boot",
                        "audio: done boot",
                        "init: boot sound returned",
                    ],
                    timeout=20.0,
                )
            session.wait_for_all(["desktop.app: launch startx", "desktop: session start"], timeout=45.0)
            if require_desktop_startup_sound:
                session.wait_for_all(
                    [
                        "desktop: startup sound begin",
                        "audio: begin desktop-session",
                        "audio: started desktop-session",
                        "audio: done desktop-session",
                        "desktop: startup sound returned",
                    ],
                    timeout=20.0,
                )
            session.send_key("ctrl-t", pause=0.15)
            session.wait_for_all(["desktop: open-new w=0 t=1 i=0"], timeout=8.0)
            time.sleep(1.0)
            session.type_text("audiosvc status", pause=0.12)
            session.send_key("ret", pause=0.15)
            session.wait_for_all([backend_marker, "audiosvc: status ok"], timeout=15.0)
            session.type_text("soundctl status", pause=0.12)
            session.send_key("ret", pause=0.15)
            session.wait_for_all(["runtime: "], timeout=15.0)
            if require_capture:
                session.type_text(f"soundctl record {record_ms} /capture.wav", pause=0.12)
                session.send_key("ret", pause=0.15)
                session.wait_for_all(["soundctl: record begin", "soundctl: record ok"], timeout=35.0)
                if verify_capture_playback:
                    session.type_text("soundctl play /capture.wav", pause=0.12)
                    session.send_key("ret", pause=0.15)
                    session.wait_for_all(["soundctl: play begin", "soundctl: play ok"], timeout=20.0)
            elif verify_playback_path:
                session.type_text(f"soundctl play {verify_playback_path}", pause=0.12)
                session.send_key("ret", pause=0.15)
                session.wait_for_all(["soundctl: play begin", "soundctl: play ok"], timeout=25.0)
            if require_path_programmed:
                session.type_text("audiosvc status", pause=0.12)
                session.send_key("ret", pause=0.15)
                session.wait_for_all(["audiosvc: path-programmed=1"], timeout=15.0)
                session.type_text("soundctl status", pause=0.12)
                session.send_key("ret", pause=0.15)
                session.wait_for_all(["soundctl: path-programmed=1"], timeout=15.0)
            log = session.read_log()
        except Exception as exc:
            log = session.read_log()
            missing = [marker for marker in required_markers if marker not in log]
            return AudioScenarioResult(False, log, missing, str(exc))
        finally:
            session.close()

    missing = [marker for marker in required_markers if marker not in log]
    return AudioScenarioResult(not missing, log, missing, "")


def write_report(report_path: Path, result: AudioScenarioResult) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Audio Stack Validation Report",
        "",
        f"- Result: {'PASS' if result.passed else 'FAIL'}",
    ]
    if result.error:
        lines.append(f"- Error: {result.error}")
    if result.missing_markers:
        lines.append("- Missing markers: " + ", ".join(result.missing_markers))
    lines.extend(
        [
            "",
            "## Serial Log",
            "",
            "```text",
            result.log.rstrip(),
            "```",
            "",
        ]
    )
    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate VibeOS audio scenarios under QEMU")
    parser.add_argument("--image", required=True, help="path to boot.img")
    parser.add_argument("--report", required=True, help="path to markdown report")
    parser.add_argument("--qemu", default="qemu-system-i386", help="QEMU binary")
    parser.add_argument("--memory-mb", type=int, default=3072, help="guest memory in MiB")
    parser.add_argument("--machine", default="", help="optional QEMU machine type")
    parser.add_argument("--audio-device",
                        action="append",
                        dest="audio_devices",
                        help="QEMU -device entry for audio emulation; may be passed multiple times")
    parser.add_argument("--expect-backend",
                        default="compat-ac97",
                        help="expected backend marker reported by audiosvc")
    parser.add_argument("--skip-capture",
                        action="store_true",
                        help="skip soundctl record validation and only verify desktop/audiosvc boot path")
    parser.add_argument("--record-ms",
                        type=int,
                        default=250,
                        help="duration in ms for soundctl record when capture validation is enabled")
    parser.add_argument("--verify-capture-playback",
                        action="store_true",
                        help="after recording /capture.wav, run soundctl play /capture.wav and require playback markers")
    parser.add_argument("--verify-playback-path",
                        default="",
                        help="run soundctl play <path> after soundctl status and require playback markers")
    parser.add_argument("--require-path-programmed",
                        action="store_true",
                        help="after playback, require audiosvc/soundctl to report path-programmed=1")
    parser.add_argument("--require-hardware-diag",
                        action="store_true",
                        help="require audiosvc/soundctl to emit PCI/codec/route hardware markers")
    parser.add_argument("--require-desktop-startup-sound",
                        action="store_true",
                        help="require the delayed desktop startup WAV path to run after desktop session start")
    parser.add_argument("--require-boot-startup-sound",
                        action="store_true",
                        help="require the boot WAV path to run during bootstrap before startx")
    args = parser.parse_args()

    audio_devices = args.audio_devices if args.audio_devices else ["AC97"]
    machine = args.machine if args.machine else None
    result = run_audio_capture_smoke(args.qemu,
                                     Path(args.image),
                                     args.memory_mb,
                                     machine,
                                     audio_devices,
                                     args.expect_backend,
                                     not args.skip_capture,
                                     args.record_ms,
                                     args.verify_capture_playback,
                                     args.verify_playback_path,
                                     args.require_path_programmed,
                                     args.require_hardware_diag,
                                     args.require_desktop_startup_sound,
                                     args.require_boot_startup_sound)
    report_path = Path(args.report)
    write_report(report_path, result)
    if not result.passed:
        print(f"audio-stack: validation failed; see {report_path}", file=sys.stderr)
        return 1

    print(f"audio-stack: validation passed; see {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
