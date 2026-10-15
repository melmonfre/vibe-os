# Plano de Fechamento Definitivo — VibeOS

last: 2026-10-15 — consolidacao de TODOS os planos abertos do repo num unico programa de execucao
doing: backlog definitivo, organizado em ondas, com regra anti-stub e prova obrigatoria
next: Onda 0 (fundacao) — ver secao "Ondas"
state: active
intent: execution-backlog

> **Este documento SUBSTITUI os planos fragmentados como fonte de trabalho em aberto.**
> Ele absorve: `COMPLETION_ACTION_PLAN.md`, `FINALIZATION_EXECUTION_PLAN.md`,
> `ACTION_PLAN.md`, `VIDEO_BACKEND_REWRITE_PLAN.md`, `NETWORK_AUDIO_PANEL_PLAN.md`,
> `CRAFT_OPTIMIZATION_PLAN.md`, `ICON_THEME_IMPLEMENTATION_PLAN.md`,
> `posix-graphics-xlike-plan.md`, `usb_boot.md`, `VIBELOADER_PLAN.md`,
> `guidelines/COMPAT_PLAN*.md`, `guidelines/MODULAR_APP_INTEGRATION_PLAN.md`,
> `guidelines/COMPAT_GAMES_MENU_PORT_PLAN.md`, `guidelines/java.md`,
> `AUDIO_HARDWARE_VALIDATION_MATRIX.md` e as secoes abertas de
> `KNOWN_BUGS.md`/`hw_validation.md`/`guidelines/MICROKERNEL_MIGRATION.md`.
> Onde houver conflito entre eles, **este plano vence** (foi escrito contra o
> codigo verificado em 15/10, nao contra os planos antigos).
> `COMPLETION_ACTION_PLAN.md` fica como **log de evidencia por prioridade**
> (historico de por que cada item fechou); nao duplicar trabalho novo la.

---

## 0. Contrato deste plano

### 0.1 Regra anti-stub (o que "definitivo" significa)

Nenhum item fecha com placeholder. Cada item termina em **exatamente um** destes estados:

| Estado | Significado | Obrigatorio |
| --- | --- | --- |
| **FEITO** | comportamento real, observavel, com prova executada | link do report/log em `build/` + comando na mensagem de commit |
| **NAO-OBJETIVO** | decidido fora de escopo, com justificativa escrita | entrada na secao "Nao-objetivos declarados" + doc de referencia corrigido |
| **BLOQUEADO-HW** | implementado, porem so se *prova* em maquina fisica | codigo no repo + linha da matriz (`AUDIO_HARDWARE_VALIDATION_MATRIX.md`, `hw_validation.md`) marcada "implementado, aguardando silicio" |
| **DECISAO [!]** | exige decisao da mantenedora antes de codar | item parado, com as opcoes escritas |

Proibido: `return 0` de fachada, `TODO` que finge implementacao, log que afirma
mais do que o codigo faz, teste que passa sem exercitar o caminho, "unsupported"
silencioso. Falta de suporte = diagnostico explicito (`ENOTTY`/`ENOSYS`/log
honesto) — nunca estado falso.

### 0.2 Provas aceitas

- build completo verde (`make`, `-Werror`) **antes** de qualquer claim;
- `make validate-*` aplicavel com report em `build/` (a matriz por area esta em cada onda);
- boot real no QEMU no perfil da area (`make run`, `run-headless-*`);
- para itens de hardware: log serial da maquina fisica anexado no report.

### 0.3 Invariantes que nenhum item pode violar

- **regra universal**: o kernel nunca quebra o userland; ABI apenas aditiva/versionada/com fallback (`docs/abi-contracts.md`);
- `BOOTINFO`, header de AppFS e structs de syscall so mudam com versao nova publicada;
- uma unidade de trabalho coesa por commit; implementacao + prova + doc no mesmo commit;
- `main` e a unica branch de longa duracao; merge/promocao e da mantenedora.

### 0.4 O que "tudo de vez" significa aqui

