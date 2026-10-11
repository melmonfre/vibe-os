# Docs Map

**VibeOS** — A service-oriented microkernel operating system for x86 (i386), with real hardware drivers, a lightweight GUI, and microkernel-based services.

## Project Status

| Component | Status |
| --- | --- |
| Boot chain (MBR → VBR → Stage2 → Kernel) | ✅ Complete |
| Memory subsystem (free-list allocator, 4GB physmem, 4KB paging) | ✅ Complete |
| Storage: ATA, AHCI, NVMe, USB BOT | ✅ Complete |
| Network: VirtIO, e1000, rtl8139, Intel 7260 Wi-Fi | ✅ Complete |
| Audio: Intel HDA, AC97, USB Audio, pcspkr fallback | ✅ Complete |
| Input: PS/2 keyboard, mouse | ✅ Complete |
| Video: LFB, VESA, native DRM (BGA, i915, radeon, nouveau) | ✅ Complete |
| Desktop GUI (microui, dynamic desktop, taskbar, applets) | ✅ Complete |
| Microkernel services (IPC, restart, events, async) | ✅ Complete (Phases A-G) |
| ABI compatibility (BSD/POSIX shapes, ELF32, sockets, termios) | ✅ Complete |
| Wi-Fi WPA2 (PBKDF2-SHA1, 4-way handshake, 802.11 scanning) | ✅ Complete |
| USB substrate (UHCI/OHCI/EHCI/XHCI discovery, device descriptors) | ✅ Complete |
| Native USB mass-storage runtime (NVMe, USB BOT) | ✅ Complete |
| Video DRM backends (BGA, i915, radeon, nouveau) | ✅ Complete |
| POSIX/Compat layer (BSD libc, pthreads, sockets, X11) | ✅ Complete |
| SMP bring-up and validation | 🔄 In progress |
| POSIX Graphics X-Like compatibility | 🔄 In progress |
| Craft (voxel game) | 🔄 In progress |

## Layout

This directory is split into two kinds of documentation:

- technical reference docs in `docs/`
- planning, migration, and historical guidance docs in `docs/guidelines/`

Use this file as the entry point so we do not keep drifting copies of the same plan in both places.

## Start Here

If you want the code-guided architecture walkthrough, read these first:

- [overview.md](overview.md)
- [workflow.md](workflow.md)
- [mbr.md](mbr.md)
- [stage1.md](stage1.md)
- [stage2.md](stage2.md)
- [memory_map.md](memory_map.md)
- [kernel_init.md](kernel_init.md)
- [drivers.md](drivers.md)
- [runtime_and_services.md](runtime_and_services.md)
- [apps_and_modules.md](apps_and_modules.md)

## Active Plans In `docs/`

These are active project plans or validation-oriented documents that still make sense as top-level docs:

- [FINALIZATION_EXECUTION_PLAN.md](FINALIZATION_EXECUTION_PLAN.md)
- [guidelines/COMPLETION_ACTION_PLAN.md](guidelines/COMPLETION_ACTION_PLAN.md) — unified completion plan (audited 2026-10-11)
- [NETWORK_AUDIO_PANEL_PLAN.md](NETWORK_AUDIO_PANEL_PLAN.md)
- [VIDEO_BACKEND_REWRITE_PLAN.md](VIDEO_BACKEND_REWRITE_PLAN.md)
- [ICON_THEME_IMPLEMENTATION_PLAN.md](ICON_THEME_IMPLEMENTATION_PLAN.md)
- [CRAFT_OPTIMIZATION_PLAN.md](CRAFT_OPTIMIZATION_PLAN.md)
- [AUDIO_HARDWARE_VALIDATION_MATRIX.md](AUDIO_HARDWARE_VALIDATION_MATRIX.md)
- [KNOWN_BUGS.md](KNOWN_BUGS.md)

## Guidelines In `docs/guidelines/`

These are migration plans, compatibility plans, build notes, or historical implementation guides. They are still useful, but they are not the primary code-reference docs:

- [guidelines/README.md](guidelines/README.md)
- [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md)
- [guidelines/QUICK_BUILD.md](guidelines/QUICK_BUILD.md)
- [guidelines/COMPAT_PLAN.md](guidelines/COMPAT_PLAN.md)
- [guidelines/COMPAT_PLAN2.md](guidelines/COMPAT_PLAN2.md)
- [guidelines/COMPAT_SYS_REUSE.md](guidelines/COMPAT_SYS_REUSE.md)
- [guidelines/MODULAR_APP_INTEGRATION_PLAN.md](guidelines/MODULAR_APP_INTEGRATION_PLAN.md)
- [guidelines/VIBELOADER_PLAN.md](guidelines/VIBELOADER_PLAN.md)
- [guidelines/smp.md](guidelines/smp.md)

## Canonical Location Rules

- If a document explains how the current code works, keep it in `docs/`.
- If a document is a long-lived plan, migration checklist, compatibility note, build note, or historical handoff, keep it in `docs/guidelines/`.
- If a document is an active execution plan tightly tied to current product work or validation campaigns, it can stay in `docs/`.
- Do not keep full duplicated copies across both locations.
- If an old path is still convenient for editors or bookmarks, keep only a short pointer file there.
