# Microkernel Migration Plan

## Goal

Transform VibeOS from a monolithic kernel into a service-oriented microkernel architecture with:

- functional BIOS boot path with MBR/VBR compatibility
- real disk partitioning
- FAT32 support for boot and system volumes
- storage support for IDE, SATA, and AHCI
- USB boot compatibility
- smaller privileged kernel core
- drivers and high-level services moved out of the kernel core over time

## Current Reality

The repository is still a monolithic kernel with early drivers linked into the core image. The immediate work is to create a migration path that preserves bootability while we progressively split responsibilities.

This document is a live checklist. Items are only marked complete when code lands in the tree.

Important audit note: in the current tree, a checked item means the migration boundary or ABI milestone exists in code. It does not automatically mean the final end-state backend has already been extracted out of the kernel or that every compatibility fallback has disappeared.

## Phase 0: Foundation

- [x] Create migration document and tracked execution checklist
- [x] Add microkernel process/service/message scaffolding to the kernel tree
- [x] Add service registry bootstrap during kernel init
- [x] Define kernel-to-service ABI versioning
- [x] Define user-space driver/service launch protocol

## Phase 1: Kernel Core Minimization

- [x] Reduce kernel core responsibilities to:
- [x] scheduler
- [x] low-level memory management
- [x] IPC/message passing
- [x] interrupt routing
- [x] process/service supervision
- [x] move filesystem logic behind a service boundary
- [x] move high-level storage management behind a service boundary
- [x] move console/video policy behind a service boundary
- [x] move input policy behind a service boundary
- [x] move socket/network control plane behind a service boundary
- [x] move audio control/data plane behind a service boundary

## Phase 2: Boot Chain

- [x] restore a robust `MBR -> VBR/stage1 -> loader -> kernel` path
- [x] support active partition boot on BIOSes that reject superfloppy layouts
- [x] keep boot-time diagnostics available without text mode dependency
- [x] define boot info handoff contract for the microkernel loader
- [x] add a dedicated loader stage that can load kernel + initial services from FAT32

## Phase 3: Partitioning and Filesystems

- [x] add MBR partition parsing
- [x] add FAT32 reader for boot volume
- [x] add FAT32 writer support where safe
- [x] define system partition layout
- [x] define data/app partition layout
- [x] keep backward-compatibility tooling for current raw image format during migration

## Phase 4: Storage Drivers

- [x] keep legacy IDE path working
- [x] add AHCI controller detection
- [x] add AHCI read path
- [x] add AHCI write path
- [x] add SATA device enumeration through AHCI
- [x] unify storage devices behind one block-device abstraction
- [x] add USB mass-storage boot/loading strategy

## Phase 5: Initial User-Space Services

- [x] storage service
- [x] filesystem service
- [x] display service
- [x] input service
- [x] log/console service
- [x] network service
- [x] audio service
- [x] process launcher / init service

## Phase 6: Compatibility and Validation

- [x] QEMU regression target
- [x] Core 2 Duo regression target
- [x] Pentium / Atom regression target
- [x] BIOSes requiring active MBR partition
- [x] USB boot validation matrix
- [x] IDE/SATA/AHCI validation matrix

## Current Open Work

This section stays near the top on purpose: open migration work first, completed work at the end of the file.

- Phase D headless desktop validation is green again, including `vidmodes-shell`; the remaining work is no longer bring-up proof, but finishing the architectural extraction behind that proof.
- The video path is still intentionally hybrid: lightweight desktop-class video IPC keeps a local continuity fast path, while `MK_MSG_VIDEO_MODE_SET` / `MK_MSG_VIDEO_LEAVE` still flow through the dedicated `videosvc` service task.
- The remaining hard boundary is backend ownership. GPU/MMIO privilege, backend coordination, and the final split between privileged video backend work and user-space service ownership are still open.
- This migration slice still does not need to block the other planning documents. Reconcile those separately after this architectural cleanup.

## Audited Remaining Gaps

These are the items that still prevent the migration from being considered fully finished end-to-end, even though the phase checklist above is green:

- Native USB mass-storage runtime support is still missing. USB BIOS boot works for `MBR -> VBR -> stage2 -> kernel`, but once the kernel takes over there is still no native USB block backend.
- The extracted service model still uses compatibility bridges in important places. `storage`, `filesystem`, `console`, and `network` still route steady-state request bodies through the kernel-side backend-shim/local-handler path, while `input`, `video`, and `audio` now have dedicated user-space request loops but still depend on kernel-owned backend state and fallbacks.
- `network` and `audio` are not feature-complete services yet. Their current implementations provide ABI shape, supervision, and service lifecycle, but not a real NIC packet path, socket stack, audio DMA pipeline, or mixer/playback backend.
- The current `stage2` path is still a pragmatic FAT32 loader, not a fully general filesystem loader. It is reliable for the current image layout, but it still relies on the current contiguous/linear loading strategy documented above.

## Event-Driven Async Migration Plan

This is the concrete migration path from the current hybrid service model toward a genuinely event-driven microkernel.

The target end-state is:

- user-facing apps never block the desktop/UI loop on storage, input, network, audio, or video backend progress
- every long-lived subsystem owns an explicit event queue and worker context
- syscalls become enqueue/subscribe/ack style boundaries where possible, instead of synchronous "do the whole job now" traps
- service hosts stop calling back into preserved kernel local handlers for steady-state operation
- the privileged kernel shrinks toward scheduling, memory, IPC, interrupt routing, supervision, and a minimal hardware isolation layer

### Architecture Principles

- queue work, do not run full device transactions in UI-facing code paths
- prefer request submission + progress notification over synchronous polling loops
- separate control plane from data plane for audio, video, network, and storage
- separate input capture from desktop rendering, and desktop rendering from presentation
- allow degraded services to fail independently without taking the desktop event loop down with them
- treat compatibility bridges as temporary migration shims, not permanent architecture
- treat the desktop session as the primary user-facing workload regardless of CPU count
- treat mouse and keyboard responsiveness as irrevocable requirements
- run non-desktop work in independent async worker contexts that generate auditable events
- run `userland.app`, shell, desktop, and foreground apps outside the bootstrap/main thread in their own independent worker/thread contexts
- reduce the bootstrap/main thread to supervision, event routing, and recovery orchestration only
- let supervision restart non-desktop services instead of allowing them to stall desktop responsiveness

### Non-Negotiable UX Contract

- the desktop must remain responsive even when optional services are degraded
- pointer motion, clicks, and keyboard input must not depend on audio, storage, video, or network backends making progress
- the shell, desktop session, and userland apps must not monopolize or live inside the bootstrap/main thread
- startup sound and background work may fail fast; desktop, mouse, and keyboard may not
- scheduler and supervision policy should explicitly favor desktop/input continuity over optional background throughput
- service failures should trigger restart/isolation semantics, not UI-wide stalls

### Priority Order

Everything should run in separated async worker/service contexts, but not with equal priority.

Priority tiers for scheduling, event dispatch, supervision, and restart policy:

1. desktop session continuity
2. keyboard input
3. mouse / pointer input
4. compositor / frame present / video session continuity
5. core storage/filesystem operations needed by the foreground session
6. audio playback/capture
7. network datapath and background daemons
8. foreground/background apps outside desktop/shell
9. optional/background work