O backlog abaixo e executado em **ondas**, nao em paralelo caotico: cada onda
fecha com gate verde antes da proxima. Duas areas sao *subsistemas novos*, nao
"itens" — Wi-Fi 802.11 real e um motor de browser — e por isso aparecem como
`DECISAO [!]` com custo honesto; todas as outras areas fecham por implementacao.

---

## 1. Ondas (ordem de execucao)

| Onda | Tema | Fecha quando |
| --- | --- | --- |
| **0** | Fundacao: bugs de boot, gates, consolidacao de docs | build/parse limpos + gate base verde |
| **1** | Rede definitiva (IPv4 client, ferramentas, NIC, honestidade Wi-Fi) | `validate-network-datapath` + `validate-network-surface`*(hardened)* verdes |
| **2** | SMP definitivo (2+ CPUs distribuidas, sem corrida) | `validate-smp` verde em smp2/smp4 e `make run` SMP=2 responsivo |
| **3** | Audio definitivo (playback+captura, azalia fechado, uaudio) | `validate-audio-*` verdes; matriz HW marcada "aguardando silicio" |
| **4** | Video definitivo (DRM, modos, protocolo de estresse) | `validate-video`/`validate-startx-*` + protocolo rodado |
| **5** | Runtimes definitivos (js/mruby/micropython/lua/sectorc/java) | `validate-modular-apps` com programa real por runtime |
| **6** | Userland/apps (Craft clone, MicroUI, orfaos, icons, fontes, jogos, browser) | apps abrem pelo menu e pela shell com smoke registrado |
| **7** | POSIX/compat definitivo (execve/waitpid/signals/poll, VFS, X-Like) | suite QEMU de compat verde |
| **8** | Hardware real (T61/T400) | matrizes preenchidas com log serial real |
| **9** | Fechamento/honestidade (README final, planos aposentados) | zero afirmacao sem evidencia |

Dependencias duras: **0 → 1 → 2** (SMP destrava a validacao confiavel do resto);
**1** antes de 6 (browser/curl) e antes de 7 (poll/socket); **3/4/5** podem
correr em paralelo depois de 2; **8** so depois de 0–7 (para nao gastar silicio
depurando coisa conhecida).

---

## Onda 0 — Fundacao

| # | Item | Estado verificado (15/10) | Definicao de pronto | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 0.1 | Bug de boot: `kernel_usb_host_controller_count()` retorna 0 apos UHCI iniciar | log se contradiz (`usb: UHCI at 00:01.2` → `usb: no controllers found`) | contagem real | boot log coerente + `validate-phase6-boot-smoke` | — |
| 0.2 | `make` sem warns: dedup `build/libksh.a` + varredura de 229 TODO/stub/n.i. | `libksh.a` ja deduplicado (Build.compat.mk:132–135); TODO sweep pendente | cada TODO convertido em: feito / bug em KNOWN_BUGS / item neste plano | `make` rc=0 sem warning | — |
| 0.3 | Telemetria: `g_timer_trace_budget` fixo em 8 prints | dificulta diagnostico de boot lento | knob de quiet/verbose | doc + default | — |
| 0.4 | Consolidacao de planos: marcar os absorvidos como "consolidado em DEFINITIVE_CLOSURE_PLAN.md" | 15+ planos com drift entre si | cada plano antigo ganha header de pointer; `docs/README.md` atualizado | grep | — |
| 0.5 | Gate base: seletor rescue + gates dos harnesses interativos | **PARCIAL (15/10)** — seletor rescue agora e closed-loop com cold-restart (para de usar timing cego), gate de comando exige o exit-marker real (`shell: command exit X rc=0`, antes aceitava o `shell: ready` impresso ANTES de ler a linha) e timeouts honestos; com isso `validate-network-surface` passou a ALCANCAR o shell e executar comandos (antes nem chegava) | — | falta o item 0.6 |
| 0.6 | **Sessao interativa trava no meio: console/entrada param de escoar** (novo, achado 15/10) | probe manual (fora de harness): rescue -> `shell: ready` em 3s -> digitar `netmgrd status` -> echo PARA no meio (`... netmgrd st`) e nada mais e ecoado, com ticks seguindo (guest vivo); no harness o app imprime 13 linhas do status e para no meio. E a CAUSA RAIZ das flakes de `validate-network-surface`/`abi-compat-surface`/`audio-stack` | contadores de enfileirado/entregue/descartado na fila de console (host) e na de teclado (`mk_input_service_read_key`) apontando o lado que para; causa raiz corrigida; gates verdes repetidos | KNOWN_BUGS "Sessao interativa trava no meio" | — |
| 0.7 | Bug de diagnostico corrigido: `sched-dispatch: BAD-FORENSICS` disparava em falso p/ todo frame ring3 (inundava o serial e soterrou a saida dos apps) | [x] FEITO 15/10 — `ebp` de frame de usuario validado contra a janela de stack de USUARIO, nao a de kernel | — | log de boot com ZERO `BAD-FORENSICS`; `validate-network-datapath` PASS; `phase6-boot-smoke` 6/6 | — |

