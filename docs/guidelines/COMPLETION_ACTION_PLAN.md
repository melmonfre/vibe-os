# Plano de Acao para Finalizar a Implementacao Completa do VibeOS

last: P1 CONCLUIDA (commit b23ac3fc5): boot stall corrigido (stack de boot movida para 0x800000 + cmov/i586 + ioapic pin+2 + EOI de IRQ espoliador); validate-phase6-boot-smoke 6/6 PASS
doing: P2/P4 parciais (ver checkboxes); open: SMP2/4 stall em 'fs: sys_storage_load' (race de wakeup no AP)
next: P4 restante (wakeup AP) e P3 camada 1 (NIC abstraction)
location: docs/guidelines
state: active

Este plano substitui a leitura solta de `docs/ACTION_PLAN.md` e `docs/FINALIZATION_EXECUTION_PLAN.md` como fonte de prioridades. Acoes concluidas aqui devem ser marcadas nos dois e neste plano. Base: verificacao real do codigo (auditoria por subsistema, build completo, boot em QEMU com gdb), nao apenas alegacoes de docs.

## Regras deste plano

- Regra universal (universal-rule.md) acima de tudo: kernel nunca quebra userland; ABI so cresce (append-only, versionada ou com fallback).
- Nada entra como "concluido" sem prova reproduzivel (`make validate-*`, relatorio em `build/`, log serial, ou sessao gdb).
- Cada prioridade tem escopo coeso de uma lane; tarefas marcadas com `[!]` tocam ABI/boot contract e exigem decisao da mantenedora.

## Estado real verificado (2026-10-11)

| Subsistema | Estado | Evidencia |
| --- | --- | --- |
| Build | verde | `make` rc=0 apos fixes do merge (50387683a); gera boot.img completo |
| Boot chain MBR->stage2->kernel | funciona ate `[8] memory` | serial QEMU; stall documentado abaixo |
| Kernel bring-up | stall confirmado | gdb: EIP `0x251df001`/`0x25ab d001` dentro das arenas de app; bt: `kernel_entry -> kernel_video_refresh_backend+43 -> kernel_video_activate_backend` |
| Armazenamento ATA/AHCI | drivers reais com waits bounded | ahci.c, ata.c; nunca alcancados (stall anterior) |
| Rede virtio IPv4 (client) | funciona no papel e no codigo | network.c DHCP/ARP/ICMP/UDP/TCP reais; nunca alcancado em runtime hoje |
| Rede e1000/rtl8139/wifi | drivers mortos ou fachada | zero callers; scans hardcoded; `ping localhost` fabricado |
| Audio | backends reais, QEMU-validated | HDA/AC97/pcspkr/uaudio em audio.c; 0 linhas da matriz HW preenchidas |
| Video/GPU | pipeline + BGA validados | i915/radeon/nouveau com stage/commit sem validacao em silicio real |
| SMP | bring-up real, scheduler nunca liga | `smp_scheduler_enable()` zero callers; IOAPIC roteia tudo pro BSP |
| Shell/ksh/terminal | funciona | ksh via fork/exec no terminal; busybox ~35 builtins |
| Desktop/apps | funciona (monolito) | desktop.c 11k linhas; icon theme ~90% do plano |
| Runtimes js/ruby/python | STUBS no build | placeholder banners; quickjs/mruby/micropython vendors desativados no Makefile |
| craft/doom | rodam; craft partial | world.c upstream reduzido a 77 linhas; curl=501 |

---

## Prioridade 1: devolver o boot ate `init` (bloqueia tudo)

Causa raiz ja localizada — nao e mais bisect cego:

- [x] Reproduzir o stall: `make run-headless-debug` serial para em `app arenas mapped` + budget de ticks (8 prints); `make debug` + `gdb build/kernel.elf` `target remote :1234`; `info symbol` nos enderecos de retorno prova `kernel_video_activate_backend` como saltador para dentro de VA de arena
  - hipotese primaria (do diagnostico): heap do kernel escolhido por maior range livre de VA **sobrepoe as arenas 0x10000000-0x39000000**; tabela/ops do backend de video ou função ponteiro le de memoria que passou a apontar para fisica da arena; `mtrr: WC size must be power of 2` + `framebuffer wc unavailable` no mesmo ponto de corte pede auditar `mtrr_set_wc` sobre range nao-potencia-2