Rules:

- a lower-priority worker must never be allowed to stall a higher-priority one
- desktop, keyboard, and mouse paths should always retain forward progress first
- standalone apps must not outrank network service continuity; apps come after network in the policy order
- when the system is overloaded, optional/background work should be throttled before foreground interactivity is touched
- restart policy should favor recovering lower-priority services around a still-live desktop instead of restarting the whole session
- priority tiers `5+` (`audio`,`network`, `app`, and `background`) must never be allowed to compromise BIOS boot completion, `userland.app` bootstrap, or `startx` session bring-up
- if a `5+` worker wedges the system during migration, the recovery policy should kill that worker first and continue preserving tiers `1..4` instead of sacrificing desktop/session continuity

### Phase A: Kernel Event Primitives

- [x] process/service bootstrap and request/reply IPC exist
- [x] service supervision/restart exists in initial form
- [x] cooperative execution points exist through `yield`/`sleep`
- [x] timer-driven tick hooks now exist as an initial event substrate and now drive waitable timeout wakeups
- [x] add first-class kernel event objects (`queue`, `waitable`, `signal`, `completion`)
- [x] add per-service event mailbox/ring abstraction instead of ad-hoc request transport only
: the kernel now has a reusable waitable-backed `kernel_mailbox` primitive, and task-lifecycle plus service/audio/video/network event delivery all flow through that shared mailbox layer instead of bespoke subscriber rings
- [x] add timeout/cancel primitives for pending work items
- [x] add non-busy wait/wakeup path so services stop relying on `yield`/poll loops
- [~] add subscription model for async completion and state-change notifications
: service supervision and task-lifecycle subscriptions now exist for `launched` / `terminated` / `blocked` / `woke` / `restart-requested`, the task stream now also carries an explicit `task_class` tag plus subscription-side event/class filters so bootstrap/desktop/app runtime supervisors can subscribe more narrowly, but subsystem-level async completions are still missing outside the per-domain streams
- [~] subsystem event streams now also surface ring pressure through per-event `dropped_events` telemetry for audio/video/network consumers, so queue overruns stop disappearing silently during restart/degradation work
- [x] define scheduler-visible event metadata so the kernel can audit pending events and prioritize desktop/input-critical work
- [ ] define one independent async worker/thread context per major task class instead of reusing UI loops as pumps
- [~] establish explicit task-class metadata for launched workers before the final mailbox split
: launch contexts, task snapshots, and task-lifecycle events now expose concrete classes such as `supervision`, `desktop`, `shell`, `app-runtime`, `storage-io`, `filesystem-io`, `audio-io`, `network-io`, `video-control`, `video-present`, and `input`, and userland subscribers can now filter on those classes; the remaining gap is turning those class tags into true per-class worker/mailbox ownership instead of only better-tagged scheduling/supervision telemetry
- [~] move bootstrap/main-thread responsibility to supervision/event arbitration instead of foreground app execution
: `init` now remains in a supervision/event loop, subscribes to service/task lifecycle events, `desktop-host` relaunches a separate `startx-host` task instead of executing the desktop session inline, and the generic AppFS launch path now carries a small serialized `argv` via `SYSCALL_LAUNCH_APP`; shell foreground AppFS commands now launch a direct `app-runtime` task and wait on task-lifecycle events instead of owning `lang_try_run()` inline, `shell-host` now launches a separate `shell` session task instead of owning `shell_start_ready()` inline, `startx-host` / `desktop-audio` / `boot-audio` now launch detached AppFS workers and wait on task events instead of owning the normal path inline, and the fallback desktop session now also launches as its own builtin task instead of running inside `startx-host` / `desktop-host`; oversized/richer launch payloads and broader per-class mailbox ownership still remain
- [~] detached AppFS launch telemetry now exposes `argc`/`argv` through launch-info reporting and host-side debug logs, so supervised generic app workers are easier to audit while broader mailbox/supervision work is still pending
- [~] `init` no longer needs to own boot-sound playback directly on non-desktop boots; dedicated `desktop-audio` / `boot-audio` builtin workers now supervise detached `audiosvc play-asset` launches and retain inline playback only as fallback

Implementation to finish Phase A:

1. add a generic task-lifecycle event stream beside service/audio/video/network streams
   - publish `launched`, `terminated`, `blocked`, `woke`, and `restart-requested`
   - make scheduler snapshots and supervisor logs correlate to the same event IDs
   - use this as the canonical wakeup source for `desktop-host`, `shell-host`, and later AppFS app runtime supervisors
2. add one waitable-backed mailbox per major task class
   - `desktop`
   - `input-keyboard`
   - `input-pointer`
   - `video-control`
   - `video-present`
   - `storage-io`
   - `filesystem-io`
   - `audio-io`
   - `network-io`
   - `app-runtime`
3. introduce launch helpers for independent user workers instead of inline `lang_try_run()` ownership
   - `desktop-host` owns session supervision only
   - `startx-host` owns session launch only
   - shell command execution and modular AppFS app execution move into separate launched tasks
   - modular AppFS launch now has a reusable short-`argv` runtime launch path; the shell foreground path uses task events by default, while richer payload fallbacks or deeper launch supervision can evolve independently
4. make the bootstrap thread reject foreground execution work after handoff
   - bootstrap may supervise, subscribe, restart, and log
   - bootstrap may not become the long-lived owner of shell, desktop, or app execution

Acceptance for Phase A:

- every wait-heavy subsystem blocks on waitables, not `yield()` loops
- `init` remains a pure supervision loop after startup
- desktop/shell/app execution are all observable through task-lifecycle events
- the tree has a reusable spawn/supervise path for foreground modular apps, not only for built-in hosts

Execution slices for Phase A:

1. land the remaining task-lifecycle events and make them visible in telemetry
2. add per-class mailboxes without changing existing request/reply ABIs
3. convert built-in hosts to mailbox-backed supervision
4. convert generic AppFS foreground app launch to independent task contexts

Phase A regression risks:

- adding new wait paths that accidentally starve the current desktop host
- introducing task-event spam that overruns tiny rings and hides the one event the supervisor needs
- keeping `lang_try_run()` as an implicit ownership path inside supervision/host tasks after detached runtime launch exists

Phase A validation gate:

- boot reaches `init` supervision idle
- desktop host launch still works
- shell host launch still works
- at least one modular app launches in its own task and emits lifecycle events on launch/exit

### Phase B: Input / Desktop Decoupling