> 0.5/0.6 sao o destravador silencioso de varias ondas: tres validadores nao
> valem como gate por causa do mesmo caminho de sessao interativa. O 0.7 ja
> caiu no caminho (bug real, nao hipotese).

---

## Onda 1 — Rede definitiva

Fonte: `COMPLETION_ACTION_PLAN.md` P3 (camadas 1–4), `NETWORK_AUDIO_PANEL_PLAN.md`
(secao rede), `FINALIZATION_EXECUTION_PLAN.md` (linhas de rede).
Ja fechado (14–15/10, nao reabrir): TX self-wipe, ARP/ICMP responders, loopback
127/8 real, roundtrip gateway, ARP cache multi-entrada+timeout, TX ring
SMP-safe 8-frames, `validate-network-datapath`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 1.1 | RX orientado a IRQ | polling 100 Hz, ring 8 slots | RX por interrupcao do device com fallback de polling | `validate-network-datapath` (com NIC)+ latencia | — |
| 1.2 | TCP cliente real: backlog>1, sockets>8, timers RTT/RTO/retransmissao | SYN/ACK existe, nao validado end-to-end | conexao TCP real cliente↔servidor (ex.: fetch HTTP) | novo `validate-network-tcp` | — |
| 1.3 | UDP cliente real (alem de DHCP) | so DHCP | send/recv UDP to arbitrario | caso no validador | — |
| 1.4 | DNS do DHCP + `/etc/resolv.conf`-equivalente + servidores config. | hardcoded 10.0.2.3 | resolver le servidor do lease e de config | `host`/`dig` resolvendo nome real | — |
| 1.5 | `ifconfig`/`route`/`netstat` com tabela real | imprimem snapshots | leem estado real do servico | saida x estado | — |
| 1.6 | `curl`/`wget` com HTTP pela propria stack | curl so `file://`, wget inexistente | HTTP GET real | GET contra host do user-net | dep 1.2 |
| 1.7 | Gate HTTP via links2 | aberto | links2 busca pagina real | report | dep 1.2/1.6 |
| 1.8 | NIC abstraction `[!]` | esbocada em e1000.c:573–610 | `send_frame/attach_mac/link_status`; network.c fala com drivers registrados | build + virtio pelo caminho novo | **[!]** |
| 1.9 | e1000 + rtl8139 ligados de verdade | **dead code** (zero callers) | init a partir de entry.c, VA==PA de DMA, IRQ de RX | `validate-network-datapath` com `-device e1000` | dep 1.8 |
| 1.10 | Wi-Fi: fim da fachada | `wifi_read_pci_bar0` BDF fixo 0:0:0, scan hardcoded, connect inventa IP | (a) painel reporta "nao disponivel" e o data-plane NAO inicializa em producao; (b) doc honesto | codigo + doc | — |
| 1.11 | Wi-Fi 802.11 real: auth/assoc + EAPOL LLC/SNAP + RNG real + TK/GTK em CCMP | `wifi_wpa2.c` tem cripto real orfa; sem caminho de dados | associacao real com AP | `iwi`/`iwlwifi` sob QEMU+mac80211_hwsim ou HW | **[!]** escopo |
| 1.12 | Panel/UI de rede | scans hardcoded | reporta estado real | UI x estado | dep 1.10 |