- [x] Invariante resolvida sem mudar ABI: ou o heap do kernel exclui explicitamente o intervalo VA das arenas, ou as arenas ganham VA acima do heap — `headers/kernel/bootinfo.h` e `docs/memory_map.md` declaram o contrato; qualquer mudanca de layout e ABI de boot (mantenedora decide)
- [x] `kernel_video_refresh_backend` para nao ativar backend com estado nao mapeado (ou re-ordenar: refresh depois de storage/scheduler)
- [x] Canal debugcon devolvido perdido no merge: `kernel_early_post` perdeu `outb 0xE9` (entry.c ~95) — restaurar e documentar codigos de estagio
- [x] Morte silenciosa -> ruidosa em morte ruidosa: `kernel_panic` e o handler `#UD` kernel-mode devem emitir em serial antes do `hlt`-loop (panic.c, idt.c ~151-183)
- [x] `tools/validate_phase6.py` atualizado (`storage: using ata backend` nao existe mais no kernel); atualizar para os reais (`[step] storage: probing ata`, `ata: start=`, `memory: done`)
- [x] Prova: `make validate-phase6-boot-smoke` 6/6 PASS (2026-10-11, TCG) + serial ate `init: entered builtin entry` e `userland.app: shell start` em TCG

## Prioridade 2: fechamento honesto do storage

- [x] `kernel/drivers/audio/hda.c` removido (paralelo morto, zero callers). `kernel/drivers/network/virtio_net.c` ainda duplica a virtio privada de network.c — decisao adiada para P3 (NIC abstraction)
- [x] `storage: ata OK`/`ahci OK`/`nvme OK`/`none` espelhados no serial (storage.c)
- [ ] Validar `make legacy-data-img` e persistencia no boot real (storage smoke test do init, que hoje nunca roda)

## Prioridade 3: rede real completa

Camada 1 — infraestrutura:

- [ ] Definir NIC abstraction [!] (ja esbocada em e1000.c:573-610): `send_frame/attach_mac/link_status`; network.c deixa de ter virtio privada dentro do servico e passa a falar com drivers registrados
- [ ] Ligar e1000 e rtl8139 de verdade: init a partir de entry.c, corrigir pressao VA==PA de DMA (e1000.c:274-283), consertar corrida do IRQ de RX (e1000.c:347-375 consome o proprio pacote)
- [ ] RX orientado a IRQ (hoje 100 Hz polling, ring de 8 slots) e TX com fila (hoje 1 frame em voo)

Camada 2 — stack:

- [ ] ARP: cache multi-entrada + timeout + responder por ARP (hoje ninguem consegue alcancar o VibeOS)
- [ ] ICMP: responder echo-request (be ping-able) + loopback real de IPv4 (hoje `ping localhost` imprime fabrica — ping.c:51-59)
- [ ] TCP: backlog >1, sockets >8, timers corretos (RTT/RTO, retransmissao hoje e retry cego a cada 50 ticks)
- [ ] DNS: resolver em netdb.c ler o servidor do DHCP (hoje hardcoded 10.0.2.3) + `/etc/resolv.conf`-equivalente
- [ ] Capacidades honestas: `supported_families` nao anunciar AF_INET6 inexistente; `CAPS_REAL_PACKET_DATAPATH` ligado a NIC com datapath, nao a presenca de PCI class (network.c:3063-3074)

Camada 3 — WiFi (deixar de ser fachada):

- [ ] Panel/UI: ate 802.11 existir, scan/conectar reportam "nao disponivel" — hoje scans hardcoded (network.c:3096-3110) e connect escreve IP inventado (network.c:2813-2856); docs/hw_validation.md e NETWORK_AUDIO_PANEL_PLAN.md afirmam "WPA2 funcional" sem suporte no codigo
- [ ] `wifi_data_plane.c` nao fala com nenhum chip real (barra fixa em bus:dev 0:0:0) — marcar como experimental e nao inicializar no boot de producao
- [ ] Caminho real incremental: 802.11 auth/assoc + EAPOL com encapsulamento LLC/SNAP + RNG real nos nonces (wifi_wpa2.c hoje usa formulas deterministas) + instalar TK/GTK num caminho CCMP (nao existe criptografia de dados 802.11 no kernel)
- [ ] Manter `wifi_wpa2.c` (criptografia PBKDF2/PTK/4-way genuine) plugada quando o plano de frames existir