- [x] keyboard polling can bypass degraded worker transport
- [x] mouse polling can bypass degraded worker transport
- [~] `init` now launches built-in `shell-host` / `desktop-host` user tasks instead of running shell/desktop inline; `desktop-host` now supervises a separate `startx-host` task instead of running the session inline, `shell-host` now supervises a separate `shell` session task, and generic foreground modular apps now also launch as direct `app-runtime` tasks, but richer launch payload handling and the remaining per-class mailbox ownership work still remain
- [~] move input service to event publication ownership instead of kernel fallback ownership
: the shared `INPUT_EVENT` stream now sits on top of explicit kernel-owned keyboard and mouse queues with their own waitable contexts, `input` now runs through a dedicated userland service host for `event` / `mouse` / `key` / `layout` requests, and the desktop no longer keeps a steady-state direct-queue bypass; raw capture still originates in the kernel queues and bootstrap/critical rescue fallback still exists when the service is unavailable
- [~] split desktop input ingestion from desktop render/update loop
  : desktop agora coleta eventos de input em um batch explicito antes de update/render, consolida efeitos de mouse/async state nesse estagio e o caminho principal do desktop passa a consumir `INPUT_EVENT` tambem para mouse em vez de depender do polling paralelo do mouse; a separacao completa em workers/loops independentes ainda nao foi feita
- [~] introduce explicit per-device queues for keyboard, mouse, and future gamepad/touch sources
: keyboard and mouse now enqueue into dedicated kernel device queues in parallel with the compatibility aggregate stream used by current userland consumers; future gamepad/touch sources and a fully extracted input publisher are still pending
- [~] convert desktop shortcuts, pointer motion, focus changes, and window actions into queued events
  : pointer motion, wheel scroll e cliques do mouse agora entram primeiro em uma fila interna curta de eventos de UI, as teclas/atalhos do desktop tambem passam por uma fila explicita antes do processamento, drag/resize/menu-scroll-drag/pintura continua do sketchpad ja reagem aos eventos `POINTER_MOVE`, toggle/focus-raise/close/minimize/maximize/begin-drag/begin-resize de janelas ja passam por uma fila dedicada de acoes antes de mutar estado, a fila de acoes de sessao agora absorve parte importante do clique esquerdo como toggle/close do start menu, launch dos icones principais da area de trabalho, launch dos atalhos laterais do start menu, abertura de entradas do proprio start menu e fechamento pos-acao dos context menus de app/file manager, e uma fila curta de acoes de app ja despacha operacoes da Trash, as acoes primary/save-as de Editor/Sketchpad, o botao save do Editor, as interacoes principais de up/lista do File Manager, as operacoes do menu de contexto do proprio File Manager, praticamente todo o bloco de tema/wallpaper/resolucao do Personalize, os cliques principais de Task Manager/Calculator/Sketchpad e tambem os cliques de ImageViewer, Audio Player, Flap Birb, DOOM e Craft; Task Manager, Calculator, Sketchpad e Personalize agora tambem fazem o proprio hit-testing por esse dispatcher, os app-clicks simples desses blocos tambem ja passam por um helper compartilhado de enqueue e por um mapeamento central `app_type -> action`, `Editor`, `File Manager` e `Trash` agora tambem ja roteiam seu conteudo por helpers dedicados, o dispatcher de conteudo de janela ja foi achatado para um roteador mais direto por `switch`/tipo de app, e o flush das filas de sessao/janela no loop principal agora tambem passa por helpers dedicados, inclusive um flush composto pos-clique para sessao/app/janela; o loop principal passou a delegar o roteamento de conteudo de janela, a moldura/controles de janela, o fechamento de overlays/popups do shell, o taskbar-toggle de janelas, o roteamento final de janela sob clique no dispatcher de shell, os atalhos da area de trabalho, o clique do start menu, os cliques contextuais/sessao iniciais, o clique do file dialog, o clique direito do desktop/file manager/app context, o clique dos applets de rede/som e o bloco residual de shell/taskbar/janelas para helpers dedicados; alem disso, o fechamento de `CLOSE_CONTEXTS` e o fechamento de popups do shell agora tambem passam por helpers centrais reutilizados, mas ainda restam decisoes e acoes de app/janela no fluxo direto atual
- [~] make `startx` survive input-service restart without direct-driver fallback
  : `startx` no longer relies on a desktop-only direct-driver steady-state path: the desktop now goes through the dedicated `input` service host in normal operation, still subscribes to `inputsvc` lifecycle/reset events, and only bootstrap/critical callers retain rescue fallback when the service is actually unavailable; the remaining gap is proving the full `kill input` -> recover -> launch-app path reliably in automated smoke and on real USB hardware, then moving raw publication ownership fully into `inputsvc`
- [~] desktop-launched `INPUT_EVENT` consumers now follow the service path while still reacting to `inputsvc` restart/degrade events
  : this removes the previous request/reply bypass in the desktop path and makes the steady-state architecture match the service boundary, but the underlying keyboard/mouse producer is still kernel-owned rather than a fully extracted `inputsvc` publisher
- [~] reserve scheduling/service priority for desktop, mouse, and keyboard above optional services
  : a atribuicao de prioridade agora promove tasks userland marcadas como `shell`/`desktop`/`bootstrap`/`critical` para `PROCESS_PRIORITY_DESKTOP_USER`, coloca `inputsvc` junto do `console` em `PROCESS_PRIORITY_INPUT`, acima de audio/network/background, e ainda reserva slices iniciais mais fortes para app runtimes/servicos recem-lancados para que launch/restart nao fiquem presos atras do trafego continuo de input/desktop; ainda falta provar a politica com smoke automatizado de restart e revisar se o cutover final precisa de uma regra ainda mais explicita para video
- [~] prove keyboard and mouse remain live while audio/network/video workers restart
  : headless QEMU now has explicit in-tree restart smoke for `audiosvc`, `network`, and `videosvc`: the desktop keeps dedicated chaos shortcuts, the start menu now also exposes click-driven `kill input/audio/video/network` plus `spawn clock` entries for lower-control smoke, and the validation script encodes matching restart scenarios plus a post-restart desktop-icon click. The remaining gap is getting that validation path green again under the current headless QEMU input-injection limits, then carrying the same proof into real hardware under restart/degradation, extending USB input beyond the current UHCI/OHCI-oriented boot HID datapath when the machine only exposes EHCI/xHCI, and then moving steady-state publication ownership fully into `inputsvc`

Implementation to finish Phase B:

1. move input publication ownership into `inputsvc`
   - IRQ handlers push raw device events into kernel device queues only
   - `inputsvc` becomes the sole steady-state publisher of normalized keyboard/mouse events
   - direct driver reads remain rescue-mode only
2. split the desktop main loop into explicit stages
   - ingest input batch
   - apply queued desktop/window/session actions
   - update app state
   - submit present request
   - drain async service events
   this keeps input ingestion logically separate even before a full compositor worker lands
3. make `startx` and desktop survive input-service restart
   - detect `input` `offline/degraded/restarted/recovered`
   - freeze only input-dependent transient state such as drag or menu-scroll-drag
   - re-subscribe automatically after restart
   - do not fall back to direct-driver polling in normal desktop mode
4. codify scheduler priority for interactivity
   - desktop continuity tier above all
   - keyboard tier above pointer-neutral background work
   - pointer tier above audio/network/app/background
   - degraded lower tiers must be terminated before interactivity tiers are sacrificed
   - detached shell/bootstrap/critical workers should remain in the same interactive band as desktop continuity helpers
5. add proof scenarios
   - restart `inputsvc` while moving the mouse
   - restart `audiosvc` while typing in terminal
   - restart `network` while dragging windows
   - restart `videosvc` and confirm input remains queued and replay-safe

Acceptance for Phase B:

- `startx` continues after `inputsvc` restart without reboot and without direct-driver steady-state fallback
- keyboard and pointer stay live while audio/network workers are killed or restarted
- desktop actions are queued before mutating window/session state
- scheduler policy explicitly favors desktop/input classes in code, not only in docs

Execution slices for Phase B:

1. make desktop re-subscribe and recover across `inputsvc` restart
2. remove normal-mode direct-driver fallback from desktop paths
3. codify interactive priority in scheduler/service restart policy
4. add restart-smoke scenarios for input/audio/network/video while desktop is in active use

Phase B regression risks:

- losing pointer continuity after input restart because drag/menu transient state is not canceled safely
- accidentally making safe mode or rescue shell depend on the new input-service ownership path
- raising desktop/input priority without throttling lower tiers first, causing starvation inversions elsewhere

Phase B validation gate:

- `make run` reaches responsive desktop
- restarting `inputsvc` does not require reboot
- mouse moves and keyboard typing still work during audio/network degradation
- `startx` session remains alive even when input service cycles

### Phase C: Audio Async Data Plane

- [x] control/status ABI exists
- [x] direct write path exists for current backends
- [x] async enqueue syscall exists for startup playback (`SYSCALL_AUDIO_WRITE_ASYNC`)
- [x] startup sound no longer needs to run as a synchronous desktop-owned playback loop
- [~] move audio queue ownership entirely into `audiosvc`
: `audiosvc` now has a dedicated userland request loop, and both `SYSCALL_AUDIO_WRITE` plus `SYSCALL_AUDIO_WRITE_ASYNC` now route through that service boundary instead of bypassing IPC or falling back to the generic backend-shim path; the async ring, completion accounting, and backend execution are still kernel-owned
- [~] add evented playback completions / underrun notifications back to userland
: `audiosvc` now publishes `queued` / `idle` / `underrun` events through a dedicated ABI over the shared mailbox-backed event layer, and both diagnostics plus the async WAV helper consume that stream on the kernel-async path; queue ownership and steady-state completion semantics still need to move fully out of the preserved kernel bridge
- [~] audio async telemetry now carries per-subscriber `dropped_events` pressure so `audiosvc` queue churn is visible to diagnostics while queue ownership is still migrating
- [ ] add async capture queue and delivery path
- [ ] stop using the desktop process as a cooperative pump participant for audio progress
- [ ] make `compat-auich`, `compat-azalia`, and future `compat-uaudio` complete playback/capture without UI-coupled progress
- [ ] move mixer policy/default-route policy fully out of kernel local handlers

Implementation to finish Phase C:

1. move playback queue ownership into `audiosvc`
   - `SYSCALL_AUDIO_WRITE_ASYNC` becomes submit-only
   - queue depth, producer index, completion, and underrun accounting live in `audiosvc`
   - kernel bridge stops owning long-lived playback state
2. add capture queue ABI
   - subscribe to capture-ready events
   - receive completed capture chunks by transfer buffer ID
   - support timeout/cancel and overflow reporting
3. split control plane from dataplane
   - control plane: route, params, start/stop, mixer, backend selection
   - dataplane: playback buffers, capture buffers, completion and underrun events
4. remove desktop from audio progress paths
   - startup sound remains fire-and-forget
   - app/desktop audio players submit and observe completions; they never advance DMA/ring state themselves
5. promote real backend ownership in service context
   - `compat-auich`, `compat-azalia`, and later `compat-uaudio` run to completion behind `audiosvc`
   - backend-specific IRQ/progress feeds service-owned queues/events
6. move policy out of kernel local handlers
   - default output route
   - backend preference
   - mute/volume persistence
   - capture source selection

Acceptance for Phase C:

- playback and capture complete through `audiosvc` queues/events only
- desktop/UI remains responsive if audio backend stalls or underruns
- no audio path requires desktop cadence or UI code to make hardware progress
- backend selection and mixer policy are visible in telemetry and service state

Execution slices for Phase C:

1. move playback queue ownership into `audiosvc`
2. add capture queue and completion path
3. move backend progress and IRQ completion accounting under service ownership
4. move mixer/default-route policy out of kernel handlers

Phase C regression risks:

- startup sound path regressing desktop bring-up latency
- capture queue work starving playback if both share one coarse worker
- backend selection turning nondeterministic across QEMU and laptop targets

Phase C validation gate:

- startup sound either completes asynchronously or fails fast
- playback and capture work without desktop loop involvement
- backend telemetry names the active path consistently
- killing/restarting `audiosvc` does not freeze input or presentation

### Phase D: Video / Presentation Split

- [ ] separate window/compositor logic from framebuffer/present backend logic
- [~] introduce explicit present queue / frame fence model
: `videosvc` now publishes `present` / `mode-set` / `leave` events through a dedicated mailbox-backed ABI, `present submit` now returns a concrete stable fence `sequence` token that the desktop uses on its main path, and a dedicated `video-present` worker now drains a mailbox-backed present queue independently of the desktop/session loop; broader continuity proof under headless restart smoke still remains open
- [~] `videosvc` now also emits an explicit `present-submitted` stage plus per-event `pending_depth` / `completed_sequence` telemetry, so the present path is visible as a service-owned fence lifecycle even though the backend work is still completing synchronously underneath
- [~] video event delivery now reports subscriber-ring overflow explicitly, so presenter pressure and missed notifications are visible across the queued presenter worker path too
- [~] desktop now also consumes the video fence/backlog stream directly and resets its async video path when `videosvc` reports overflow or dropped notifications, so presenter-pressure faults no longer leave the compositor blind to stale fence state
- [~] stop doing heavyweight backend work directly from desktop paint cadence
: the desktop main loop now submits fullscreen presents through `present_submit` instead of `sys_present_full()`, it still tears down its async video subscriptions when `videosvc` rejects the submit path, and the actual flip now drains through the dedicated presenter worker instead of executing inline in the desktop cadence; kernel-owned backend work and final recovery proof still remain
- [~] add evented mode-change / hotplug / backend-failure notifications
: `videosvc` now emits explicit `mode-set-begin`, `mode-set-done`, compatibility `mode-set`, `leave`, and backend `failed/recovered` notifications on the mailbox-backed video stream, and the desktop now consumes those events plus `video`/`input` supervision events to refresh metrics, clamp layout, and redraw immediately when the backend changes; future hotplug is still pending
- [~] move video service off backend-shim steady-state execution
: steady-state `gfx` / `present` / palette / info requests now terminate in the dedicated `videosvc` userland host and the lightweight desktop continuity fast path inside `video.c`; `present` drains through the queued presenter worker, while `mode` / `leave` transitions are forced through the dedicated service task and serialized in service context instead of the generic backend-shim path. The desktop-shell `800x600 -> 1024x768 -> restore` roundtrip is now green under headless QEMU; backend ownership still remains kernel-side
- [ ] define what remains privileged for GPU/MMIO ownership versus what moves into service processes

Implementation to finish Phase D:

1. formalize a two-stage video model
   - desktop/compositor produces frame jobs
   - `videosvc` owns present queue, fences, mode transitions, and backend coordination