> **1.11 e um subsistema**, nao um item: driver 802.11 + supplicant + CCMP.
> Opcoes honestas para a mantenedora: (A) implementar completo, (B) manter Wi-Fi
> como NAO-OBJETIVO (remover a fachada e declarar), (C) porta de um driver
> Linux via a camada compat. A onda 1 fecha sem ele; 1.11 e o unico item
> parado esperando decision.

---

## Onda 2 — SMP definitivo

Fonte: `COMPLETION_ACTION_PLAN.md` P4 + `KNOWN_BUGS.md` aberto + `guidelines/smp.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 2.1 | Classe #5: corrupcao real-time smp2 (flag ON, sem icount) | aberta; guards silentes; gdb suprime; campos de `process_t` recebem ponteiros de heap | causa raiz corrigida ou classe eliminada por design | `validate-smp` smp2 real-time verde em N corridas | — |
| 2.2 | `smp_scheduler_enable()` chamado apos bring-up | `smp.c:464` zero callers; APs em `sti;hlt` | APs preempcam de verdade | `validate-smp` | — |
| 2.3 | Handler de IPI de reschedule | `smp.c:484` so EOI | reschedule imediato entre CPUs | teste de latencia/wakeup | — |
| 2.4 | IOAPIC: afinidade de IRQ (nao tudo no BSP) | tudo no BSP | IRQ roteado por afinidade | `validate-smp` + IRQ stats | — |
| 2.5 | smp4: bring-up dos 3 APs | `cpus=4/4` ausente | 4 CPUs online | `validate-smp` smp4 | — |
| 2.6 | `QEMU_RUN_SMP` default para 2 | default 1 | `make run` responsivo em 2 CPUs | `make run` + taskmgr mostrando 2 cores | dep 2.1–2.5 |
| 2.7 | `validate-smp` como gate confiavel (nao-flaky) | smp2/smp4 falham | verde de forma repetivel | 3 corridas limpas | dep 2.1 |

---

## Onda 3 — Audio definitivo

Fonte: `NETWORK_AUDIO_PANEL_PLAN.md` (audio), `AUDIO_HARDWARE_VALIDATION_MATRIX.md`,
`COMPLETION_ACTION_PLAN.md` P5.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 3.1 | HDA captura (input stream + BDL de captura) | azalia so playback; caps sem CAPTURE | record real | `validate-audio-stack-roundtrip` | — |
| 3.2 | IRQ compartilhado INTx em HDA/AC97 | `audio.c:5363` assume handler unico | handler compartilhado correto | boot sem IRQ storm | — |
| 3.3 | Fechar `compat-azalia` | ~35 sub-itens `[~]` (codec, widgets, path, unsol, jack sense) | rota canonica estavel, sem re-probe agressivo, diagnostico honesto | `validate-audio-hda-*` | — |
| 3.4 | `compat-uaudio` (USB audio) MVP | existe MVP UHCI-only; sem isocronico completo | playback USB com transporte real | `validate-audio-stack` com USB | dep Onda 0/2 |
| 3.5 | Gargalo PCM (chiado): DMA stale + start/stop por write | aberto | playback limpo (sem artefato) | gravacao + inspecao | dep 3.3 |
| 3.6 | Preencher `AUDIO_HARDWARE_VALIDATION_MATRIX.md` (HW-01..HW-20) | 0/20 linhas | cada linha com log real | logs | **HW** |
| 3.7 | Telemetria de audio (IRQ/starvation/underrun) | ausente | contadores expostos | painel/log | — |

---

## Onda 4 — Video/GPU definitivo

Fonte: `VIDEO_BACKEND_REWRITE_PLAN.md`, `COMPLETION_ACTION_PLAN.md` P6.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 4.1 | Decisao do default i915 (`INTEL_I915_EXPERIMENTAL_COMMIT=1` hoje) | plano prega "0 seguro" | default definido e coerente com o doc | Makefile + doc | **[!]** |
| 4.2 | MTRR como fallback opcional de WC | aberto | suporte ou NAO-OBJETIVO escrito | doc | — |
| 4.3 | Atualizar `hw_validation.md` e `VIDEO_BACKEND_REWRITE_PLAN.md` contra o codigo | drift | docs batem com o codigo | grep | — |
| 4.4 | Modos widescreen (1366x768/1360x768/1920x1080) | caem para 640x480/1024x768 | modos aplicados | `validate-video` | **HW**/firmware |
| 4.5 | Protocolo de estresse (vidstress/appcycle/dragstress, fill, gradiente) | parcialmente rodado | matriz rodada e registrada | report | — |
| 4.6 | Campanha de heap ao redor do caminho grafico | faltam coletas | antes/depois de boot/startx/jogos | report | parcial **HW** |
| 4.7 | Bench de present (full-frame, WC, SSE2 non-temporal) | faltam medicoes | numeros + meta registrados | report | parcial **HW** |
| 4.8 | Portar 1 backend de GPU real (i915/radeon/nouveau) | probe only | 1 backend com modeset real | snapshot/rollback + log | **HW** |

---

## Onda 5 — Runtimes definitivos

Fonte: `COMPLETION_ACTION_PLAN.md` P7 (linhas 175–178), `guidelines/java.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 5.1 | quickjs: reativar bloco comentado + `quickjs_wrapper.c`; app `js` real | comentado no Makefile:805–828 | REPL + script real | `validate-modular-apps` cenário js | — |
| 5.2 | micropython | idem | REPL + script | validador | — |
| 5.3 | mruby | idem | REPL + script | validador | — |
| 5.4 | lua + sectorc | existem; validar | programa nao-trivial | validador | — |
| 5.5 | Java (`java`/`javac` caseiros) | experimental | declarado experimental honesto (sem promessa de JVM) OU JVM real | doc + smoke | **[!]** escopo |
| 5.6 | Heap/stack por classe de runtime respeitando `MEMORY_ADAPTIVE_RAM_POLICY` | aberto | medido por runtime | report de memoria | — |

