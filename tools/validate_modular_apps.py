#!/usr/bin/env python3
import argparse
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Tuple


BOOT_MARKER = "userland.app: shell start"
SHELL_READY_MARKER = "shell: ready"
DESKTOP_CENTER_X = 320
DESKTOP_CENTER_Y = 240
FILES_ICON_CENTER = (572, 63)
START_BUTTON_CENTER = (35, 469)
START_MENU_TERMINAL_CENTER = (186, 142)


@dataclass
class Scenario:
    name: str
    description: str
    command: Optional[str]
    must_have: List[str]
    boot_markers: Optional[List[str]] = None
    command_marker: Optional[str] = None
    action: Optional[Callable[["QemuSession"], None]] = None


@dataclass
class ScenarioResult:
    scenario: Scenario
    passed: bool
    log: str
    missing_markers: List[str]
    error: Optional[str] = None


def parse_mode_text(mode_text: str) -> Optional[str]:
    parts = mode_text.lower().split("x")
    if len(parts) != 2:
        return None
    try:
        width = int(parts[0], 10)
        height = int(parts[1], 10)
    except ValueError:
        return None
    if width <= 0 or height <= 0:
        return None
    return f"{width}x{height}"


class QemuSession:
    def __init__(self, qemu_binary: str, image_path: Path, memory_mb: int, workspace: Path):
        self.serial_log = workspace / "s.log"
        self.monitor_socket = workspace / "m.sock"
        self.proc = subprocess.Popen(
            [
                qemu_binary,
                "-m",
                str(memory_mb),
                "-drive",
                f"format=raw,file={image_path}",
                "-netdev",
                "user,id=net0",
                "-device",
                "virtio-net-pci,netdev=net0",
                "-boot",
                "c",
                "-display",
                "none",
                "-serial",
                f"file:{self.serial_log}",
                "-monitor",
                f"unix:{self.monitor_socket},server,nowait",
                "-no-reboot",
                "-no-shutdown",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.mouse_x = 0
        self.mouse_y = 0
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
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)

    def read_log(self) -> str:
        if not self.serial_log.exists():
            return ""
        return self.serial_log.read_text(encoding="utf-8", errors="replace")

    def wait_for_log(self, marker: str, timeout: float) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if marker in self.read_log():
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError(f"Timed out waiting for marker: {marker}")

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

    def boot_mode_size(self) -> Optional[Tuple[int, int]]:
        log = self.read_log()
        matches = re.findall(r"video: boot init backend=.* mode=(\d+)x(\d+)x\d+", log)
        if not matches:
            return None
        width_text, height_text = matches[-1]
        return (int(width_text, 10), int(height_text, 10))

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
                return b"".join(chunks).decode("utf-8", errors="replace")

            response_deadline = time.time() + timeout
            while time.time() < response_deadline:
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
            key = key_map.get(ch, ch.lower())
            self.send_key(key, pause=pause)

    def reset_mouse_to_center(self) -> None:
        self.mouse_x = DESKTOP_CENTER_X
        self.mouse_y = DESKTOP_CENTER_Y

    def move_mouse_to(self, x: int, y: int, pause: float = 0.04) -> None:
        dx = x - self.mouse_x
        dy = y - self.mouse_y

        while dx != 0 or dy != 0:
            step_x = max(-40, min(40, dx))
            step_y = max(-40, min(40, dy))
            self.hmp(f"mouse_move {step_x} {step_y}")
            self.mouse_x += step_x
            self.mouse_y += step_y
            dx -= step_x
            dy -= step_y
            time.sleep(pause)

    def left_click(self, pause: float = 0.08) -> None:
        self.hmp("mouse_button 1")
        time.sleep(pause)
        self.hmp("mouse_button 0")
        time.sleep(pause)


def scenario_startx(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch startx", "desktop: session start"], timeout=90.0)
    session.send_key("ctrl-f", pause=0.15)
    session.wait_for_log("desktop: open-new w=0 t=3 i=0", timeout=8.0)
    session.send_key("ctrl-t", pause=0.15)
    session.wait_for_log("desktop: open-new w=1 t=1 i=0", timeout=8.0)


def run_command(session: QemuSession, command: str, timeout: float = 6.0, pause: float = 0.12,
                marker: Optional[str] = None) -> None:
    command_marker = marker
    if command_marker is None:
        command_marker = command.split(" ", 1)[0] if command else ""
    session.type_text(command, pause=pause)
    session.send_key("ret", pause=0.15)
    if command_marker:
        session.wait_for_log(f"shell: command {command_marker}", timeout=timeout)


def scenario_terminal_runtime(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch terminal", "desktop: open-new w=0 t=1 i=0"], timeout=20.0)
    time.sleep(1.0)
    run_command(session, "cc /hello.c", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
        timeout=12.0,
    )


def scenario_terminal_vidmodes(session: QemuSession) -> None:
    session.wait_for_all(["desktop.app: launch startx", "desktop: session start"], timeout=45.0)
    session.send_key("ctrl-t", pause=0.15)
    session.wait_for_log("desktop: open-new w=0 t=1 i=0", timeout=8.0)
    time.sleep(1.0)
    run_command(session, "vidmodes", timeout=8.0, marker="")
    session.wait_for_all(
        [
            "vidmodes: begin",
            "vidmodes: caps mode_count=",
            "vidmodes: try 800x600",
            "video: runtime modeset source=",
            "video: handoff stage=runtime source=",
            "vidmodes: mode verify ok 800x600",
            "vidmodes: mode ok 800x600",
            "vidmodes: try 1024x768",
            "vidmodes: mode verify ok 1024x768",
            "vidmodes: mode ok 1024x768",
            "vidmodes: restore ok ",
            "vidmodes: summary ok=",
        ],
        timeout=40.0,
    )


def scenario_open_terminal(session: QemuSession, timeout: float = 45.0) -> None:
    session.wait_for_all(["desktop.app: launch startx", "desktop: session start"], timeout=timeout)
    session.send_key("ctrl-t", pause=0.15)
    session.wait_for_log("desktop: open-new w=0 t=1 i=0", timeout=8.0)
    time.sleep(1.0)


def scenario_doom(session: QemuSession) -> None:
    scenario_open_terminal(session)
    run_command(session, "doom", timeout=8.0, marker="")
    session.wait_for_all(["desktop.app: launch doom", "desktop: open-new w=0 t=16 i=0"], timeout=20.0)
    time.sleep(0.8)
    session.send_key("ret", pause=0.15)
    session.wait_for_all(
        [
            "doom: key enter",
            "fs: asset file /DOOM/DOOM.WAD",
            "doom: port run begin",
        ],
        timeout=20.0,
    )


def scenario_craft(session: QemuSession) -> None:
    scenario_open_terminal(session)
    run_command(session, "craft", timeout=8.0, marker="")
    session.wait_for_all(["desktop.app: launch craft", "desktop: open-new w=0 t=17 i=0"], timeout=20.0)
    session.wait_for_all(
        [
            "fs: asset file /textures/texture.png",
            "fs: asset file /textures/font.png",
            "fs: asset file /textures/sky.png",
            "fs: asset file /textures/sign.png",
            "craft: start",
            "craft: after textures",
            "craft: session ready",
            "craft: first frame rc=",
        ],
        timeout=25.0,
    )


SCENARIOS = [
    Scenario(
        name="startx-desktop",
        description="Shell -> startx.app -> desktop session -> Ctrl+F files + Ctrl+T terminal",
        command="startx",
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
        ],
        action=scenario_startx,
    ),
    Scenario(
        name="startx-autoboot-desktop",
        description="Userland autostart reaches desktop session and opens files + terminal shortcuts",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=3 i=0",
            "desktop: open-new w=1 t=1 i=0",
        ],
        boot_markers=[
            BOOT_MARKER,
            "desktop.app: launch startx",
            "desktop: session start",
        ],
        action=scenario_startx,
    ),
    Scenario(
        name="terminal-app",
        description="Dedicated terminal launcher app",
        command="terminal",
        must_have=["desktop.app: launch terminal", "desktop: open-new w=0 t=1 i=0"],
    ),
    Scenario(
        name="clock-app",
        description="Dedicated clock launcher app",
        command="clock",
        must_have=["desktop.app: launch clock", "desktop: open-new w=0 t=2 i=0"],
    ),
    Scenario(
        name="filemanager-app",
        description="Dedicated filemanager launcher app",
        command="filemanager",
        must_have=["desktop.app: launch filemanager", "desktop: open-new w=0 t=3 i=0"],
    ),
    Scenario(
        name="editor-app",
        description="Dedicated editor launcher app",
        command="editor",
        must_have=["desktop.app: launch editor", "desktop: open-new w=0 t=4 i=0"],
    ),
    Scenario(
        name="taskmgr-app",
        description="Dedicated task manager launcher app",
        command="taskmgr",
        must_have=["desktop.app: launch taskmgr", "desktop: open-new w=0 t=5 i=0"],
    ),
    Scenario(
        name="calculator-app",
        description="Dedicated calculator launcher app",
        command="calculator",
        must_have=["desktop.app: launch calculator", "desktop: open-new w=0 t=6 i=0"],
    ),
    Scenario(
        name="sketchpad-app",
        description="Dedicated sketchpad launcher app",
        command="sketchpad",
        must_have=["desktop.app: launch sketchpad", "desktop: open-new w=0 t=7 i=0"],
    ),
    Scenario(
        name="personalize-app",
        description="Dedicated personalize launcher app",
        command="personalize",
        must_have=["desktop.app: launch personalize", "desktop: open-new w=0 t=18 i=0"],
    ),
    Scenario(
        name="edit-app",
        description="Dedicated edit launcher app",
        command="edit",
        must_have=["desktop.app: launch edit", "desktop: open-editor w=0 t=4 i=0"],
    ),
    Scenario(
        name="nano-app",
        description="Dedicated nano launcher app",
        command="nano",
        must_have=["desktop.app: launch nano", "desktop: open-editor w=0 t=4 i=0"],
    ),
    Scenario(
        name="terminal-runtime-apps",
        description="Graphical terminal executes runtime alias cc against /hello.c",
        command="terminal",
        must_have=[
            "desktop.app: launch terminal",
            "desktop: open-new w=0 t=1 i=0",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
        action=scenario_terminal_runtime,
    ),
    Scenario(
        name="cc-alias-shell",
        description="Text shell resolves cc as sectorc.app and compiles /hello.c",
        command="cc /hello.c",
        command_marker="cc",
        must_have=[
            "shell: command cc",
            "busybox: external ok sectorc",
            "sectorc: compile begin",
            "sectorc: compile ok",
        ],
    ),
    Scenario(
        name="java-explicit-path",
        description="Text shell executes java through /bin/java",
        command="/bin/java -version",
        command_marker="/bin/java",
        must_have=[
            "shell: command /bin/java",
            "busybox: external ok java",
            "java: version ok",
        ],
    ),
    Scenario(
        name="grep-explicit-path",
        description="Text shell executes grep through /compat/bin/grep against /hello.c",
        command="/compat/bin/grep print /hello.c",
        command_marker="/compat/bin/grep",
        must_have=[
            "shell: command /compat/bin/grep",
            "busybox: external ok grep",
            "grep: match ok",
        ],
    ),
    Scenario(
        name="doom-assets-app",
        description="Desktop autostart opens a terminal, launches DOOM and reaches the real WAD runtime",
        command=None,
        must_have=[
            "desktop.app: launch doom",
            "desktop: open-new w=0 t=1 i=0",
            "desktop: open-new w=0 t=16 i=0",
            "doom: key enter",
            "fs: asset file /DOOM/DOOM.WAD",
            "doom: port run begin",
        ],
        boot_markers=[
            BOOT_MARKER,
            "desktop.app: launch startx",
            "desktop: session start",
        ],
        action=scenario_doom,
    ),
    Scenario(
        name="craft-assets-app",
        description="Desktop autostart opens a terminal, launches Craft and reaches real texture loads and first frame",
        command=None,
        must_have=[
            "desktop.app: launch craft",
            "desktop: open-new w=0 t=1 i=0",
            "desktop: open-new w=0 t=17 i=0",
            "fs: asset file /textures/texture.png",
            "fs: asset file /textures/font.png",
            "fs: asset file /textures/sky.png",
            "fs: asset file /textures/sign.png",
            "craft: after textures",
            "craft: session ready",
            "craft: first frame rc=",
        ],
        boot_markers=[
            BOOT_MARKER,
            "desktop.app: launch startx",
            "desktop: session start",
        ],
        action=scenario_craft,
    ),
    Scenario(
        name="vidmodes-shell",
        description="Desktop autostart opens a terminal and exercises runtime video mode switching through vidmodes",
        command=None,
        must_have=[
            "desktop.app: launch startx",
            "desktop: session start",
            "desktop: open-new w=0 t=1 i=0",
            "vidmodes: begin",
            "vidmodes: caps mode_count=",
            "vidmodes: try 800x600",
            "video: runtime modeset source=",
            "video: handoff stage=runtime source=",
            "vidmodes: mode verify ok 800x600",
            "vidmodes: mode ok 800x600",
            "vidmodes: try 1024x768",
            "vidmodes: mode verify ok 1024x768",
            "vidmodes: mode ok 1024x768",
            "vidmodes: restore ok ",
            "vidmodes: summary ok=",
        ],
        boot_markers=[
            BOOT_MARKER,
            "desktop.app: launch startx",
            "desktop: session start",
        ],
        action=scenario_terminal_vidmodes,
    ),
]


def run_scenario(qemu_binary: str, image_path: Path, memory_mb: int, scenario: Scenario) -> ScenarioResult:
    with tempfile.TemporaryDirectory(prefix="vmod-") as temp_dir:
        workspace = Path(temp_dir)
        scenario_image = workspace / "boot.img"
        shutil.copyfile(image_path, scenario_image)

        session = QemuSession(qemu_binary, scenario_image, memory_mb, workspace)
        error: Optional[str] = None
        log = ""
        try:
            session.wait_for_all(scenario.boot_markers or [BOOT_MARKER, SHELL_READY_MARKER], timeout=60.0)
            if scenario.command:
                run_command(session,
                            scenario.command,
                            timeout=6.0,
                            pause=0.12,
                            marker=scenario.command_marker)
            if scenario.action:
                scenario.action(session)
            else:
                session.wait_for_all(scenario.must_have, timeout=16.0)
        except Exception as exc:
            error = str(exc)
        finally:
            log = session.read_log()
            session.close()

    missing = [marker for marker in scenario.must_have if marker not in log]
    return ScenarioResult(
        scenario=scenario,
        passed=(not missing and error is None),
        log=log,
        missing_markers=missing,
        error=error,
    )


def write_report(report_path: Path, results: List[ScenarioResult]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Modular Apps Validation Report",
        "",
        "## QEMU Interactive Matrix",
        "",
        "| Scenario | Result | Notes |",
        "| --- | --- | --- |",
    ]

    for result in results:
        notes = result.scenario.description
        if result.missing_markers:
            notes = "missing: " + ", ".join(result.missing_markers)
        if result.error:
            notes = (notes + " | " if notes else "") + result.error
        lines.append(f"| {result.scenario.name} | {'PASS' if result.passed else 'FAIL'} | {notes} |")

    lines.extend(["", "## Marker Summary", ""])
    for result in results:
        lines.append(f"### {result.scenario.name}")
        if result.passed:
            lines.append("- required markers observed")
        else:
            lines.append("- missing markers: " + ", ".join(result.missing_markers))
        if result.error:
            lines.append("- error: " + result.error)
        observed = [marker for marker in result.scenario.must_have if marker in result.log]
        if observed:
            lines.append("- observed markers: " + ", ".join(observed))
        preview = [line for line in result.log.strip().splitlines() if line][-20:]
        if preview:
            lines.extend(["", "```text", *preview, "```"])
        lines.append("")

    report_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate modular desktop/launcher apps in headless QEMU.")
    parser.add_argument("--image", required=True, help="boot image to validate")
    parser.add_argument("--report", required=True, help="markdown report output path")
    parser.add_argument("--qemu", default="qemu-system-i386", help="QEMU binary")
    parser.add_argument("--memory-mb", type=int, default=3072, help="guest RAM in MB")
    parser.add_argument("--scenario", action="append",
                        help="run only the named scenario (can be passed multiple times)")
    parser.add_argument("--expect-boot-mode",
                        help="require a boot-time video init marker for the given WxH mode, e.g. 800x600")
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    if not image_path.is_file():
        print(f"error: boot image not found: {image_path}", file=sys.stderr)
        return 1

    qemu_binary = shutil.which(args.qemu) or shutil.which("qemu-system-x86_64")
    if qemu_binary is None:
        print("error: no QEMU binary found", file=sys.stderr)
        return 1

    selected_scenarios = SCENARIOS
    if args.scenario:
        selected_names = set(args.scenario)
        selected_scenarios = [scenario for scenario in SCENARIOS if scenario.name in selected_names]
        missing_names = sorted(selected_names - {scenario.name for scenario in selected_scenarios})
        if missing_names:
            print("error: unknown scenario(s): " + ", ".join(missing_names), file=sys.stderr)
            return 1

    expected_boot_mode = None
    if args.expect_boot_mode:
        expected_boot_mode = parse_mode_text(args.expect_boot_mode)
        if expected_boot_mode is None:
            print("error: invalid --expect-boot-mode value, expected WxH", file=sys.stderr)
            return 1
        selected_scenarios = [
            Scenario(
                name=f"{scenario.name}-boot-{expected_boot_mode}",
                description=f"{scenario.description}; boot should initialize in {expected_boot_mode}",
                command=scenario.command,
                must_have=[*scenario.must_have, "video: boot init backend=", f"mode={expected_boot_mode}x8"],
                boot_markers=(scenario.boot_markers or [BOOT_MARKER, SHELL_READY_MARKER]),
                command_marker=scenario.command_marker,
                action=scenario.action,
            )
            for scenario in selected_scenarios
        ]

    results = [run_scenario(qemu_binary, image_path, args.memory_mb, scenario) for scenario in selected_scenarios]
    write_report(report_path, results)

    failed = [result for result in results if not result.passed]
    if failed:
        print(f"modular-apps: {len(failed)} scenario(s) failed; see {report_path}", file=sys.stderr)
        return 1

    print(f"modular-apps: validation passed; report written to {report_path}")
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    sys.exit(main())