2. turn `present submit` into a real queued presenter
   - queue present request with fence token
   - presenter worker drains queue independently of desktop update cadence
   - desktop waits only on fence/event when it truly needs pacing feedback
3. remove heavyweight backend work from desktop cadence
   - no mode-set
   - no large backend copies
   - no backend handoff logic
   - no device health probing in the desktop loop
4. expand video event stream
   - `present-complete`
   - `mode-set-begin`
   - `mode-set-done`
   - `backend-failed`
   - `backend-recovered`
   - future `hotplug`
5. define privilege boundary
   - privileged kernel keeps minimal MMIO/interrupt mediation and protected mappings
   - queue ownership, policy, present scheduling, and telemetry move to `videosvc`
6. add desktop recovery semantics
   - if `videosvc` restarts, desktop keeps state and re-subscribes
   - input stays live while presentation is temporarily unavailable
   - restart should degrade visuals before killing the session

Acceptance for Phase D:

- desktop no longer performs device-progress work directly
- frame submission is queued and fence-based
- `videosvc` restart does not destroy desktop/session state
- backend failures are surfaced as explicit events, not inferred from stalls

Execution slices for Phase D:

1. queue `present submit` behind a dedicated presenter worker
2. move mode-set/handoff/backend-failure handling under `videosvc`
3. make desktop consume only fences/events, not backend progress
4. define the minimal privileged MMIO/interrupt surface and keep the rest in service-space policy

Phase D regression risks:

- desktop repaint cadence appearing smooth in QEMU while fence handling races on `2+` CPUs
- mode-set recovery paths breaking because desktop state is still tied to backend-local assumptions
- over-moving privileged code and leaving MMIO ownership ambiguous

Phase D validation gate:

- `make validate-startx-*` still reaches desktop and keeps the non-`vidmodes-shell` scenarios green
- presentation uses queue/fence semantics
- desktop-session `vidmodes-shell` completes `800x600 -> 1024x768 -> restore` through `videosvc`
- `videosvc` restart degrades visuals without killing input/session state
- mode change notifications remain observable from userland

### Phase E: Storage / Filesystem Async Split

- [ ] replace synchronous request/response-only file path with queued IO requests where it matters
- [ ] add async block IO completion path
- [ ] add writeback worker model so persistence flush does not block unrelated app/UI work
- [ ] move VFS execution/discovery off placeholder/stub bridging toward native executable lookup
- [ ] remove filesystem steady-state dependence on kernel local handler execution

Implementation to finish Phase E:

1. add queued async IO requests where latency matters
   - executable/app loading
   - asset reads
   - file manager directory enumeration
   - large writes and persistence flush
2. add async completion objects for block IO
   - request ID
   - transfer buffer ID
   - completion event
   - timeout/cancel/error result
3. add writeback workers
   - metadata writeback
   - persistence flush
   - delayed dirty-page/file flush policy
   these workers must not block unrelated UI or app reads
4. move executable discovery away from placeholder files
   - give AppFS/VFS native executable metadata and lookup
   - let `lang_loader` resolve real manifests/entries instead of placeholder path discovery
5. shift backend ownership out of kernel local handlers
   - `storagesvc` owns request queueing and completion
   - `fssvc` owns path traversal/open/read/write orchestration
   - backend-shim remains bootstrap/rescue only
6. add foreground-aware priority
   - desktop launch and active app asset loads outrank background scans or writeback

Acceptance for Phase E:

- app launch and asset IO do not block the desktop loop
- persistence flush/writeback no longer stalls unrelated reads or input
- executable discovery no longer depends on placeholder files
- steady-state storage/filesystem work flows through service-owned queues

Execution slices for Phase E:

1. queue app/asset reads first
2. queue writeback and persistence flush second
3. replace placeholder-based executable lookup with native metadata lookup
4. remove steady-state filesystem/storage backend-shim dependence

Phase E regression risks:

- app launch slowing down because async lookup adds queueing without foreground priority
- writeback workers causing hidden ordering bugs in persistence paths
- breaking current command discovery before native executable metadata is fully in place

Phase E validation gate:

- desktop remains responsive while large reads/writes run
- modular app launch still works after placeholder discovery removal
- persistence flush no longer blocks unrelated active foreground work
- storage/filesystem state is observable through service events and queue metrics

### Phase F: Network Async Split

- [ ] replace current control-plane MVP with real packet TX/RX queues
- [~] add socket wakeup/readiness events
: `network` now publishes `status`, `recv`, `accept`, `send`, and `closed` events through a dedicated mailbox-backed event stream, and the desktop consumes those events to refresh network state reactively instead of relying only on periodic polling; true queued TX/RX ownership is still pending
- [~] the network event stream now also publishes backend `rx` / `tx` activity from the virtio compatibility path plus explicit overflow telemetry, giving userland visibility into datapath motion before full extracted TX/RX ownership lands
- [~] desktop async consumers now tear down and re-subscribe their audio/network/video event streams when supervision reports `offline` / `degraded` / `restarted`, and the desktop now applies the same reset path when a `videosvc` `present_submit` fails, reducing stale-subscription behavior across service restarts and submit-path faults
- [~] desktop-side `netmgrd` control actions now run through detached AppFS workers for wifi/ethernet connect, disconnect, autoconnect, forget, reconcile, and startup export, with the desktop keeping only optimistic cache/UI state while network events converge the real result; mailbox-backed event delivery is now in place, but real datapath ownership and per-class worker isolation are still pending
- [ ] add async DNS/DHCP completion flow
- [ ] keep `netmgrd` as policy daemon, not datapath owner
- [ ] remove steady-state dependence on kernel local handler execution for networking

Implementation to finish Phase F:

1. introduce real TX/RX queues
   - receive ring owned by networking service/driver task
   - transmit queue with completion events
   - per-socket readiness fed from service-owned queue state
2. keep `netmgrd` in policy space only
   - Wi-Fi/ethernet selection
   - DHCP trigger
   - reconnection/autoconnect policy
   - saved credentials and profile logic
   it must not own packet movement or socket progress
3. add async DHCP/DNS flow
   - request submission
   - completion event
   - timeout/error event
   - desktop terminal tools observe readiness instead of sleeping/polling
4. remove kernel local-handler steady-state dependence
   - control-plane scaffolding remains only until real datapath is service-owned
   - packet RX/TX, socket accept/recv/send readiness, and link transitions stop terminating in kernel shims
5. expose actionable network telemetry
   - active backend
   - link state
   - lease state
   - DNS state
   - per-socket readiness backlog counters

Acceptance for Phase F:

- socket readiness comes from service-owned TX/RX queues
- DHCP and DNS complete asynchronously with explicit events
- `netmgrd` remains policy-only
- networking no longer depends on backend-shim for normal packet flow

Execution slices for Phase F:

1. land real TX/RX queues and service-owned readiness
2. move DHCP and DNS to explicit async request/completion flows
3. strip datapath ownership out of `netmgrd`
4. remove steady-state kernel local-handler dependence

Phase F regression risks:

- mixing policy-daemon responsibilities with datapath queue ownership again during early bring-up
- socket APIs appearing async while still doing synchronous datapath work underneath
- network restart paths stalling terminal/desktop because readiness queues are not drained correctly

Phase F validation gate:

- link transitions generate events
- socket `recv`/`accept`/`send` readiness is queue-driven
- DHCP/DNS operations complete through explicit async flows
- restarting network service does not freeze shell or desktop

### Phase G: Strict Microkernel Cutover

- [ ] remove backend-shim syscall from steady-state service execution
- [ ] keep kernel-side local handlers only for bootstrap/rescue, or remove them entirely
- [ ] move storage/filesystem/video/input/console/network/audio backend ownership to service processes or narrowly-scoped driver tasks
- [ ] define per-domain crash containment and restart contracts
- [ ] prove that one failed service does not freeze unrelated UI/control loops
- [ ] audit privileged kernel code down to scheduler, VM, IPC, interrupts, supervision, and minimal hardware mediation
- [ ] codify desktop/input primacy in scheduler and supervision policy, not just in docs

Implementation to finish Phase G:

1. remove `backend-shim` from steady-state paths one subsystem at a time
   - storage
   - filesystem
   - video
   - input
   - console
   - network
   - audio
   keep rescue/bootstrap-only compatibility behind explicit boot flags while each cutover lands
2. define crash-containment contracts per domain
   - input failure must not kill desktop state
   - audio failure must not stall input/video
   - network failure must not stall shell/desktop/app loops
   - video failure may blank presentation temporarily but must preserve session state and supervision
3. codify scheduler and supervision policy in code
   - desktop, keyboard, mouse always first
   - lower tiers throttled or killed first under overload
   - restart ordering favors preserving the live desktop session
4. audit privileged code aggressively
   - what stays in kernel: scheduler, VM/memory mediation, IPC, interrupts, supervision, minimal hardware isolation
   - what leaves kernel: subsystem policy, queue ownership, steady-state backend orchestration
5. turn service restart into a normal tested path
   - chaos-style validation for audio/input/network/video restart
   - desktop/session survives without reboot
   - telemetry explains every degrade/recover/restart transition

Acceptance for Phase G:

- `backend-shim` is absent from steady-state desktop/session operation
- service crash/restart is a supported normal path
- one failed service does not freeze unrelated UI/control loops
- privileged kernel scope is small, explicit, and auditable

Execution slices for Phase G:

1. remove steady-state `backend-shim` for the already-prepared domains first
2. keep rescue/bootstrap-only compatibility behind explicit boot flags
3. codify crash containment and restart order in code
4. run destructive restart validation until it is boring and repeatable

Phase G regression risks:

- deleting a compatibility bridge before the replacement path is observable and diagnosable
- rescue/safe-mode accidentally losing its minimal fallback guarantees
- claiming strict microkernel cutover while restart still silently falls back to kernel-local work

Phase G validation gate:

- normal desktop/session usage does not hit `backend-shim`
- safe mode and rescue shell still boot
- one failed service host does not freeze pointer motion, keyboard input, or repaint cadence
- privileged kernel inventory can be listed concretely and defended line by line

## Cross-Phase Rules

These rules apply to every phase and should not be violated for convenience:

- never trade desktop/input continuity for optional subsystem throughput
- every new async queue must have visible telemetry, timeout, and cancel semantics
- every new service restart path must be testable in QEMU before it is called complete
- rescue/safe-mode fallbacks may exist, but normal desktop steady-state must not rely on them
- if a lower-tier worker can wedge the machine, the correct fix is containment and kill/restart policy, not pushing more work back into the desktop loop

## Anti-Patterns To Avoid

- replacing one `poll + yield` loop with another loop hidden behind a helper
- calling a path "async" when it still performs the real backend transaction synchronously before returning
- using the desktop loop as the hidden owner of audio/video/network progress
- keeping `backend-shim` in normal desktop usage while marking a phase done
- routing recovery through reboot/session restart before trying per-service containment

## Recommended Execution Order For Phases A-G

The best practical order is:

1. finish Phase A first
   - without generic task events, waitables, and worker contexts, later phases keep leaking back into poll loops
2. finish Phase B next
   - desktop, keyboard, mouse, and `startx` continuity are the hard UX contract
3. finish Phase D before deep audio/network work
   - presentation separation is the other half of desktop continuity
4. finish Phase C in parallel once B/D boundaries are stable
   - audio must become queue-driven, but it must never outrank desktop/input/video continuity
5. finish Phase E before broad app/runtime claims
   - modular app launch and filesystem discovery need native async ownership
6. finish Phase F after the event substrate and restart policy are proven
   - real networking will stress queueing, telemetry, and supervision
7. use Phase G as the final cutover gate, not as an aspirational cleanup bucket

## Definition Of Done Per Phase

- Phase A is done when bootstrap is pure supervision and all major task classes have auditable event-driven workers.
- Phase B is done when `startx` and desktop survive input restart and interactivity priority is enforced in code.
- Phase C is done when `audiosvc` owns playback/capture queues and UI no longer participates in audio progress.
- Phase D is done when `videosvc` owns present queue/fences and desktop no longer drives backend work directly.
- Phase E is done when IO-heavy launch/read/writeback paths are queued and executable discovery is native.
- Phase F is done when packet flow, socket readiness, DHCP, and DNS are all service-owned and async.
- Phase G is done when steady-state desktop usage no longer depends on backend-shim or destructive fallbacks.

## Concrete File Ownership By Phase

This section maps each phase to the files most likely to change first. It is intentionally pragmatic and should be used to keep patches focused instead of spreading one phase across the whole tree without need.

### Phase A likely files

- `kernel/process/scheduler.c`
- `headers/kernel/scheduler.h`
- `kernel/syscall.c`
- `headers/include/userland_api.h`
- `headers/userland/modules/include/syscalls.h`
- `userland/modules/syscalls.c`
- `userland/bootstrap_init.c`
- `userland/bootstrap_hosts.c`
- `userland/modules/lang_loader.c`

Phase A ownership notes:

- keep event primitives and lifecycle streams centered in the scheduler/syscall layer
- keep supervision ownership in `bootstrap_init.c` and host lifecycle ownership in `bootstrap_hosts.c`
- do not mix generic task-host work with audio/video/network specifics yet

### Phase B likely files

- `kernel/drivers/input/input.c`
- `kernel/microkernel/input.c`
- `kernel/process/scheduler.c`
- `userland/applications/desktop.c`
- `userland/bootstrap_hosts.c`
- `kernel/microkernel/service.c`
- `tools/validate_modular_apps.py`

Phase B ownership notes:

- kernel input code should only capture and queue raw device events
- service ownership and restart behavior belong in `kernel/microkernel/input.c` and `kernel/microkernel/service.c`
- desktop-side recovery, resubscribe, and transient-state cleanup belong in `userland/applications/desktop.c`

### Phase C likely files

- `kernel/microkernel/audio.c`
- `headers/kernel/microkernel/audio.h`
- `lang/apps/audiosvc/audiosvc_main.c`
- `userland/modules/utils.c`
- `userland/applications/audioplayer.c`
- `kernel/microkernel/service.c`
- `tools/validate_audio_stack.py`

Phase C ownership notes:

- queue ownership and completion semantics should move toward `audiosvc_main.c`
- generic helper consumers may observe audio events, but they must not become hidden datapath owners

### Phase D likely files