**DoD da onda:** `make validate-modular-apps` com **hello-loop + programa
nao-trivial** em js/ruby/python/lua/sectorc, sem stub em nenhum.

---

## Onda 6 — Userland/apps definitivos

Fontes: `COMPLETION_ACTION_PLAN.md` P8 (182–189), `CRAFT_OPTIMIZATION_PLAN.md`,
`ICON_THEME_IMPLEMENTATION_PLAN.md`, `MODULAR_APP_INTEGRATION_PLAN.md`,
`COMPAT_GAMES_MENU_PORT_PLAN.md`, `NETWORK_AUDIO_PANEL_PLAN.md` (browser).

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 6.1 | Craft = clone funcional (mundo, chunks async, SMP nos workers, renderer voxel, gameplay, perf) | `upstream/src/world.c` com 77 linhas; sem mundo visivel | checklist do `CRAFT_OPTIMIZATION_PLAN.md` 7.1–7.6 cumprido | sessao real: mundo, andar/destruir/colocar, sem corrupcao | dep 2 (SMP) |
| 6.2 | MicroUI: consumir ou remover | `desktop_new.c` fora do build | decidido e aplicado | build | — |
| 6.3 | Ramo orfao `shell_bsd.c` + `applications/ported/ksh` | fora de todo source list | removido ou ligado | build limpo | — |
| 6.4 | links2 no catalogo/imagem | fora | entra no `config/app_catalog.tsv` + imagem | boot + execucao | dep 1 |
| 6.5 | vibe-browser | Otter Qt5 pristine vendored | status explicito "referencia vendored" OU engine proprio minimo | doc | **[!]** escopo |
| 6.6 | Icon theme: fechar MVP (lookup/cache/validacao) | parcial | gaps do plano fechados ou NAO-OBJETIVO | boot + relatorio de cobertura | — |
| 6.7 | Fontes: escalavel ou multi-tamanho de bitmap | 1 bitmap 6x8 | decisao + implementacao | UI em varias resolucoes | — |
| 6.8 | Jogos `compat/games` no menu: smoke por jogo | sem smoke | cada jogo abre por menu e shell, corrigido | report por jogo | — |
| 6.9 | Modular app: paths explicitos + assets de DOOM/Craft | faltam smokes | todos launchables no AppFS e resolvidos | `validate-modular-apps` | — |