Camada 4 — ferramentas/apps:

- [ ] ifconfig/route/netstat lendo tabela real; `host`/`dig` usando o resolver de verdade; curl/wget com HTTP via stack propria (links2 HTTP engine ja existe como base)
- [ ] Validar com QEMU user-net: DHCP bound, ping gateway ida-e-volta, HTTP GET via links2; expandir `tools/validate_network_surface.py` alem dos checks de UI

## Prioridade 4: SMP de verdade

- [ ] Chamar `smp_scheduler_enable()` depois do bring-up dos APs (kernel/smp.c:464 — hoje zero callers; APs ficam em `sti; hlt` para sempre)
- [x] LAPIC timer por-CPU no AP (calibrado contra PIT, vetor 0x83, stub smp_timer_stub); AP dorme em hlt e preempcao vem do tick
- [ ] Handler de IPI de reschedule (smp.c:484 so faz EOI; hlt+tick cobre, mas reschedule imediato ainda nao)
- [ ] IOAPIC: rotear IRQs por afinidade em vez de tudo no BSP (ioapic.c:526)
- [ ] Respeitar o gate existente: `BOOTINFO_FLAG_EXPERIMENTAL_SMP` existe e e toggle no stage2, mas **nenhum codigo do kernel o le** — implementar a leitura ou atualizar AGENTS.md/smp.md para a realidade [!]
- [x] `tools/validate_smp.py` reescrito (marcadores reais, espera todos, timeout configuravel). fallback-no-toggle PASS; smp2/smp4: bring-up markers PASS, mas stall em `fs: sys_storage_load` (race de wakeup no AP — ver P4 open abaixo)
- [ ] Retornar `QEMU_RUN_SMP` default para 2 (Makefile:94, item aberto do FINALIZATION) e provar `make run` responsivo em 2 CPUs
- [ ] Hardware real: documento de campanha T61/T400 com trampoline/SIPI rastreados (sem isso, README segue honesto em "SMP em estabilizacao")

### P4 open (evidencia 2026-10-11)

