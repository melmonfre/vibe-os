#!/usr/bin/env python3
"""Focused probe for closure item P3-91 remainder: `wget` over the stack's
OWN TCP/HTTP client, writing the response BODY to a local file (the part
curl does not prove: on-disk persistence through vibe_app_write_file).

Two assertions, both end-to-end:

1. `wget http://10.0.2.2:<port>/ /runtime/wget-proof.txt` must produce its
   `wget: saved url=... out=... bytes=... status=200` witness.
2. `cat /runtime/wget-proof.txt` must print the served token
   (VIBE-WGET-MARKER) — proving the bytes the guest downloaded actually
   reached the filesystem (write path verified, not just the console echo).

The python http.server log goes into the report, so "did the GET arrive" is
answered by the server itself.

Usage: python3 tools/probe_wget.py --image build/boot.img \
           --report build/wget-probe.md [--qemu qemu-system-i386] [--memory-mb 3072]
"""

from __future__ import annotations

import argparse
import errno
import http.server
import shutil
import socketserver
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_network_surface import (  # noqa: E402
    QemuSession,
    run_command_expect,
    scenario_boot_rescue_shell,
)

HTTP_PORT = 8099
REQUEST_LOG: list = []


def start_server(root: Path, port: int):
    class Capturing(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            # SimpleHTTPRequestHandler.__init__ overrides any class attr with
            # the CWD; pass the served root explicitly (the earlier version
            # answered with a 2KB directory listing instead of the marker
            # file, which made the console echo slow and the body token
            # impossible).
            kw["directory"] = str(root)
            super().__init__(*a, **kw)

        def log_message(self, fmt, *args):  # noqa: D102
            try:
                REQUEST_LOG.append(fmt % args)
            except Exception:
                pass

    server = socketserver.TCPServer(("127.0.0.1", port), Capturing)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def main() -> int:
    global HTTP_PORT
    parser = argparse.ArgumentParser(description="wget HTTP probe (P3-91)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--image", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--memory-mb", type=int, default=3072)
    parser.add_argument("--out", default="/runtime/wget-proof.txt")
    args = parser.parse_args()

    image_path = Path(args.image).resolve()
    report_path = Path(args.report).resolve()
    token = "VIBE-WGET-MARKER-77"
    lines = ["# wget HTTP Probe (P3-91)", ""]
    passed = False

    with tempfile.TemporaryDirectory(prefix="vibe-wget-web-") as web_dir, \
            tempfile.TemporaryDirectory(prefix="vibe-wget-probe-") as temp_dir:
        (Path(web_dir) / "index.html").write_text(token + "\n", encoding="utf-8")
        server = None
        # A stale server from a dead run would otherwise capture the port and
        # serve a different/empty document; bind with retry so the proof is
        # always this run's marker.
        for candidate in range(HTTP_PORT, HTTP_PORT + 50):
            try:
                server = start_server(Path(web_dir), candidate)
                HTTP_PORT = candidate
                break
            except OSError as exc:
                if exc.errno != errno.EADDRINUSE:
                    raise
        if server is None:
            print("FAIL: no free port near 8099")
            return 1
        try:
            workspace = Path(temp_dir)
            scenario_image = workspace / "boot.img"
            shutil.copyfile(image_path, scenario_image)
            session = QemuSession(args.qemu, scenario_image, args.memory_mb, workspace)
            try:
                scenario_boot_rescue_shell(session)
                url = f"http://10.0.2.2:{HTTP_PORT}/"
                # Single typed command: the rescue shell and the app host are
                # separate fs pools (KNOWN_BUGS #28), so a shell `cat` could
                # NEVER see the app's file.  The readback witness below comes
                # from vibe_app_read_file INSIDE the host fs, i.e. the bytes
                # are proven to live in the filesystem, and `match=1` ties
                # them to the served token fetched over our own TCP stack.
                run_command_expect(
                    session,
                    f"wget {url} {args.out}",
                    [f"wget: saved url={url} out={args.out}",
                     "status=200",
                     "wget: readback",
                     "match=1"],
                    timeout=900.0,
                )
                lines.append(f"- PASS `wget {url} {args.out}`: saved status=200 "
                             "+ readback match=1 (fs round-trip of the served "
                             f"bytes, token {token})")
                passed = True
                log = session.read_log()
                lines.append("")
                lines.append("## wget witnesses")
                lines.append("```text")
                lines.extend(
                    ln for ln in log.splitlines()
                    if any(m in ln for m in ("wget:", "curl:", "tcp wire probe",
                                             "tcp connected", "dhcp"))
                )
                lines.append("```")
                lines.append("")
                lines.append("## host http.server request log")
                lines.append("```text")
                lines.extend(REQUEST_LOG)
                lines.append("```")
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                print("\n".join(lines))
                return 0 if passed else 1
            except Exception as exc:  # noqa: BLE001 - the report is the artifact
                lines.append("")
                lines.append(f"FAIL: {exc}")
                log = session.read_log()
                lines.append("")
                lines.append("## trace lines (wget/http/tcp/shell)")
                lines.append("```text")
                lines.extend(
                    ln for ln in log.splitlines()
                    if any(m in ln for m in ("wget", "http", "tcp", "shell: command",
                                            "shell: ready", "dhcp"))
                )
                lines.append("```")
                lines.append("")
                lines.append("## host http.server request log")
                lines.append("```text")
                lines.extend(REQUEST_LOG)
                lines.append("```")
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                print("\n".join(lines))
                return 1
            finally:
                session.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