---

## Onda 7 — POSIX/compat definitivo

Fontes: `ACTION_PLAN.md` P6/P9, `posix-graphics-xlike-plan.md`,
`guidelines/COMPAT_PLAN.md`, `MODULAR_APP_INTEGRATION_PLAN.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 7.1 | `execve` real na camada compat | aberto | exec real de binario | teste | — |
| 7.2 | `waitpid` com estados BSD-shaped | aberto | estados corretos | teste | — |
| 7.3 | Sinais completos (sigaction/sigprocmask/sigwait) | parcial | semantica completa | suite | — |
| 7.4 | `poll`/`select` ligados a backends de espera do microkernel | aberto | espera real, sem busy-loop | teste + latencia | — |
| 7.5 | VFS on-disk (alem de ramfs) + ELF nativo do VFS | aberto | montagem real + exec | teste | — |
| 7.6 | Semantica POSIX real de arquivo (append/trunc/write) | aberto | testes POSIX | suite | — |
| 7.7 | X-Like: servidor compat isolado + janelas | cliente shim pronto | Fases 3/5/7 do plano X-Like | suite QEMU grafica | — |
| 7.8 | X-Like: isolamento de falha (matar cliente != derrubar desktop) | aberto | provado | teste de falha | — |

---

## Onda 8 — Hardware real (T61/T400)

Fonte: `hw_validation.md`, `AUDIO_HARDWARE_VALIDATION_MATRIX.md`,
`COMPLETION_ACTION_PLAN.md` P0, `usb_boot.md`.

| # | Item | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- |
| 8.1 | Boot BIOS real com log serial | boot + desktop na maquina | log serial anexado | **HW** |
| 8.2 | USB mass-storage runtime nativo (EHCI/xHCI + BOT) | storage USB monta e le | log | **HW** + Onda 0 |
| 8.3 | Audio HDA/AC97 real | playback audivel + captura | matriz HW preenchida | **HW** |
| 8.4 | Video i915/radeon real | modeset nativo | log + snapshot | **HW** |
| 8.5 | Rede NIC real (T61/T400) | link + DHCP + ping | log | **HW** + Onda 1 |
| 8.6 | POSIX/timing em PIT/HPET reais | superficie syscall estavel | suite | **HW** |
| 8.7 | SMP real em 1 notebook | 2 CPUs distribuidos | taskmgr + log | **HW** + Onda 2 |

> Nada em 8.x e "implementacao": e **prova de silicio**. Toda linha entra como
> BLOQUEADO-HW ate existir a maquina; o codigo correspondente ja esta nas ondas 0–7.

---

## Onda 9 — Fechamento e honestidade

Fonte: `COMPLETION_ACTION_PLAN.md` P9 (quase fechado), "Definicao Pratica de Acabou".

| # | Item | DoD | Prova |
| --- | --- | --- | --- |
| 9.1 | Zero afirmacao "funcional/validado" sem linha no report `build/` | varredura de README/docs | grep + reports |
| 9.2 | README reduzido ao honestamente futuro (UEFI, IPv6, 3D accel) | secao final limpa | leitura |
| 9.3 | Planos absorvidos aposentados (pointer stubs) | arquivos com header "consolidado" | grep |
| 9.4 | `validate-smp`, `validate-network-*`, `validate-abi-*`, `validate-audio-*`, `validate-video`, `validate-modular-apps` verdes e repetiveis | matriz completa | reports |

---

## 2. Nao-objetivos declarados (para nao virarem stub disfarcado)

Honestamente fora de escopo (a menos que a mantenedora promova):

- **UEFI** — boot legacy 32 bits apenas (o repo ja assume isso);
- **IPv6** — sem parsing/config; `supported_families` = UNIX|INET (ja honesto);
- **Aceleracao 3D** — nenhuma; DRM e modeset/2D;
- **Wayland / XCB / extensoes X11 amplas** — X-Like minimo e o alvo;
- **Suspender/retomar e hotplug** — nao implementados;
- **VibeLoader (bootloader novo)** — `VIBELOADER_PLAN.md` se declara "funcionalmente
  fechado"; seus itens restantes sao backlog **opcional** (config file, menu,
  imagem PNG no loader) e ficam registrados aqui como opcionais, fora das ondas;
- **Suporte a formatos de imagem alem de BMP e PNG** no desktop.

Cada um destes deve aparecer assim nos docs de referencia (a Onda 9 confere).

---

## 3. Matriz de rastreabilidade (nada foi perdido)

| Plano de origem | Absorvido em |
| --- | --- |
| `COMPLETION_ACTION_PLAN.md` (P3/P4/P5/P6/P7/P8/P9) | Ondas 1,2,3,4,5,6,9 |
| `FINALIZATION_EXECUTION_PLAN.md` | Ondas 1,3,8 + DoD global |
| `ACTION_PLAN.md` (P6–P9) | Ondas 6,7 |
| `VIDEO_BACKEND_REWRITE_PLAN.md` | Onda 4 |
| `NETWORK_AUDIO_PANEL_PLAN.md` | Ondas 1,3,6 |
| `CRAFT_OPTIMIZATION_PLAN.md` | Onda 6.1 |
| `ICON_THEME_IMPLEMENTATION_PLAN.md` | Onda 6.6 |
| `posix-graphics-xlike-plan.md` | Onda 7.7/7.8 |
| `usb_boot.md` | Ondas 0.1, 8.2 |
| `VIBELOADER_PLAN.md` | Nao-objetivos (backlog opcional) |
| `guidelines/COMPAT_PLAN.md` | Onda 7.5/7.6 |
| `guidelines/MODULAR_APP_INTEGRATION_PLAN.md` | Onda 6.9 |
| `guidelines/COMPAT_GAMES_MENU_PORT_PLAN.md` | Onda 6.8 |
| `guidelines/java.md` | Onda 5.5 |
| `guidelines/MICROKERNEL_MIGRATION.md` (abertos) | Ondas 2,8 |
| `AUDIO_HARDWARE_VALIDATION_MATRIX.md` | Ondas 3.6, 8.3 |
| `KNOWN_BUGS.md` (Abertos) | Ondas 0.1, 2.1 |
| `hw_validation.md` (unverified) | Ondas 4.3, 8 |

---

## 4. Definicao de Acabou (global, vale para toda onda)

- [ ] `make` limpo, rc=0, **sem warnings**
- [ ] perfil de boot aplicavel chega em desktop/shell sem `hlt`-loop
- [ ] `make validate-*` da area verde, report em `build/`
- [ ] causa raiz corrigida, nao sintoma; nenhum stub/verde falso
- [ ] ABI intacta ou nova versao publicada; userland existente nao quebrou
- [ ] doc/plano atualizado **no mesmo commit**, com a evidencia
- [ ] item marcado FEITO/NAO-OBJETIVO/BLOQUEADO-HW/DECISAO — nunca "parcial" eterno

---

## 5. Estado inicial (ponto de partida verificado 15/10)

Ja FECHADO e fora do backlog (nao reabrir): boot chain real-BIOS (auditoria),
corrupcao SMP classes #1/#2, deadlock de serial, A20, CR0.CD/NW, trampoline do AP,
58 guards de ponteiro de usuario, classe #3 refutada, guard de privilegio classe #4,
P2 persistencia cross-boot, P3 datapath IPv4 (TX/ARP/ICMP/loopback/roundtrip/cache/
TX-ring), `kernel_debug_printf` width, e a passagem P9 de honestidade documental.

Aberto hoje, por ordem de entrada nas ondas: **0.1–0.5**, depois 1.x, 2.x, ...

`HEAD` no momento da escrita: `8560a0d97`.