- [x] Causa raiz do #UD no AP corrigida (ordem do TSS): `smp_ap_entry` rodava `gdt_init()` antes de `local_apic_init()`, e `kernel_cpu_index()` (que le o APIC ID local) retornava 0 no AP -> AP fazia `ltr` do TSS do BSP e zerava o esp0 do BSP; os dois CPUs compartilhavam um TSS e as transicoes user->kernel corrompiam stacks (evidencia gdb: #UD com eip=3/7 e CS valendo endereco de stack). Apos inverter a ordem: nenhum #UD, todos os marcadores de bring-up do smp2 passam.
- [x] Lost-wakeup na janela enqueue->block corrigida: `scheduler_block_current_ex` aborta quando um `complete_wait` inline marcou SIGNALED durante a janela, e o wait loop do `kernel_waitable_wait_timeout` consome o `pending_signals` restante no retry; `wait_result` agora e limpo no inicio de cada wait (e no wrapper legacy) para nao abortar de forma falsa.
- [x] Aliasing heap<->arena eliminado: as arenas agora sao backed pelo proprio VA window (`app arena [10000000, 39000000) p=10000000/14000000/38000000` — identity preservado); no fallback de placement a janela sombreia o heap e passa a ser reservada (`reserved_ranges[3]`). Terra-mining do P1 fechado sem mudar ABI de boot.
- [x] Telemetria de deadlock paga o primeiro dividendo: panic de spinlock agora imprime `lock=0x%x cpu=%d` (identificou `g_scheduler_lock` no teste abaixo).
- [x] Alocacao de pid agora e atomica (`process_alloc_pid` com `lock xaddl`; 3 sites: process_create, fork, thread). `g_next_pid++` era um RMW nao-atômico — bug SMP real (pids duplicados ligariam fila ipc/registro de launch na task errada). UP sem regressao: validate-phase6-boot-smoke 6/6 PASS com o fix.
- [ ] smp2 (flag ON) ainda nao chega ao shell — a lista de processos corrompe sob SMP, e agora com evidencia de que NAO e pid-duplicacao: o AP alterna entre pid=1 e um no fantasma `pid=4885 kind=4` (kind 4 nao existe no enum; apenas 3 tasks foram criadas) — ou seja o bloco heap do host (pid=1) esta sendo pisado por outro dono. Watchpoint de HW no campo `next` pegou UMA escrita (valor `0x12`, pc ambiguo `kernel_irq_restore+7` — TCG reporta proximo boundary). Suspeitos remanescentes: overflow de buffer vizinho no heap (cwd[256]/signal_actions[32]/fds[16] sao areas largas no fim do process_t), stack-overflow do boot stack do AP (AP_STACK_SIZE=4096 com schedule_frame+IRQ aninhada), ou double-alloc no free-list. Teste controlado AP_STACK_SIZE 4K->16K NAO corrigiu (piorou de forma diferente: traces com `cpu=174063` e `old=2065172/22`, #UD em `eip=0x1f8314`=`&g_scheduler_lock` com CS de usuario) — a corrupcao alcancar variaveis globais do proprio scheduler aponta para escrita selvagem ampla (talvez DMA/transfer ou buffer de mensagem com tamanho nao-confiado), nao overflow de stack de AP; stack revertido para 4096. Proximo passo concreto: guarda de integridade no scheduler (validar `pid>0 && kind<=2 && state<=3` e alinhamento de `next` durante walk em `scheduler_add_task`/timeout-hook com trace de 1 tiro mostrando o no ofensor + endereco) + auditoria dos caminhos de escrita em heap com tamanho vindo de payload ipc (`mk_transfer_copy_to`, handlers de storage/fs com `sector_count` do cliente).
- [ ] Nota de telemetria: `g_timer_trace_budget` esta de novo em 8 prints (edit antigo perdido) — subir para 64 quando for diagnosticar boot lento.
- [ ] UP nao regressou: `validate-phase6-boot-smoke` 6/6 PASS com esses fixes; smoke UP atinge `shell: ready`.
- [ ] smp4: bring-up dos 3 APs nao fecha (`smp: online cpus=4/4` ausente) — o boot ainda assim avanca (degradado para os CPUs que online-aram) e chega no desktop (`desktop.app: launch startx`, tick=918). Provavel corrida no caminho INIT/SIPI com multiplos APs em paralelo (g_smp_boot_lock segura o BSP, mas `smp_wait_for_cpu` + retry resetam `g_smp_started_cpus = i` por indice). Precisa de trace do trampoline stage por AP.

## Prioridade 5: audio fechamento hardware

- [ ] HDA capture: azalia hoje so playback (ops sem read; caps sem CAPTURE) — implementar input stream + BDL de captura
- [ ] IRQ compartilhado INTx em HDA/AC97 (audio.c:5363-5373 assume handler unico)
- [ ] Preencher de fato a `AUDIO_HARDWARE_VALIDATION_MATRIX.md` (HW-01..HW-20) — hoje toda linha "nao testado"; ate la, docs/hw_validation.md nao pode dizer "Validated"
- [ ] uaudio (USB audio) campaign de validacao em hardware

## Prioridade 6: video/GPU fechamento

- [ ] Decisao de default [!]: Makefile ja liga `INTEL_I915_EXPERIMENTAL_COMMIT=1` por default, VIDEO_BACKEND_REWRITE_PLAN.md prega "0 seguro" — um dos dois vence por decisao escrita
- [ ] Campanha em silicio real: i915/radeon/nouveau com snapshot/rollback provado; portar 1 backend de hardware real sobre a estrutura (ultimo `[ ]` do plano de video)
- [ ] Atualizar hw_validation.md e VIDEO_BACKEND_REWRITE_PLAN.md contra o codigo real (nouveau hoje comita PCRTC com rollback; doc fala "probe-only")

## Prioridade 7: runtimes de linguagem deixam de ser stub

- [ ] quickjs: reativar bloco comentado (Makefile:805-828) e `quickjs_wrapper.c`; app `js` real em vez do eco de placeholder
- [ ] micropython e mruby: mesma coisa, um por vez, com heap/stack por classe de app respeitando MEMORY_ADAPTIVE_RAM_POLICY
- [ ] Java: `java`/`javac` caseiros (VJ01) — registrar como experimental honesto ou apontar caminho de substituição; nao prometer JVM
- [ ] Provar: `make validate-modular-apps` com hello/js/ruby/python/lua/sectorc rodando REPL/programa real

## Prioridade 8: userland e apps acabativa

- [ ] MicroUI: ou desktop passa a consumir (`desktop_new.c` nao esta no build) ou a lib sai do runtime — hoje compila e nao serve a ninguem
- [ ] Ramo orphan remover/ligar: `shell_bsd.c` + `applications/ported/ksh` fora de todo source list
- [ ] craft: restaurar `upstream/src/world.c` real (77 linhas hoje contra upstream completo) e streaming de chunks; curl http via stack propria apos P3; documentar que texturas hoje vem de assets empacotados
- [ ] links2: entrar no catalogo/`config/app_catalog.tsv` e imagem, ou mover para fora da area de runtime do OS
- [ ] vibe-browser: hoje e Otter Qt5 pristine sem integracao nem regra de build — status explicito "referencia vendored" ou engine proprio minimo (Qt nao e viavel aqui)
- [ ] Icon theme: fechar o plano (SVG/XPM, heranca hicolor, cache em disco, cursor themes)
- [ ] Fontes: stack de fonte escalavel ou multi-tamanho de bitmap (fonte atual: 1 bitmap 6x8 no shim do syscall) — trava legibilidade de toda a UI
- [ ] ABI de app: header emitido carimba `VIBE_APP_ABI_VERSION 1` com struct de 44B (ABI2 no doc) — stamping e branching no loader; decisao [!] de versao

## Prioridade 9: verdade documental (trava de confianca)

- [ ] FINALIZATION_EXECUTION_PLAN.md:15-21 e hw_validation.md dizem "validado em hardware real" sem nenhuma linha preenchida na matriz — trocar por estado real com evidencia, mantendo o padrao "Limitacoes honestas" do README
- [ ] NETWORK_AUDIO_PANEL_PLAN.md "rede wifi real funcional" e ACTION_PLAN/AGENTS.md "SMP gated por flag" — corrigir contra o codigo
- [ ] docs/README.md: tabela Project Status atualizada com as linhas deste plano
- [ ] `make` deve sair sem warnings: alvo `build/libksh.a` declarado duas vezes (Build.compat.mk:134 vs Build.ksh.mk:38) — dedup; varrer os 229 TODO/stub/n.i. do grep inicial e converter cada um em: feito, bug em KNOWN_BUGS.md, ou linha neste plano
- [ ] `universal-rule.md` ganha um paragrafo novo so se nascer contratu — registrar decisoes em docs/guidelines como DECISIONS pontuais

## Definicao Pratica de Acabou (implementacao completa)

- [ ] `make` limpo, rc=0, sem warnings
- [ ] `make run` (SMP=2) e todos os perfis `run-headless-*` chegam em desktop com ksh no terminal sem `hlt`-loop
- [ ] `make validate-phase6` 100% verde (matriz CPU completa)
- [ ] `make validate-abi-compat-surface` e `validate-abi-final` verdes com inventario batido
- [ ] Rede: ping bidirecional, DHCP real, DNS do DHCP, HTTP via curl/links2 em QEMU user-net
- [ ] Storage: AHCI e ATA em QEMU; persistencia smoke no init; matriz HW de audio/video preenchida onde o hardware existe
- [ ] SMP: 2 CPUs com trabalho distribuido (taskmgr mostra 2 cores ativos) em QEMU e em 1 notebook real
- [ ] Runtimes: js/ruby/python/lua/sectorc com benchmark hello-loop + programa nao-trivial cada
- [ ] Docs: zero afirmacao de "funcional/validado" sem linha de evidencia no relatorio `build/` correspondente
- [ ] README.md "Ainda em fechamento" vazio ou reduzido ao que e honestamente futuro (ex.: UEFI, IPv6, GPU accel 3D)

## Ordem de execucao e dependencias

P1 (boot) e o unico gate: P2/P3/P4/P5/P6 dependem de o kernel chegar em `init`. Dentro do pos-boot, a ordem recomendada e P2 (storage, pequeno e fecha o smoke do init) -> P3-camada1/2 (rede infra + stack) -> P4 (SMP) em paralelo com P5/P6 (audio/video campanhas) -> P7 (runtimes) -> P8 (userland acabamento) -> P9 roda continuo como trava de honestidade em cada PR.

Cada item concluido atualiza este plano + ACTION_PLAN + FINALIZATION no mesmo commit, com caminho de prova (relatorio em `build/` ou comando reproduzivel).