- `kernel/microkernel/video.c`
- `headers/kernel/microkernel/video.h`
- `kernel/drivers/video/video.c`
- `userland/applications/desktop.c`
- `kernel/microkernel/service.c`
- `tools/validate_gpu_backends.py`
- `tools/validate_modular_apps.py`

Phase D ownership notes:

- backend privilege boundaries must be documented while code moves
- compositor/session logic should stay in desktop code; present queue and backend transitions belong in `video.c`

### Phase E likely files

- `kernel/microkernel/storage.c`
- `kernel/microkernel/filesystem.c`
- `headers/kernel/microkernel/filesystem.h`
- `userland/modules/fs.c`
- `userland/modules/lang_loader.c`
- `userland/applications/filemanager.c`
- `kernel/microkernel/service.c`

Phase E ownership notes:

- executable discovery cleanup likely spans `fs.c` and `lang_loader.c`
- queued IO ownership belongs first to storage/filesystem service layers, not to desktop code

### Phase F likely files

- `kernel/microkernel/network.c`
- `headers/kernel/microkernel/network.h`
- `lang/apps/netmgrd/netmgrd_main.c`
- `lang/apps/netctl/netctl_main.c`
- `userland/applications/desktop.c`
- `kernel/microkernel/service.c`

Phase F ownership notes:

- `netmgrd` must stay policy-only as extraction advances
- queued datapath and readiness logic belongs in `network.c`, not in desktop helpers

### Phase G likely files

- `kernel/microkernel/service.c`
- `kernel/microkernel/storage.c`
- `kernel/microkernel/filesystem.c`
- `kernel/microkernel/video.c`
- `kernel/microkernel/input.c`
- `kernel/microkernel/network.c`
- `kernel/microkernel/audio.c`
- `kernel/process/scheduler.c`
- `kernel/syscall.c`

Phase G ownership notes:

- this is where `backend-shim` steady-state removal should happen domain by domain
- avoid mixing privilege audit with user-facing UX work in the same patch unless the dependency is direct

## Minimal Commit Sequence Per Phase

Each phase should be landed as a short sequence of reviewable commits rather than one large migration patch.

### Phase A commit sequence

1. scheduler/task lifecycle event ABI
2. host/supervisor subscription and logging
3. generic launched-task context for modular foreground apps
4. bootstrap enforcement as supervision-only

### Phase B commit sequence

1. input-service restart/resubscribe handling
2. removal of normal desktop-mode direct-driver fallback
3. scheduler priority hardening for desktop/keyboard/mouse
4. restart-smoke validation for desktop continuity

### Phase C commit sequence

1. playback queue ownership into `audiosvc`
2. capture queue and completion events
3. backend progress completion under service ownership
4. mixer/default-route policy extraction

### Phase D commit sequence

1. present queue and fence worker
2. desktop-side fence/event consumption
3. mode-set/backend-failure event expansion
4. privilege-boundary cleanup for video backend ownership

### Phase E commit sequence

1. async read path for app/assets
2. writeback worker path
3. native executable lookup replacing placeholders
4. steady-state storage/filesystem backend-shim removal

### Phase F commit sequence

1. service-owned TX/RX readiness queues
2. DHCP/DNS async completion path
3. `netmgrd` narrowed to policy-only role
4. steady-state network backend-shim removal

### Phase G commit sequence

1. domain-by-domain steady-state bridge removal
2. crash containment/restart policy codified
3. destructive restart validation matrix
4. privileged kernel audit and final cleanup

## What Must Not Be Marked Done Early

To keep the migration honest, the following claims are explicitly forbidden until they are literally true in code and validation:

- do not mark Phase B done while desktop still depends on direct-driver steady-state fallback
- do not mark Phase C done while audio progress still needs UI cadence
- do not mark Phase D done while desktop still owns heavyweight present/backend work
- do not mark Phase E done while executable discovery still depends on placeholder paths
- do not mark Phase F done while packet flow still resolves through kernel local-handler steady-state execution
- do not mark Phase G done while normal desktop usage still hits `backend-shim`

## Checklist For "Microkernel For Real"

The system should only be called a real microkernel in the strict sense when all items below are true together:

- [ ] desktop input, render, audio startup, and app launch paths are all asynchronous and queue-driven
- [ ] no desktop/UI path depends on synchronous device writes to make forward progress
- [ ] storage/filesystem/video/input/console/network/audio no longer rely on backend-shim steady-state execution
- [ ] service restarts are normal and recoverable, not a path that requires direct kernel fallback to preserve usability
- [ ] audio has real async playback and capture with completion/wakeup events
- [~] network has first socket readiness/event semantics (`status`, `recv`, `accept`, `send`, `closed`) even though the full extracted NIC datapath is still pending
- [ ] video has explicit present queues/fences and no desktop-owned hot path into device progress
- [ ] input is event-published by a service boundary, not preserved through permanent direct-driver syscall escape hatches
- [ ] there is a wait/signal primitive richer than "poll + yield + sleep"
- [ ] each major task class runs in an independent async worker/thread context that emits auditable events
- [ ] shell, desktop, and foreground userland apps all execute outside the bootstrap/main thread
- [ ] compatibility stubs are either gone or clearly isolated to rescue/boot mode
- [ ] native USB runtime storage and audio backends exist
- [ ] failure of one service host does not freeze pointer motion, keyboard input, repaint cadence, or unrelated services
- [ ] scheduler policy explicitly protects desktop, mouse, and keyboard responsiveness over optional background work

## Checklist For "System Perfect"

This is the practical quality bar on top of the stricter architectural one:

- [ ] `make run`, `make run-debug`, `run-azalia`, and the hardware-profile targets all boot to a responsive desktop on `2+` CPUs
- [ ] startup sound works or fails fast without ever freezing input/video
- [ ] keyboard, mouse, video, and audio remain responsive while services degrade/restart
- [ ] desktop stability remains the top priority independent of processor count or backend quality
- [ ] HDA/AC97/USB audio backend selection is deterministic and explained in telemetry
- [ ] every major service exports actionable health/progress/error state
- [ ] the desktop can survive service restart of audio, input, network, and video without reboot
- [ ] the validation matrix covers QEMU plus the known real laptop classes already tracked in-tree

## Document Reality Audit

The statements below classify how much of this document is literally true in the current tree.

### Verified As Implemented In The Current Tree

- Phase 2 boot-chain claims are materially true: `MBR -> active FAT32 partition -> VBR -> stage2 -> KERNEL.BIN` exists and is validated in QEMU.
- Phase 3 disk-layout claims are materially true: the image is partitioned, FAT32 is used for the boot volume, and the raw data/AppFS partition is built and consumed consistently.
- Phase 4 ATA/AHCI claims are materially true: both backends exist, are selected behind one block-device abstraction, and pass the current headless smoke/boot matrix.
- The `init -> userland.app` handoff is materially true on the current IDE/AHCI QEMU paths.
- Phase 6 validation infrastructure is materially true: `make validate-phase6` produces `build/phase6-validation.md` and the matrix currently passes.

### True Only As A Migration Boundary, Not As Final Extraction

- Phase 1 service-boundary claims are only partially true in the strong microkernel sense: the syscall/IPC/service boundaries exist, but much of the concrete backend logic for `storage`, `filesystem`, `video`, `input`, and `console` still lives in kernel-side local handlers.
- Phase 5 initial user-space service claims are true for transport, lifecycle, and supervision, but not yet for backend ownership. The service hosts are real user-space tasks, but they still call back into preserved kernel handlers through the backend-shim syscall.
- USB compatibility is true only for BIOS boot/loading strategy. Native runtime USB block I/O is still missing.

### Not Yet True If Read Literally As End-State Claims

- VibeOS is not yet a fully extracted service-oriented microkernel in the strict sense. The repository is currently a hybrid system with real service boundaries plus compatibility bridges back into in-kernel handlers.
- `init` has started moving toward supervisor-only behavior by launching separate built-in shell/desktop hosts, but modular AppFS apps still do not get independent task contexts yet.
- `network` is not yet a real networking stack/service. It is currently a query/capability stub with request ABI scaffolding.
- `audio` is not yet a real playback/capture service. It is currently a query/control stub with no real DMA/ring/mixer backend.
- The boot loader is not yet a fully general FAT32 loader. It is a robust current-path loader tuned to the current image layout.

## Stub / Bridge Inventory

This is the current list of migration-relevant stubs, bridges, and fallbacks that still need proper implementation.

### User-Space Services With Remaining Kernel-Owned Backends

- `storage` service host:
  current user-space process exists, but the request body is still ultimately handled by the kernel-side local storage handler through `sys_service_backend()`.
- `filesystem` service host:
  current user-space process exists, but file operations still terminate in the preserved kernel-side local handler.
- `video` service host:
  current user-space process now handles steady-state requests directly without `sys_service_backend()`, but rendering/present backend work is still executed synchronously inside kernel-owned `video.c` logic.
- `input` service host:
  current user-space process now handles steady-state request/reply directly, but keyboard/mouse publication is still kernel-owned and bootstrap/critical rescue fallback still exists.
- `console` service host:
  current user-space process exists, but the console backend still resolves to the preserved kernel-side local handler.
- `network` service host:
  current user-space process exists, but its backend is still a stub local handler.
- `audio` service host:
  current user-space process now handles steady-state control/read/write requests directly without `sys_service_backend()`, but async queue ownership, completion accounting, and backend execution still live in kernel-owned audio state.

Relevant code:

- `userland/bootstrap_service.c`
- `kernel/microkernel/service.c`

### Kernel-Side Service Stubs

- `kernel/microkernel/network.c`
  - marks the service as `QUERY_ONLY`
  - reports socket families/types as capability metadata only
  - returns failure for real socket/bind/connect/send/recv/setsockopt/getsockopt operations
- `kernel/microkernel/audio.c`
  - reports a device called `stub`
  - marks the service as `QUERY_ONLY`
  - has parameter/status bookkeeping only
  - returns failure for real audio read/write and mixer operations

### Direct Fallback Paths Still In Use

- `kernel/microkernel/input.c`
  - still falls back to direct keyboard/mouse driver reads when service transport is degraded
- `kernel/microkernel/service.c`
  - still falls back from process transport to local handlers when transport is degraded
- `userland/bootstrap_init.c`
  - still retains the built-in shell fallback if `userland.app` is missing or returns

### Shell / Userland Stubs Still Present

- `userland/modules/busybox.c`
  - weak fallback implementations still exist for:
    - CPU/APIC topology helpers
    - heap/RAM reporting helpers
    - `vibe_lua_main`
    - `sectorc_main`
    - desktop/editor launch entrypoints
  - `startx`, `edit`, and `nano` are still unavailable in the boot app unless an external app is found
  - `lua` and `sectorc` still report `indisponivel` when neither an external app nor a linked runtime is present
  - built-in `uname` still returns a fixed string (`VIBE-OS`) rather than a full compat-style implementation

### VFS Execution Discovery Stubs

- `userland/modules/fs.c`
  - bootstraps empty placeholder files in `/bin`, `/usr/bin`, and `/compat/bin` for command discovery
- `userland/modules/lang_loader.c`
  - still treats those placeholder paths as runtime stubs so the shell can discover external apps before native VFS execution exists

This is acceptable as a migration bridge, but it is not the final shape for native executable discovery/execution.

### Core Kernel No-Op Stubs Still In Tree

- `kernel/memory/paging.c`
  - `paging_init()` is still a no-op placeholder
- `kernel/memory/memory_init.c`
  - `memory_subsystem_init()` is still only a thin stub wrapper around lower-level init calls
- `kernel/hal.c`
  - `hal_init()` is still empty

These are not the main blockers for the current microkernel migration path, but they are real stubs and the document should not pretend otherwise.

## Stub Implementation Priority

Recommended order for replacing the current migration stubs with proper implementations:

1. Native USB mass-storage runtime backend
2. Remove the backend-shim dependency for `storage`
3. Remove the backend-shim dependency for `filesystem`
4. Remove the backend-shim dependency for `video`, `input`, and `console`
5. Implement a real extracted `network` backend (preferably a narrow QEMU-friendly NIC first)
6. Implement a real extracted `audio` backend (DMA/ring/mixer path)
7. Replace VFS executable placeholder files with native executable metadata/lookup
8. Replace the built-in `uname` stub and boot-app-only `startx`/editor fallback gaps with proper external apps or linked runtimes
9. Generalize the FAT32 `stage2` loader beyond the current contiguous/linear strategy
10. Replace remaining kernel no-op stubs such as paging/HAL initialization as part of core cleanup

## First Implementation Slice

The first slice implemented in-tree is intentionally modest:

1. Introduce structured message envelopes.
2. Introduce a service registry owned by the kernel.
3. Extend `process_t` so services can be represented explicitly.
4. Keep the existing monolithic behavior while creating the interfaces needed for extraction.

This does not finish the migration, but it starts replacing ad-hoc coupling with explicit microkernel-oriented primitives.

## Latest Completed Slice

- External `startx` launch now enters as a real desktop-class task instead of a generic app-runtime launch, and bootstrap waiters now follow desktop-class lifecycle events for that session handoff.
- `videosvc` steady-state presentation is now queue-backed: fullscreen submit returns a fence token, `video-present` drains the present queue, and `mode-set` / `leave` transitions no longer go through the old backend-shim steady-state path.
- Desktop-class video control requests now reserve a longer request budget for legitimate runtime mode transitions, so `videosvc` control-plane handoffs are not treated as immediate transport failures.
- The service request path now defers unexpected replies locally while a caller waits on a specific service response, avoiding the self-requeue livelock that could starve longer-running IPC transactions.
- The desktop validation path now reaches `desktop: session ready` reliably again through the stable desktop shortcut flow used by `validate_modular_apps.py`.
- The service request wait loop now blocks on the remaining request budget instead of re-arming 1-tick IPC timeouts, avoiding the timeout/resume wedge that previously stranded long `videosvc` mode-set replies in the caller queue.
- The `vidmodes` smoke now completes the full migration-safe sequence end-to-end: reassert the active desktop mode, switch through `1024x768`, restore `800x600`, and pass the official `vidmodes-shell` modular validation scenario.
