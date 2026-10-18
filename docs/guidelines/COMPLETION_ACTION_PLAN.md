# Plano de Acao para Finalizar a Implementacao Completa do VibeOS

> **CONSOLIDADO (15/10):** o trabalho em aberto deste plano e dos demais planos
> do repo vive agora em [DEFINITIVE_CLOSURE_PLAN.md](DEFINITIVE_CLOSURE_PLAN.md)
> ("Plano de Fechamento Definitivo"), organizado em ondas com regra anti-stub.
> Este arquivo permanece como **log de evidencia por prioridade** (historico de
> por que cada item fechou). Nao abrir trabalho novo aqui — use o novo plano e
> registre a prova de volta neste log quando o item fechar.

last: P1 CONCLUIDA (commit b23ac3fc5): boot stall corrigido (stack de boot movida para 0x800000 + cmov/i586 + ioapic pin+2 + EOI de IRQ espoliador); validate-phase6-boot-smoke 6/6 PASS
doing: P2/P4 parciais (ver checkboxes); FECHADO 16/10: (a) o stall de wakeup do storage no AP acabou (real-time smp2 faz `init: storage smoke ok`); (b) FIX servico-layer: `mk_service_launch_task` devolvia pid>0 como sucesso e callers liam !=0 como falha — os "deferred launch failed" de input/console/network eram SUCESSOS mal reportados (KNOWN_BUGS #14); pos-fix, smp2 real-time alcancou `desktop: session ready` sem icount pela primeira vez
next: P4 classe #5 — witnesses executados 16/10 parte 5: WATERMARK-FRAME 0 hits; sched-rsp OK x2 + 0 OUTSIDE (ESP do AP dentro das stacks declaradas nos ticks — 'esp acima do topo' NAO se sustenta no ponto amostrado); a face-1 (OVER-dump com cadeia de frames tss_set_kernel_stack ACIMA do topo do bloco) reconcilia com isso via ESCRITA por ponteiro-velho/alias. Proximo passo concreto: traco orcado em kernel_tss_set_kernel_stack (quem chama, valor do esp0, cpu) + auditoria dos usos de ponteiros de task/stack depois de terminate (g_current stale, service->process stale, launch record stale). RUN atual deu nova face: #UD eip=0x11394=userland_entry+0x684 (salto para dentro do BLOB; bytes 00 00 no ELF = salto selvagem, nao reescrita); ud-bt bruto e heuristico (flag: e1000_ioctl nao tem chamadores -> residuo). Face-2 FECHADA. P3 NIC abstraction [!] segue travada em decisao
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
| Rede virtio IPv4 (client) | **funciona em runtime no QEMU** (14/10) | datapath real provado: `dhcp bound ip=10.0.2.15` + ARP reply + RX entregando (`rx activity len=64`); causa raiz do silêncio era TX self-wipe (submit_frame), corrigida |
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
- [~] Persistencia no boot real — PARCIAL (14/10): (a) o storage smoke test do init RODA hoje (confirmado: 'init: storage smoke begin/ok') — a alegacao 'nunca roda' estava desatualizada; mas era um teste FRACO (leia-duas-vezes-e-compare) que NUNCA exercitava o caminho de ESCRITA, justamente a parte arriscada em hardware real (handshake DRQ de escrita + flush de write-cache). Agora faz ler-original -> escrever-padrao -> ler-back -> verificar -> RESTAURAR-original -> verificar-restauracao, nao-destrutivo, em todo boot, no ultimo setor da janela de persistencia (rascunho: kernel_storage_save/sys_storage_save tem zero chamadores; o fs host so le a area e nunca salva de volta). CONTROLE NEGATIVO provou o assert forte: LBA 0x0FFFFFF0 (alem do limite de disco) -> 'storage smoke read failed' (sem verde falso). Gate UP: validate-phase6-boot-smoke 6/6 PASS com o write-path smoke ativo. PROVA VIVA: boot core2duo mostra 'init: storage smoke ok (read+write+restore)'. (b) DURABILIDADE CROSS-BOOT VALIDADA (14/10 parte 12): nova ferramenta `tools/validate_storage_persist.py` + alvo `make validate-storage-persist` ligam DUAS vezes na MESMA imagem scratch SEM snapshot (todo outro alvo QEMU usa snapshot=on, que descarta escritas). Sinal real: boot 1 reconstroi a imagem do fs ('persistent image invalid, rebuilding') e a grava no disco via o caminho de writeback do fs (tick-driven, por isso o boot 1 roda o timeout INTEIRO — nao pode sair cedo no marker de smoke); boot 2 le de volta ('persistent image valid' SEM rebuild). Prova executada: `make validate-storage-persist` PASS, relatorio build/storage-persist-validation.md: boot1 rebuild=yes/smoke=yes, boot2 valid=yes/rebuild=no => durable. Confirmacao independente por inspecao do disco: a regiao de persistencia da scratch tinha 346 bytes nao-zero apos o boot 1. CONTROLE NEGATIVO (assert forte, sem verde falso): duas imagens fresh DIFERENTES (simulando nao-durabilidade) fazem o boot 2 reportar 'invalid, rebuilding' => a ferramenta FALHARIA corretamente. Isto e o gate NECESSARIO (QEMU); durabilidade em midia REAL ainda so se prova em disco fisico (campanha P0).

## Prioridade 3: rede real completa

Camada 1 — infraestrutura:

- [ ] Definir NIC abstraction [!] (ja esbocada em e1000.c:573-610): `send_frame/attach_mac/link_status`; network.c deixa de ter virtio privada dentro do servico e passa a falar com drivers registrados
- [x] **FEITO (15/10)** Ligar e1000 **e** rtl8139 de verdade (init a partir de entry.c, DMA, corrida do IRQ de RX). Ambos provados no fio. Ver KNOWN_BUGS #12 (e1000) e #13 (rtl8139). **e1000:** causa raiz = anel de TX na janela **alias legada 0x420+**; canonicos do 8254x sao **0x3800/0x3804/0x3808/0x3810/0x3818** (o datasheet e o modelo do QEMU só honram o canonico -> escrita descartada, TDLEN=0, nenhum frame saia). **rtl8139 (dois defeitos independentes):** TX = escrevia `TSD = len | OWN`, ligando o bit OWN que faz o chip **recusar** a transmissao (Linux `8139too` escreve sem OWN; conclusao por `TxStatOK`); RX = o cabecalho do ring e **32 bits com o tamanho nos 16 altos** (`rx_size = word >> 16; pkt_size = rx_size - 4`) e o driver usava o layout do TSD de TX -> `rx_len` lixo. Junto nos dois: ISR de RX deixou de consumir o pacote (classe comum), reclaim por bit de conclusao correto, MAC/RA reais, link real (nao fabricado), padding <60B no rtl8139, Cfg9346 unlock, match 0x8169 falso removido. PROVA: `make validate-network-datapath NIC=e1000` e `NIC=rtl8139` = PASS (`build/network-datapath-validation-{e1000,rtl8139}.md`: **2 quadros no fio** cada = ARP request nosso + reply do gateway -> TX e RX ponta-a-ponta); regressoes virtio PASS e `validate-phase6-boot-smoke` 6/6. **RESTA (nao desta linha):** plugar os drivers na **NIC abstraction [!]** (linha acima) para o SERVICO (DHCP/responders) usa-los — hoje o servico fala só virtio, entao nenhum dos dois serve trafego ao userland ainda.
- [x] VIRTIO RX NAO ENTREGA — CAUSA RAIZ FECHADA (14/10): NUNCA foi problema de RX. Capture com `-object filter-dump` no QEMU provou que TODO frame TX saia com 42/594 bytes ZERADOS (168/168 pacaps com zero bytes nao-nulos): os construtores (`send_arp_request`/`send_icmp_echo`/`send_udp`/reply) montam o frame DIRETAMENTE em `g_network_virtio_tx_frame` e passam esse mesmo ponteiro para `submit_frame`, que fazia `memset` do buffer antes do `memcpy` — wipe-self: apagava o proprio frame e enviava zeros. O host descartava tudo (dest MAC 00:00:00:00:00:00), por isso `rxseen=0` e 0 ofertas DHCP apesar de 10 discovers. FIX: `submit_frame` so copia para o staging buffer quando `frame != g_network_virtio_tx_frame` (kernel/microkernel/network.c). EVIDENCIA pos-fix (mesmo perfil virtio-net user-net): pcap mostra ARP request (ff:..:ff) -> ARP reply, DHCP discover 68->67 -> offer 67->68 -> request -> ack; serial: `virtio rx activity len=64`, `virtio rx arp-reply seen`, `dhcp offer from 10.0.2.2 ip=10.0.2.15`, `dhcp bound ip=10.0.2.15`. Primeira vez no repo que o datapath IPv4 real fecha em QEMU. O probe sugerido (avail/used idx da RX, PA==VA) tornou-se desnecessario: o ring RX sempre esteve correto — nada chegava porque nada valido saia.
- [ ] RX orientado a IRQ (hoje 100 Hz polling, ring de 8 slots) — parte TX FECHADA abaixo
- [x] TX com fila (14/10 parte 2): o datapath virtio NAO era SMP-safe e tinha 1 frame em voo. ACHADO (auditoria): `submit_frame` era tocado de (a) tick de timer no BSP (poll→drain→responders), (b) syscalls de rede no CPU do chamador, (c) spin-poll do probe — SEM NENHUMA lock: `tx_busy` era check-then-set nao-atomic, e os builders compartilhavam UM scratch `g_network_virtio_tx_frame` com `desc[0]/desc[1]` fixos → dois escritores concurrentes publicavam por cima da mesma cadeia em voo (DMA pode ler frame rasgado) e o segundo frame era descartado em silencio. FIX NA CAUSA RAIZ: (1) `g_virtio_ring_lock` (irqsave) protegendo toda bookkeeping de ring; builders nunca sao chamados com a lock segurada — `drain_rx` claima o used-entry sob lock, processa FORA (responders re-entram `submit_frame` e lockam por dentro), republish sob lock; `drain_tx`/`post_rx` self-locked com double-check de `rx_posted` (dois CPUs em maybe_activate nao podem postar a cadeia duas vezes); (2) TX virou RING de 8 frames com hdr+payload PRIVADOS por slot (`g_network_virtio_tx_slots`, descricoes slot*2/slot*2+1 — mesmo padrao ja usado pelo RX) — sem buffer compartilhado, sem par de descricoes fixo, ate 8 em voo, backpressure com contador `drop=` (nunca silencio); (3) maquina de passos do self-test removida (as duas replies cabem no ring); (4) smoke do TX agora por snapshot de contadores, nao por flag de busy. A classe do bug a7ece92a7 (caller==scratch alias) virou ESTRUTURALMENTE impossivel. EVIDENCIA: `make validate-network-datapath` PASS com novo gate `drop=0` na linha do roundtrip (o boot estoura 2 replies back-to-back no MESMO drain pass — o caso exato que o design 1-frame descartava; `network: icmp gateway roundtrip ok rtt_ticks=1 drop=0 gw=10.0.2.2`); no validate-smp com flag SMP ON o anel novo rodou completo sem deadlock de lock (roundtrip+arp cache+responders todos presentes, nenhum 'stuck': o unico FAIL sao os escombros da classe #5 conhecida, BAD-HEAD/WILD com ponteiros de heap escritos em header — familia pre-existente, nao-gating, invariada por esta mudanca); validate-phase6-boot-smoke 6/6; make verde -Werror.

Camada 2 — stack:

- [x] ARP: cache multi-entrada + timeout (14/10): era 1 entrada unica — cada aprendizado novo PISAVA o MAC do gateway (thrash de re-ARP em LAN real) e entradas velhas nunca expiravam (MAC obsoleto para sempre). Agora: tabela de 8 slots (`MK_NETWORK_ARP_CACHE_MAX`), refresh-in-place por IP, insercao em slot livre, eviction da entrada mais ANTIGA por diff de tick (wrap-safe, int32), e expiracao de 3000 ticks (~30s @100Hz) checada no LOOKUP (entrada velha vira miss -> forca re-ARP em vez de MAC obsoleto). Estado interno do servico (struct kernel; zero campos em `mk_network_status/info`) — ABI intacta. EVIDENCIA (deterministica, nao-interativa, no mesmo gate): self-test injeta peer 10.0.2.77 e verifica coexistencia peer+gateway (`network: arp cache multi ok entries=2`) + aging do peer alem do janela -> lookup MISS -> re-learn -> lookup HIT; falta de coexistencia expoe `network: arp cache FAILED (...)` que quebra o gate. `make validate-network-datapath` PASS com marker no serial (build/network-datapath-validation.md).
- [x] ARP RESPONDER (14/10): `mk_network_backend_ingest_arp` agora responde_requests de ARP para o IP bound (unicast reply ao solicitante; nunca proxy). Antes disso NINGUEM conseguia alcancar o VibeOS. EVIDENCIA: `make validate-network-datapath` (novo gate) — self-test de responders no kernel injeta um request ARP e um echo-request inbound no MESMO caminho de ingest do RX; pcap (`filter-dump`) mostra ARP reply (op=2) ao MAC do peer do self-test + ICMP echo-reply ao peer; serial: `network: arp request answered for 10.0.2.15`, `network: icmp echo-reply sent to 10.0.2.77`, `network: responder self-test sent`. Self-test gateado a QEMU user-net (servidor DHCP 10.0.2.2) — nunca emite quadros sinteticos em rede real.
- [x] ICMP: responder echo-request (be ping-able) + loopback real de IPv4 + DEADLOCK DE SOCKET ENCONTRADA E CORRIGIDA NA CAUSA RAIZ (14/10): (a) echo-reply construido a partir do request espelhado (id/seq/payload), checksums reais, enviado so quando o submit confirma (sem falso-verde de log); (b) loopback 127/8 real: `mk_network_service_send` responde echo internamente e entrega pela MESMA fila RX/recv de uma reply de fio (`mk_network_icmp_loopback_reply`), smoke de boot nao-interativa `network: loopback smoke ok bytes=21` roda em todo boot (UP, com e sem NIC — provado nos dois); output fabricado de `ping localhost` REMOVIDO do app (era ping.c:51-59; o gate agora exige datapath real apenas para alvos NAO-loopback). (c) CAUSA RAIZ NOVA: `network_queue_raise_ready_callback` era chamado COM `g_network_queue_lock` ASSERTA e o callback chamava `network_queue_get_rx_depth()/get_tx_depth()` que readquiriam a mesma spinlock nao-recursiva → DEADLOCK em qualquer `socket()` (register levanta READY_SEND). Evidencia ao vivo: a smoke travou o boot no primeiro tick; com o fix, `loopback smoke ok` em UP. Esse bug explica porque o caminho de socket jamais passou em harness interativo (parcialmente o `shell: command` flaky do network-surface). FIX: callback passou a receber handle/flags/rx_depth/tx_depth POR VALOR (capturados sob lock); assinatura interna kernel-kernel (zero uso em userland, grep provado), ABI intacta. (d) Ping gateway ida-e-volta REAL via smoke do kernel: `network: icmp gateway roundtrip ok rtt_ticks=1 gw=10.0.2.2` + pcap com echo-request(type8)->echo-reply(type0) de/para 10.0.2.2. EVIDENCIA COMPLETA: `build/network-datapath-validation.md` = PASS (dhcp bound + roundtrip + 2 responders + loopback smoke); `validate-phase6-boot-smoke` 6/6 PASS pos-fix (incl. usb-bios-boot com PHASE6_TIMEOUT=420 — confirma que o fail anterior era so o teto de 20s).
- [ ] TCP: backlog >1, sockets >8, timers corretos (RTT/RTO, retransmissao hoje e retry cego a cada 50 ticks)
- [ ] DNS: resolver em netdb.c ler o servidor do DHCP (hoje hardcoded 10.0.2.3) + `/etc/resolv.conf`-equivalente
- [x] Capacidades honestas (14/10): corrigido o falso-verde de capability. `supported_families` anunciava AF_INET6 sem NENHUMA implementacao IPv6 (sem parsing de header, sem ND, sem config de endereco) — agora so UNIX|INET. `MK_NETWORK_CAPS_REAL_PACKET_DATAPATH` era setado por mera PRESENCA de device PCI class-02; agora exige `virtio_queue_ready` (datapath de fato). EVIDENCIA (trace novo `network: caps flags=... families=... datapath=N`): com virtio `flags=fffe families=3 datapath=1`; SEM NIC `flags=7ffe families=3 datapath=0`; com e1000 reconhecido mas SEM driver (8086:100e rotulado compat-em) `flags=7ffe families=3 datapath=0` — exatamente o falso-positivo removido. families=3 = UNIX|INET (sem INET6).

Camada 3 — WiFi (deixar de ser fachada):

- [ ] Panel/UI: ate 802.11 existir, scan/conectar reportam "nao disponivel" — hoje scans hardcoded (network.c:3096-3110) e connect escreve IP inventado (network.c:2813-2856); docs/hw_validation.md e NETWORK_AUDIO_PANEL_PLAN.md afirmam "WPA2 funcional" sem suporte no codigo
- [ ] `wifi_data_plane.c` nao fala com nenhum chip real (barra fixa em bus:dev 0:0:0) — marcar como experimental e nao inicializar no boot de producao
- [ ] Caminho real incremental: 802.11 auth/assoc + EAPOL com encapsulamento LLC/SNAP + RNG real nos nonces (wifi_wpa2.c hoje usa formulas deterministas) + instalar TK/GTK num caminho CCMP (nao existe criptografia de dados 802.11 no kernel)
- [ ] Manter `wifi_wpa2.c` (criptografia PBKDF2/PTK/4-way genuine) plugada quando o plano de frames existir

Camada 4 — ferramentas/apps:

- [ ] ifconfig/route/netstat lendo tabela real; `host`/`dig` usando o resolver de verdade; curl/wget com HTTP via stack propria (links2 HTTP engine ja existe como base)
- [ ] Validar com QEMU user-net: DHCP bound, ping gateway ida-e-volta, HTTP GET via links2; expandir `tools/validate_network_surface.py` alem dos checks de UI
  - [x] DHCP bound PROVADO em runtime (14/10): apos o fix do TX self-wipe, boot com `-netdev user -device virtio-net-pci` alcancou `dhcp offer from 10.0.2.2 ip=10.0.2.15` -> `dhcp ack` -> `dhcp bound ip=10.0.2.15`, com ARP reply e `virtio rx activity len=64`; pcap (`filter-dump`) confirma quadros reais bidirecionais (ARP 42/64B, UDP 67<->68). Resta: ping gateway ida-e-volta e HTTP GET.
  - [x] `make validate-network-surface` esta FLAKY/PRE-EXISTENTE-FALHANDO (14/10): confirmei com as mudancas desta rodada STASHADAS (HEAD limpo) que ele tambem falha — as mensagens variam entre runs ("Timed out waiting for markers: boot mode: rescue shell, shell: ready" e "... shell: command netmgrd ..."), sinal de timing de `sendkey` do harness de rescue shell, NAO de regressao de codigo. Bug latente do proprio validador; precisa de hardening do seletor de rescue (retry/espera) antes de poder servir de gate. NAO foi introduzido nesta sessao.
  - [x] `make validate-phase6` cenario `usb-bios-boot` FALHA com `PHASE6_TIMEOUT=20` (14/10, pre-existente, provado stashando a mudanca desta rodada e rodando HEAD limpo: mesmo missing `init: entered builtin entry, storage: init returned, shell: session start`): o boot USB BIOS neste host precisa de mais que 20s; com 60s na mesma imagem limpa os tres markers aparecem. Ajustar `PHASE6_TIMEOUT>=40` para este cenario ou dar timeout por cenario no harness. NAO e regressao do fix de rede (run com e sem a mudanca: 5/6 + usb-bios-boot FAIL identico).

## Prioridade 0: HARDWARE-FIRST (decisão da mantenedora, 2026-10-13)

Aceitação mudou: **boot, usb, video, audio, rede e posix precisam funcionar em máquina real (T61/T400), não só no QEMU.** QEMU continua gate necessário, mas nunca mais é suficiente. Fila hardware-first:

- [x] Cadeia de boot em BIOS real: auditoria feita (docs/hw_validation.md "Real-Hardware Boot Chain Audit"). **Bug fatal encontrado e corrigido**: sondas de A20 em boot/stage2.asm eram código morto (`xor ax,ax` setando ZF antes do `je`) — em BIOS IBM sem int 15h 2401h/2402h o loader pararia em `a20_error` mesmo com A20 habilitado via port-92/KBC. Prova: trace do loader lido vivo do guest (HMP xp 0xa02f) — `2FAVBaQ...` (sai na 1a checagem) vs `2FAVBabcdq...` (antes). validate-phase6-boot-smoke em execução com stage2 corrigido (relatório build/phase6-validation-boot-smoke.md).
- [x] Leitura de disco pós-power-on hardened (rodada 13/10 parte 2): MBR (leitura do VBR) e stage1 (leitura da stage2) agora fazem retry limitado 3x com media reset entre tentativas — CF transiente no primeiro acesso é normal em mídia real (spin-up/PLL/bridge USB) e invisível ao QEMU; mesma disciplina que stage2 já tinha. ATA do kernel auditado: já possui retry limitado, soft-reset, re-seleção de device por attempt e waits com deadline de ticks — nada a mudar. stage1 continua exigindo EDD (pré-EDD halt com erro claro) — aceito para a era T61/T400, documentado. Prova: make verde; boot UP `desktop: session ready`; stage1 montado em 512B exatos com assinatura AA55; validate-phase6-boot-smoke (relatório build/).
- [ ] USB storage runtime no kernel: backend nativo de massa-storage USB não existe (notas do phase6); boot USB via emulação BIOS funciona, mas "usb funcionando em hw real" exige driver real (EHCI/xHCI + BOT) — campanha com hardware físico (bloqueado: precisa máquina real + logs).
- [x] CPU caches em hw real (rodada 13/10 parte 6): CR0.CD/NW nunca eram limpos em lugar nenhum da cadeia — habilitar paging com CD=1 forca TODO o mapa uncacheable (framebuffer WC + heap em velocidade de barramento) em Core2 real; QEMU/TCG ignora os bits (no-op), entao o boot passava. Corrigido nos 3 sitios: `paging_init` (limpa CD+NW + WBINVD ANTES de setar PG, per SDM), AP trampoline (limpa CD+NW no MESMO write de PG — pos-RESET CR0=0x60100010 tem CD=1/NW=1, o clear do BSP nao vale para o AP), stage2 entrada PM (CD/NW off; paging ainda off = janela segura). Tambem endurecido o layout do trampoline: dados ancorados em offset FIXO 0x100 + magicas por campo + read-back + guarda de tamanho (antes: `align 8` acoplava os offsets ao tamanho do codigo — uma edicao de 5 bytes deslocou tudo e pendurou o AP em sipi1). Evidencia: make verde; validate-phase6-boot-smoke 6/6 PASS (no-op no TCG, como esperado); icount smp2 `smp: ap online apic=1`; magicas verificadas byte-a-byte no build/ap_trampoline.bin (5/5 OK em 0x11a/0x11e/0x124/0x128/0x12c). FALTA: validacao em maquina real (o ganho de cache so se prova em hw).
- [ ] Video hw real: i915 experimental gateado; campanha T61 (GM965)/T400 (4-series) exige máquina física — checklist em docs/hw_validation.md; sem ela, README/hw docs ficam honestos como estão.
- [ ] Áudio hw real: HDA validado só em QEMU; matriz AUDIO_HARDWARE_VALIDATION_MATRIX.md precisa das linhas físicas (codec verbs, power states, link training) — bloqueado: hardware.
- [ ] Rede hw real: e1000/rtl8139 são dead code honesto (P3); NICs reais T61/T400 (Intel PCIe/82567) não têm backend — P3 abre com abstração NIC; driver real é decisão de escopo da mantenedora.
- [ ] POSIX em hw real: superfície syscall é time/timing-dependente de PIT/HPET reais; validar depois do boot físico.

## Prioridade 4: SMP de verdade

- [ ] Chamar `smp_scheduler_enable()` depois do bring-up dos APs (kernel/smp.c:464 — hoje zero callers; APs ficam em `sti; hlt` para sempre)
- [x] LAPIC timer por-CPU no AP (calibrado contra PIT, vetor 0x83, stub smp_timer_stub); AP dorme em hlt e preempcao vem do tick
- [ ] Handler de IPI de reschedule (smp.c:484 so faz EOI; hlt+tick cobre, mas reschedule imediato ainda nao)
- [ ] IOAPIC: rotear IRQs por afinidade em vez de tudo no BSP (ioapic.c:526)
- [x] Respeitar o gate existente: `BOOTINFO_FLAG_EXPERIMENTAL_SMP` — VERIFICADO 15/10 que o kernel JA LE o flag (`kernel/entry.c:699` e `:788` testam contra ele). A nota anterior ("nenhum codigo do kernel o le") estava STALE; AGENTS.md:190 esta correto ("SMP so liga com o flag; padrao BSP-only").
- [x] `tools/validate_smp.py` reescrito (marcadores reais, espera todos, timeout configuravel). fallback-no-toggle PASS; smp2/smp4: bring-up markers PASS, mas stall em `fs: sys_storage_load` (race de wakeup no AP — ver P4 open abaixo)
- [ ] Retornar `QEMU_RUN_SMP` default para 2 (Makefile:94, item aberto do FINALIZATION) e provar `make run` responsivo em 2 CPUs
- [ ] Hardware real: documento de campanha T61/T400 com trampoline/SIPI rastreados (sem isso, README segue honesto em "SMP em estabilizacao")

### P4 open (evidencia 2026-10-11)

- [x] Causa raiz do #UD no AP corrigida (ordem do TSS): `smp_ap_entry` rodava `gdt_init()` antes de `local_apic_init()`, e `kernel_cpu_index()` (que le o APIC ID local) retornava 0 no AP -> AP fazia `ltr` do TSS do BSP e zerava o esp0 do BSP; os dois CPUs compartilhavam um TSS e as transicoes user->kernel corrompiam stacks (evidencia gdb: #UD com eip=3/7 e CS valendo endereco de stack). Apos inverter a ordem: nenhum #UD, todos os marcadores de bring-up do smp2 passam.
- [x] Lost-wakeup na janela enqueue->block corrigida: `scheduler_block_current_ex` aborta quando um `complete_wait` inline marcou SIGNALED durante a janela, e o wait loop do `kernel_waitable_wait_timeout` consome o `pending_signals` restante no retry; `wait_result` agora e limpo no inicio de cada wait (e no wrapper legacy) para nao abortar de forma falsa.
- [x] Aliasing heap<->arena eliminado: as arenas agora sao backed pelo proprio VA window (`app arena [10000000, 39000000) p=10000000/14000000/38000000` — identity preservado); no fallback de placement a janela sombreia o heap e passa a ser reservada (`reserved_ranges[3]`). Terra-mining do P1 fechado sem mudar ABI de boot.
- [x] Telemetria de deadlock paga o primeiro dividendo: panic de spinlock agora imprime `lock=0x%x cpu=%d` (identificou `g_scheduler_lock` no teste abaixo).
- [x] Alocacao de pid agora e atomica (`process_alloc_pid` com `lock xaddl`; 3 sites: process_create, fork, thread). `g_next_pid++` era um RMW nao-atômico — bug SMP real (pids duplicados ligariam fila ipc/registro de launch na task errada). UP sem regressao: validate-phase6-boot-smoke 6/6 PASS com o fix.
- [x] UAF de transfer corrigido (defesa em profundidade): handlers de servico pegavam `slot->data` bruto, soltavam o lock e escreviam (ATA PIO lento) enquanto o cliente poderia dar timeout e `mk_transfer_destroy` -> `kernel_free` do mesmo buffer em outro CPU. Agora `mk_transfer_data_read/write` conta referencia (`in_use`), `mk_transfer_release` devolve, e `destroy` com `in_use>0` so marca `dying` (free adiado para a ultima release). Todos os 22 sites (storage/fs/video/input/console) atualizados e balanceados. NAO e a causa da corrupcao do smp2 (persiste), mas e um UAF SMP real e necessario. UP sem regressao: validate-phase6-boot-smoke 6/6 PASS (uso pesado de transfer no boot do desktop; slot leak estouraria e nao estourou).
- [x] CORRUPCAO DA LISTA DE PROCESSOS SMP RESOLVIDA (causa: exaustao de stack dos workers 8192->32768; ver FECHADO abaixo). Evidencia: icount smp2 `desktop: session ready` zero-traco; real-time smp2 zero-corrupcao; phase6 6/6. O que resta e o wakeup race SEPARADO (proximo item aberto)., e agora com evidencia de que NAO e pid-duplicacao: o AP alterna entre pid=1 e um no fantasma `pid=4885 kind=4` (kind 4 nao existe no enum; apenas 3 tasks foram criadas) — ou seja o bloco heap do host (pid=1) esta sendo pisado por outro dono. Watchpoint de HW no campo `next` pegou UMA escrita (valor `0x12`, pc ambiguo `kernel_irq_restore+7` — TCG reporta proximo boundary). Suspeitos remanescentes: overflow de buffer vizinho no heap (cwd[256]/signal_actions[32]/fds[16] sao areas largas no fim do process_t), stack-overflow do boot stack do AP (AP_STACK_SIZE=4096 com schedule_frame+IRQ aninhada), ou double-alloc no free-list. Teste controlado AP_STACK_SIZE 4K->16K NAO corrigiu (piorou de forma diferente: traces com `cpu=174063` e `old=2065172/22`, #UD em `eip=0x1f8314`=`&g_scheduler_lock` com CS de usuario) — a corrupcao alcancar variaveis globais do proprio scheduler aponta para escrita selvagem ampla (talvez DMA/transfer ou buffer de mensagem com tamanho nao-confiado), nao overflow de stack de AP; stack revertido para 4096. Confirmado apos o refcount de transfer: a corrupcao PERSISTE com os mesmos nos fantasma em passo ~1628 (=sizeof(process_t)) e `kind` invalido (4), e o stall ainda e em `fs: sys_storage_load`. Logo o UAF de transfer NAO era a causa. `scheduler_terminate_task` nao libera `process_t` (so marca TERMINATED e limpa g_cursor/g_current), e os unicos `kernel_free` de process_t sao pre-link (seguros). Hipotese DOUBLE-ALLOCATION do heap REFUTADA com evidencia: o heap ganhou header de 16B {size,magic,next,reserved} com guards nomeados (malloc exige magic FREE; free exige magic ALLOC; DOUBLE-ALLOC/FREE-UNTRACKED/DOUBLE-FREE/OVERLAP/CYCLE com budget) e o kernel_malloc_aligned teve corrigido o bug latente shift==0 (escrevia ALIGNED_FLAG sobre o proprio header de size). validate-phase6-boot-smoke 6/6 PASS com o heap novo (UP limpo, guards sem falso-positivo). No smp2 com os guards: NENHUM trace de heap disparou (heap integro) mas o guard do scheduler pegou a assinatura nova: `LIST-CYCLE-IN-FIND node=<host pid=1> next=0x0` com o mesmo endereco alternando kind=1/kind=0 entre traces — ou seja a lista foi TRUNCADA (o `next` do host virou NULL perdendo os nos seguintes; g_cursor ficou apontando para no orfao e o walk wrap-indefinido virou ciclo). Proximo passo concreto: auditoria de quem escreve em `process_t.next`/`kind` do bloco do host SEM passar pelo heap — candidatos: buffer de mensagem ipc (`g_last_storage_request` copias), `memcpy(child, current, sizeof(process_t))` do fork com child em bloco errado, e o `scheduler_sanitize_resume_frame` escrevendo no frame salvo do host. FECHADO (evidencia desta rodada): watchpoint de HW em `process_t.next`/`kind` do host NAO pegou nenhuma escrita ilegima — so os 2 writes legitimos (create/add_task) e reads de walk. Logo os campos nunca foram escritos via aquele VA: a aliasing e FISICA. Causa real (1o de 2): o heap NUNCA foi reservado no physmem — `kernel_mm_init` so configura o allocator; as paginas fisicas do heap continuavam livres no bitmap e eram entregues a outros consumidores sob outros VAs (invisiveis a watchpoint no VA do heap). Corrigido: `physmem_reserve_range(heap_start..heap_end)` antes de `kernel_mm_init`. validate-phase6-boot-smoke 6/6 PASS com a reserva.
AINDA ABERTO: a corrupcao do smp2 PERSISTE apos a reserva (assinatura nova: `kind=0x391d5CE0 state=0x391d5CE4` — par de ponteiros consecutivos DO PROPRIO HEAP escritos em kind/state; primeiro BAD-NODE mostra `node=0x7fffffff` como ponteiro de lista). Watchpoints no VA do host continuam sem hits ilegitimos → a escrita vem de outro VA que aponta para a MESMA fisica, ou de aliasing de stack. EVIDENCIA NOVA (rodada icount): `-icount auto` reproduz a corrupcao DETERMINISTICAMENTE (mesmo ponto: primeira request de storage) — otimo para tooling. POREM os enderecos do heap do host MUDAM entre runs mesmo com icount (0x391d5c50 -> 0x391da950), entao watchpoints armados por endereco fixo NAO sao confiaveis aqui; a assinatura tambem evoluiu: `node=0x2f867` (endereco de TEXTOS de kernel como ponteiro de lista!) e pares `state=0x391DA878` (ponteiro para ANTES do proprio bloco). Walks sem lock em `scheduler_cpu_load`/`first_runnable` sao read-only (nao corrompem). Proximo passo concreto: nao mais watchpoint fixo — usar o GUARD do scheduler para dumpar o CONTEUDO completo do no ofensor (64 words) + o valor de `g_head` no momento do primeiro BAD-NODE (via trace expandido), e rodar com icount para reproducibilidade. FECHADO (rodada 12/10): (a) shadow de PC de alocacao implementado (kernel_malloc grava __builtin_return_address em block->reserved; scheduler_list_diag dumpa hdr{size,magic,allocpc}); (b) BUG REAL encontrado e corrigido: `process_alloc_pid` usava `lock xaddl` com registrador de incremento NAO-INICIALIZADO ("=&r" em vez de "+r" com valor 1) — somava lixo ao g_next_pid (observado: pids avancavam de 1628 em 1628, depois por enderecos de heap). Corrigido; pids agora monotonicos 1..N sob SMP icount. validate-phase6-boot-smoke 6/6 PASS com o fix.
ASSINATURA REFINADA (a corrupcao persiste pos-xadd, entao havia segundo bug): o PRIMEIRO BAD-NODE de cada run e um endereco de kernel TEXT (ex. node=0x2f890, dentro de scheduler_schedule_frame) — o "no" NEM COMECA num bloco de heap (hdr contem ponteiros internos ao proprio bloco, allocpc=0x0, magic≠FREE/ALLOC). Logo: algum campo `next` (ou g_cursor/g_head) recebeu um PONTEIRO WILD que nao e cabeca de bloco — nao e reciclamento limpo de bloco livre. Todos os caminhos de criacao de task setam next explicitamente (auditoria: process_create/elf_load/thread_create/fork todos cobertos); nada libera task linkada. FECHADO (rodada walks guardadas): kernel_heap_is_block() adicionado (probe de pertencimento: payload dentro de [heap_start,heap_end), 8-alinhado, header magic ALLOC + size sao); TODAS as walks do scheduler agora validam cabecalho ANTES de seguir um ponteiro (find_next, add_task, first_runnable, cpu_load, find_task_by_pid, summary, timeout_tick_hook) com cap de passos — o ciclo que prendia a g_scheduler_lock para sempre virou trace nomeado e degradao segura. Sanidade de g_head/g_cursor na entrada do find_next (cursor e o unico ponteiro que nao vem de next de task). scheduler_add_task nao aborta mais silenciosamente (ADD-ABORTED nomeado). Atribuicao de dono do link envenenado: WILD trace agora imprit prevallocpc (PC de alocacao do DONO do next) + prehdr (32 words da cauda do bloco anterior). validate-phase6-boot-smoke 6/6 PASS (UP sem regressao).

CAUSA RAIZ DA CORRUPCAO FECHADA (rodada 13/10): a forense de watermark provou a origem. Process stacks sao alocadas IMEDIATAMENTE APOs o bloco process_t (process_create_with_stack: p->stack = kernel_malloc logo apos p), e crescem PARA BAIXO; as stacks dos 6 workers de servico eram 8192 bytes. Quando um worker exauria a stack (cadeia ATA-PIO + crypto + FS profunda no boot SMP), o estouro caminhava para baixo SOBRE o proprio process_t do dono: canario destruido primeiro, depois payload, depois header — exatamente a assinatura observada (prevcnry=0 no pid=3 storage, watermark UNAVAILABLE porque o ponteiro p->stack ja estava lixo). FIX: stacks de servico 8192 -> 32768 (storage/filesystem/console/input/network/audio; video ja era 32768). Hardening de heap que PAGOU a investigacao: edge canary (16B no fim de cada bloco, escrito no malloc, verificado no free com allocpc do transgressor), watch-pc registry (free de bloco alocado em site de task emite WATCHED-FREE nomeando quem libertou), e stack watermark (memset 0xA5 + probe clean_bottom no trace WILD). EVIDENCIA: (a) icount smp2 flag-ON chega a `desktop: session start` + `desktop: session ready` com ZERO traços de corrupcao (primeira vez no repo); (b) real-time smp2 flag-ON: ZERO corrupcao (BAD-HEAD/WILD/CANARY ausentes — antes da correcao real-time mostrava BAD-HEAD); (c) validate-phase6-boot-smoke 6/6 PASS (UP sem regressao). A corrupcao da lista de processos do SMP esta RESOLVIDA.
ITEM P4 AINDA ABERTO (agora limparamente separado da corrupcao): real-time smp2 nao corrompe mas PARA na primeira request de storage — `block pid=1 svc=2 owner=2` e o worker nunca e despachado (o icount despacha). E o wakeup race ORIGINAL (hipotese: smp_wake_sleeping_cpus / IPI 0x82 perdido quando o AP esta em hlt e o host acorda no outro CPU, ou preferred_cpu do worker apontando para um CPU que nao re-entra em schedule). Proximo passo: trazar o caminho READY do worker storage sob real-time (nao icount, que o evita) — instrumentar smp_wake_sleeping_cpus com contador e o hlt-loop do AP com trace de wake.

DIAGNOSE PRECISO DO CONGELAMENTO (rodada 13/10, tempo-real vs icount): o log tempo-real termina em tick~658 (guest relógio 6.5s) apos 260s de wall — o relógio CONGELA, nao e timeout lento. Cadeia exata: `storage request type=16 pid=9->pid=1`; cpu0 despacha pid=1 (host); pid=9 bloqueia `owner=0 deadline=8860` (espera reply); pid=1 trata e volta a `block owner=2 deadline=0` (sua fila de receive — NORMAL); cpu0 passa a pid=5. Depois DISSO nada: nenhum `timer: tick` novo, nenhum trace. Como pid=9 tem deadline=8860 (88s guest) e o PIT roda a 100Hz, se o relógio avançassemos pid=9 timearia e o boot seguiria; ele nunca timea => o handler de IRQ0 (PIT) parou de rodar no BSP. O loop ocioso `sti; hlt; cli` (scheduler.c:1004) NAO segura lock durante hlt (ok), mas o caminho IRQ0 -> kernel_timer_irq_handler -> scheduler_schedule_frame faz spinlock_lock_irqsave(&g_scheduler_lock) NO TOPO do ISR: se o outro CPU (ou a propria task pid=5) segurou g_scheduler_lock e nao devolveu (sem atingir o cap de find_next, sem stuck-panic), o ISR do BSP trava adquirindo o lock, o EOI nunca sai, e o relógio para no proximo tick. Isso explica: icount OK (timing deterministico evita a janela), tempo-real congela, e NENHUM guard (corrupcao/spinlock-stuck/heap) dispara. AUDITORIA DESCARTOU A 1a HIOPOTESE: todos os retornos de scheduler_schedule_frame liberam g_scheduler_lock antes de voltar (disciplina de lock verificada linha a linha: unlock precede cada return; o halt-loop ocioso nao segura lock durante hlt). Logo NAO e "retorno com lock segurado". HIOPOTESES RESTANTES para o congelamento do ISR0 do BSP: (a) pid=5 (para quem cpu0 switchou no exato momento do congelamento) roda um caminho com IF=0 (cli) que nao cede — ex.: um spin de driver ou um loop kernel sem yield, entao o PIT nunca e tomado; (b) o EOI do IRQ0 se perde numa janela do IOAPIC/LAPIC sob timing real-time (icount evita). PROXIMO PROBE CONCRETO: (1) RESOLVIDO: pid=5 = service=1 = MK_SERVICE_INIT (a task supervisora/init; `first dispatch cpu=0 pid=5 kind=1 service=1`). Entao no instante do congelamento cpu0 aterrissa no init supervisor (que roda `init: supervisor idle`) e o IRQ0 do BSP cessa — nao e pid=5 fazendo algo ruim, e o init sendo a ULTIMA task antes do relogio parar. Isso aponta para (b) perda de EOI/tomada de IRQ0 do BSP, nao para uma task especifica mal-comportada; (2) instrumentar a ENTRADA do ISR0 (kernel_timer_irq_handler) com um contador por CPU impresso periodicamente, para distinguir "PIT nao dispara" de "dispara mas o handler nao completa"; (3) checar se pid=5 entra em algum caminho com cli sem sti/substitui o `sti; hlt; cli` do idle por `sti; hlt` sem o cli final (o cli pode mascarar o proximo IRQ se find_next retornar NULL repetidamente).

DEADLOCK DE SERIAL RESOLVIDO (rodada 13/10, parte 2): a medicao HMP ao vivo (xp/1wx 0x33d054 duas vezes + info registers por CPU + bt gdb) refutou a hipotese "relogio parado": g_kernel_ticks AVANCA durante o suposto congelamento (0x1ec49->0x1f545 em 10s) — o guest esta vivo, o que para e o TRACE (orcamento) ou o sistema entra em deadlock de lock. O deadlock real: ambos CPUs em spinlock_lock(&g_serial_lock), lock=1, e o DETENTOR DESAPARECEU (owner-stamp=NONE). Causa: a aquisicao BLOQUEANTE do g_serial_lock e insegura porque (a) o caminho de print aninha (stuck-detector->printf, panic->printf) e (b) um detentor que morre no meio do print vaza a lock para sempre. FIX NA CAUSA RAIZ: o canal de debug nunca pode bloquear — serial_enter agora e trylock com IRQs salvos; em contencao o chamador escreve RAW (entrelacamento embaralha caracteres; bloqueio perde o boot inteiro). Complementos: kernel/lock.c ganhou spinlock_stuck_report() manual/lock-free (o detector de stuck nao pode morrer na lock que anuncia — evidencia: antes o relatorio nunca saia; agora `spinlock: stuck lock=0x33fff0 cpu=0` APARECEU), e kernel_panic fala raw primeiro (sobrevive a qualquer estado de lock). EVIDENCIA: (a) real-time smp2 flag-ON: NENHUM `spinlock: stuck`, relogio avanca de tick~658 para tick=9321 (14x mais longe), `fs: sys_storage_load returned` x2; (b) a saida entrelacada (`eexception`,`exceptikernel_panic`) CONFIRMA o fallback raw funcionando sob concorrencia sem travar; (c) validate-phase6-boot-smoke 6/6 PASS (UP sem regressao — o caminho serial e usado por TODO print). ARQUIVOS: kernel/drivers/debug/serial.c (trylock), kernel/lock.c (stuck report raw), kernel/panic.c (panic raw-first).

PROXIMO ITEM ABERTO REVELADO (estava mascarado pelo deadlock): real-time smp2 flag-ON agora morre em `#GP err=80 eip=0x391fb7fa cs=8` — um ENDERECO DE HEAP executado como codigo (jump selvagem / retorno corrompido), disparado no lancamento do desktop (`launched pid=11 prio=desktop` -> `host: argv startx` -> carregamento de assets icones). Nenhum trace de corrupcao de lista (BAD-NODE/WILD/CANARY) dispara antes — e uma segunda superficie de falha, distinta da corrupcao da lista (ja resolvida) e do deadlock de serial (resolvido nesta). Hipotese inicial: um function-pointer/vtable ou retorno em bloco de heap corrompido no caminho startx/assets; icount pode ou nao reproduzir (timing). Proximo passo concreto: rodar icount smp2 ate o mesmo ponto (pid=11 desktop) para ver se o #GP e deterministico; se sim, watchpoint no eip via gdb para pegar a instrcao de salto; se nao, instrumentar o caminho de assets/startx-host para ponteiros de funcao nao-confiados.

CLASSE DE FALHA #1 FECHADA (rodada 13/10 parte 3): ponteiros de usuario NAO validados na camada de syscall. Prova viva: diff byte-a-byte do .text do guest (HMP xp) contra o ELF — 0x30bb3/0x30bb4 corrompidos com dados de arquivo enquanto o desktop carregava assets; o #GP `eip=0x515d2` era a instrucao CORROMPIDA ali (opcode `cf` = iret com resto) — jump para dentro de .text destruido. O kernel copiava dados de disco para enderecos que a app passava sem checagem (memcpy/ATA inw direto em (void*)(uintptr_t)ptr). FIX: 58 guards em TODOS os sites de ponteiro de usuario (storage bulk, fs, network, audio, transfer, mensagens/eventos, cwd, readdir/readlink/mkdir/opendir/utimensat/execve, paleta, launch name/argv, shm, waitpid status, snapshot/status/budget/health, console strings, munmap/madvise) com predicado de pagina novo `paging_va_user_accessible()` (P|U por pagina; cobre arenas + mmap 0x40000000+; janela baixa identity-mapped tem U entao o window-check e necessario mas nao suficiente). Politica: tarefas ring0 (servicos bootstrap) confiadas — zero regressao; ring3 validado. EVIDENCIA: zero EFAULT em UP/smp2 real-time/icount (nenhuma rejeicao falsa); UP `desktop: session ready`; real-time smp2 foi MAIS LONGE QUE NUNCA (tick=1290, `host: desktop session launched` — antes: #GP em 697).

EXPERIMENTO CONTROLADO (honestidade): a assinatura icount do 12f1ce7bd (desktop ready limpo) NAO se manteve no HEAD pos-serial-fix MESMO SEM os guards — stashando os guards e rodando HEAD: BAD-NODE/WILD com `prevcnry=0` no pid=3 e `node=0x2aaec` (texto de kernel como ponteiro) voltou. Logo: (a) os guards nao sao regressao — deslocaram a falha (#UD eip=3 em icount com guards vs BAD-NODE sem); (b) o stack-bump 32768 MAQUOU o segundo writer uma vez, nao o eliminou; o serial-fix (trylock/interleaving) reabriu a janela de timing. CLASSE DE FALHA #2 RESOLVIDA (ver FECHADA abaixo): writer forward-overflow kernel-internal — o canario do bloco process_t do pid=3 (host storage) e destruido ENQUANTO o bloco esta vivo e alocado (prevcnry=0), com dados shape de stack (enderecos de retorno/.text) nos campos. NAO e ponteiro de usuario (zero EFAULT prova), NAO e double-alloc (guards de heap nunca disparam), NAO e free-while-linked (WATCHED-FREE nunca disparou). PROXIMO PROBE CONCRETO: identificar o VIZINHO — o bloco alocado imediatamente ANTES do process_t da vitima (dumpar allocpc+tamanho do predecessor fisico via walk do free-list/header no momento do primeiro prevcnry=0), ou varredura periodica de canario sobre todos os blocos ate o primeiro trashed + atribuicao; candidatos estruturais: buffers de mensagem ipc com length nao-confiado copiado para dentro de bloco de heap, AppFS header read, ou caminho de asset com memcpy de tamanho externo.

CLASSE #2 FECHADA (rodada 13/10 parte 4): o probe de predecessor fisico pagou na hora. `kernel_heap_block_predecessor()` (walk contiguo do heap sob g_heap_lock, sem confiar no header da vitima) ligado ao trace WILD quando prevcnry=0 capturou: `OVERFLOW-WRITER pred{start=0x391de990 size=8224 allocpc=process_create_with_stack+0x1bd cnry=0}` — 8224 = 16(hdr)+8192(payload)+16(canario): uma STACK de 8192; e o watermark da vitima mostrou `pid=3 st=0x391e1040 size=8192`. Os dois unicos donos de stack-8192 restantes eram `video-present` (kernel/microkernel/video.c:344) e `video-control` (:478) — launches auxiliares de driver que ficaram de fora do bump 8192->32768 da rodada anterior (os 6 servicos principais + o launch principal de video ja estavam alinhados). GEOMETRIA CORRIGIDA PELO PROBE: o escritor nao era o vizinho ABAIXO da vitima (como a hipotese forward-overflow dizia) — era a stack ACIMA do process_t do PROPRIO dono da vitima, estourando PARA BAIXO ~1700 bytes: topo-da-stack do dono -> header do bloco da stack -> canario do process_t -> payload do process_t (campos stack/context viram lixo -> watermark UNAVAILABLE) -> header do process_t (magic=0x16) -> canario da bloco-vecinho-abaixo (pred cnry=0). O trace de prevcnry=0 era o RABO do mesmo caminho de escrita, nao um escritor independente. FIX: video-present/video-control 8192->32768. EVIDENCIA: icount smp2 flag-ON (antes 100% deterministico na corrupcao): `desktop: session ready` + `desktop: visual ready` com ZERO traços (nenhum BAD-NODE/WILD/CANARY/OVERFLOW-WRITER/#UD/panic) — primeira vez que o icount passa do desktop com os guards de ponteiro ativos; validate-phase6-boot-smoke 6/6 PASS.

SUPERFICIE NOVA ABERTA (classe #3, real-time smp2 apenas): com a classe #2 fechada, o real-time smp2 agora mostra o proximo bug — pid=1 (host storage) retoma execucao com EBP/ESP apontando para o PROPRIO bloco process_t: a primeira chamada kernel_debug_printf empurra `{ret=0x44956 (dentro de serial_enter), saved_ebp=0x391d5cd4 = &pid1->wait_result}` em +0x28/+0x24, esmagando kind/service_type (escrita dirigida de 8 bytes — header fica INTACTO, magic=ALLO size=0x680 validos; NAO e overflow). Cascata: host storage vira zumbi -> servicos marcados offline no tick 1579 -> boot para (relogio VIVO, tick=4000+, sem freeze, sem panic). Pergunta aberta: o frame salvo de pid=1 ja estava com esp/ebp dentro do bloco QUANDO? (a) frame inicial corrompido no topo da stack antes do 1o dispatch, (b) context pointer redirecionado para o payload do proprio process_t, ou (c) frame salvo num block/yield com regs ja ruins. PROXIMO PROBE CONCRETO: validar no DESPACHO (scheduler_schedule_frame, caminho next!=current): `next->context` deve cair dentro de [next->stack, next->stack+next->stack_size) e o frame (edi..eflags 44B) deve ter esp/ebp dentro dessa janela — violacao imprime DISPATCH-BAD-FORENSICS{ctx,stack,size,frame[11],clean_bottom} ANTES do iret para o frame podre; isso pinça o momento exato em que o frame se tornou ruim e distingue (a)/(b)/(c).


RESULTADO DOS PROBES DE CLASSE #3 (rodada 13/10 parte 5): os tres probes foram implementados e rodados em real-time smp2. (1) DISPATCH-BAD-FORENSICS (valida ctx+frame contra a janela de stack no despacho;blind-spot fechada: task despachada sem stack valida agora imprime BAD-FORENSICS-NOSTACK em vez de skip silencioso): NAO disparou em nenhum run — frames despachados estao INTACTOS. (2) Teto de sector_count (wrap uint32: sector_count>=0x800000 zerava byte_count=count*512 e navegava pelos checks de tamanho; guards em 6 sites: handlers BLOCK_READ/BLOCK_WRITE, clientes read/write_sectors, syscalls backend read/write) — hardening correto e aditivo, mas NAO-EXERCITADO (zero REJECT nos runs): defesa em profundidade, NAO a causa da classe #3. (3) Canary sweep no tick hook (varredura orcamentada a cada 8 ticks; primeira violacao nomeia vitima + predecessor e congela): NAO disparou — blocos process_t intactos nos runs observados. CONCLUSAO HONESTA: a classe #3 como formulada (frame/heap pisado durante execucao) NAO se confirmou com os probes; a assinatura migrou de novo.

CLASSE #4 — ASSINATURA ATUAL (real-time smp2, evidencia viva df3 13/10): cpu0 em CPL=0 CS=0x08 deslizando por ZEROS da arena de app (EIP 0x025dabb8->0x028e6000->0x031ff000 em amostras de segundos; codigo executado = `addb %al,(%eax)` = bytes 00 00), ESP=0x0000000b lixo, EFL=0x82 (IF=0 — por isso o relogio congela: PIT no BSP que nunca toma IRQ; cpu1 permanece vivo). g_current[0]=pid=9 (lang host, frame salvo em yield — sane). O log serial morre entre `lang: arena ok` e `lang: image read ok` durante a SEGUNDA carga de app lang (a primeira — desktop pid=12 — completa e lanca ring3 normalmente). TESTE CRUZADO UP: a mesma sequencia de carga lang roda LIMPA em UP (image read ok -> prepare ok -> launching ring3 child -> desktop: session ready -> tick=4000) — logo a falha e ESPECIFICA DE SMP, nao do lang_loader em si. HIPOTESE PRINCIPAL: o task de stub ring3 criado por sys_task_create(stub_entry na arena, ...) as vezes recebe o caminho de contexto inicial RING0 (process_setup_initial_context em vez do user) por corrida no branch de privilegio — o stub executa o app DENTRO da arena em CPL=0, e o codigo do app (esperando stack de usuario) destrói esp/regs sem nenhuma estrutura de kernel ser pisada (todos os guards silenciam — so o estado da propria task sofre). CLASSE #5 (real-time smp2, 13/10 parte 6): apos os fixes de hw-real, o crash real-time migrou para a PRIMEIRA troca de storage — `#UD kernel-mode eip=3 cs=8` / `eip=7` / `eip=0x213` / `eip=0x217` (sempre inteiros pequenos = dados lidos como ponteiro), com o DISPATCH-BAD-FORENSICS MUDO (frames validos no despacho) e o canary sweep disparando so DEPOIS. Padrao dos runs: storage request type=16 pid=9->pid=1; pid=9 bloqueia; o host storage (pid=1) e/ou o stub ring3 (pid=11) sao despachados e o eip vira lixo. HIPOTESE PRINCIPAL: double-dispatch/steal — a mesma task RUNNING em dois CPUs por corrida na selecao (o filtro `state==READY && current_cpu<0` em find_next tem uma janela entre o set de current_cpu e a publicacao de estado sob g_scheduler_lock), resultando em dois CPUs irem ao mesmo frame de uma task so. PROBE RODADO E REFUTADO (13/10 parte 7): assercao anti-double-dispatch adicionada no despacho (imprime DOUBLE-DISPATCH se `next->current_cpu >= 0 || next->state == PROCESS_RUNNING` antes de reivindicar) — NAO disparou em real-time smp2. Double-dispatch REFUTADO: o bloco find_next+dispatch roda inteiramente sob g_scheduler_lock (adquirido antes de ler g_current[cpu], solto so apos a decisao), entao a mesma task nao pode ser reivindicada por dois CPUs. EVIDENCIA NOVA REFINADA: o valor corrompido recorrente e `cs=0x1f9374 = &g_scheduler_lock` (a propria lock do scheduler como seletor de segmento!), e em runs alternados o frame despachado de pid=11 traz `{eip=5, cs=2, eflags=1}` — a corrupcao e TIMING-DEPENDENTE (aparece num run, nao no proximo). Isso + frames validos no despacho em outros runs aponta para a STACK/TSS do AP: quando o AP executa `int $0x81` (yield_stub) ou toma o timer LAPIC, o hardware troca para esp0 do TSS; se o TSS por-CPU do AP estiver com esp0 errado/desatualizado, o frame e empilhado na regiao errada (uma stack que ainda contem a chamada `spinlock_lock(&g_scheduler_lock)` → o endereco da lock vira "cs"/"eip"). HIPOTESE A TESTAR: `kernel_tss_set_kernel_stack()` usa `kernel_cpu_index()` que retorna 0 se o LAPIC do AP nao estiver mapeado — o AP escreveria o esp0 do TSS do BSP (colisao de TSS cross-CPU). PROBE DO TSS/TR RODADO E REFUTADO (13/10 parte 8): adicionado `kernel_current_tr()` (le o TR real via `str`) e assercao no despacho comparando com `kernel_cpu_tss_selector(cpu_index)` — TR-MISMATCH NAO disparou: o TR de cada CPU bate com o seletor esperado para o seu cpu_index, logo kernel_cpu_index() e o hardware TR concordam. Hipoteses de double-dispatch E de TSS trocado estao AMBAS REFUTADAS com evidencia. EVIDENCIA DA VARREDURA (mesmo run): `sched-scan: BAD-CANARY pid=1 node=0x391d5c80 allocpc=process_create_with_stack` com `stack{st=0x391d6300 size=32768 UNAVAILABLE}` e `writer-candidate pred{start=0x391d5c40 size=48 allocpc=0x32323 cnry=1}`. pid=1 (storage host) tem stack de 32768 (ja bumpado) e ainda assim o campo `stack` le UNAVAILABLE — provavelmente o PROPRIO campo foi corrompido pelo escritor (nao um underflow real: os buffers de stack no caminho storage/ata/ipc sao modestos, 512B de verify + ~512B de struct mk_message). O predecessor fisico e um bloco de 48 bytes (ramfs_create). PROBE DE PROVENIENCIA DA STACK RODADO (13/10 parte 9): adicionada validacao de `stack`/`stack_size` no `scheduler_add_task` (link) e o dispatch ja valida no despacho (BAD-FORENSICS-NOSTACK). RESULTADO: NENHUM disparo — a stack de pid=1 e valida tanto no link QUANTO em todo despacho, mas a varredura a encontra podre depois => a corrupcao acontece ENQUANTO pid=1 esta BLOQUEADO (entre despachos), nao em criacao nem durante execucao ativa. Isso estreita para o estado de bloqueio/waitable. VARREdura COM EXTENSAO (EXTENT bad=N total=M): num run o sweep viu `bad=0 total=4` (4 tasks todas SAUDAVEIS) e mesmo assim o `#UD eip=3` ocorreu em tick~2500 => a corrupcao de canario e o #UD sao INTERMITENTES e nem sempre coincidem: o #UD pode ser um salto para dados corrompidos por outro caminho (nao necessariamente o bloco de task). ASSINATURA FINAL DO #UD REFINADA (13/10 parte 9, com cpu= no handler): `#UD kernel-mode cpu=1 eip=217 cs=0` — o AP (cpu=1) falta com CS=0 (seletor NULO). O trace imediatamente anterior e a prova: `scheduler: cpu=1 old=5/0 next=958429020/174969 state=2069364` — o AP DESPACHOU uma task cujo `pid`=0x3920d1dc (ENDERECO DE HEAP no campo pid!), `service_type`=174969 e `state`=2069364, todos lixo — o mesmo padrao historico (`kind`/`state` recebendo ponteiros de heap). Nenhum guard dispara: o bloco passa em scheduler_node_sane (e um bloco de heap valido), mas seu CONTEUDO (pid/kind/state/context) foi corrompido. O AP entao iret'a para o frame podre -> cs=0/eip=0x217. O DISPATCH-BAD-FORENSICS nao pegou porque o frame no momento do check ainda passava, ou a corrupcao do conteudo acontece logo apos o check sob o lock. NATUREZA: race de SMP profunda no scheduler/waitable, SOMENTE real-time (icount e UP limpos), intermitente, que escreve ponteiros de heap em campos de task. NAO gateia boot de hardware (que e UP). Proximo probe: (a) capturar via gdb o write no campo `pid`/`state` de uma task (watchpoint no endereco do bloco, nao em VA fixo — usar o allocpc do bloco vitima); (b) auditar caminhos que escrevem em `process_t` a partir de um indice/offset derivado de dado nao-confiado (ipc/waitable); (c) considerar se `scheduler_find_task_by_pid`/waitable queue fazem lookup por um pid ja corrompido e escrevem no bloco errado. Registrado como item aberto de estabilidade SMP — ver nota de prioridade abaixo.

BUG DE RUIDO CORRIGIDO (14/10): o `sched-scan: EXTENT bad=N total=M` imprimia INCONDICIONALMENTE a cada 8 ticks (budget 2000) — milhares de linhas identicas "bad=0" inundavam o serial e afogavam os marcadores que os harnesses de validacao (validate-network-surface etc.) greppam. Agora so imprime quando `bad_count != 0` (varredura limpa e silenciosa). Isto foi introduzido por mim na rodada do sweep e corrigido aqui.

NOTA DE PRIORIDADE (mantenedora, foco hardware real): a classe #5 e exclusiva do caminho real-time smp2 e NAO bloqueia o objetivo de bootar em maquina real (boot fisico e UP). A cadeia de boot hardware-real esta em boa forma apos as rodadas: A20 (bug fatal de codigo-morto corrigido), retry de disco + media reset, caches CR0.CD/NW ligados, trampoline do AP ancorado/verificado. Os itens que dependem de hardware fisico (video i915, audio HDA, rede, USB mass-storage) permanecem campanhas bloqueadas por falta de maquina — sem ela, o contrato proibe claimar verde. PROXIMO PROBE CONCRETO (retomavel): (a) instrumentar o caminho de BLOQUEIO/waitable de pid=1 para dumpar quem escreve no estado da task bloqueada; (b) gdb watchpoint no bloco vitima (via allocpc) para pegar a instrucao que escreve o ponteiro de heap no campo pid.
ENDURECIMENTO DE WAITABLE + HONESTIDADE (14/10 parte 10): a cadeia `wait_next` de waitable era o ultimo linked-list do kernel SEM defesa (scheduler list ja tinha node_sane+cap+diag). Adicionados node-sane (`kernel_heap_is_block`) + cap de passos + trace orcado `CHAIN-BAD` em enqueue/detach, e guard `enqueue-already-linked` (re-enfileirar task com wait_next!=0 = cross-chain, deve ser inalcanzavel). EVIDENCIA: build verde; UP `desktop: session ready` + `desktop: visual ready` com guards ativos e silentes; validate-phase6-boot-smoke 6/6 PASS. RESULTADO HONESTO: CHAIN-BAD NAO disparou em NENHUM run (UP nem real-time smp2) => a cadeia de waitable NAO e a escritora observavel da classe #5; o endurecimento fecha um bug LATENTE legitimo em si (protege cross-chain/stale-node), mas NAO avanca a causa raiz da classe #5. Classe #5 segue ABERTA e nao-gating: em real-time smp2 a manifestacao muda a cada run (canario pisado, #UD eip=inteiro-pequeno no AP, e agora `EXTENT bad=0 total=0` = o PROPRIO g_head apontando para nao-bloco), sempre com TODOS os guards de heap/scheduler/waitable silentes — caracteriza escrita por caminho que nenhum instrumento atual cobre. DESCARTADOS na sequencia: g_last_storage_request/reply (helper-diagnostico morto, mk_storage_service_last_request sem chamadores, nao toca blocos alheios); scheduler_complete_wait (corretamente sob g_scheduler_lock, inline_wake soundo). NEGATIVOS sobre estrategia de captura (14/10 parte 11): (1) a escrita da classe #5 NAO e no campo `pid` (offset 0) — mira kind/service_type em +0x24/+0x28 e, num run, o PROPRIO g_head; watchpoint em ->pid mira alvo errado (g_head=0x391d5c80 confirmado estavel entre runs). (2) anexar gdb (-s) SUPRIME a corrida: com gdb conectado o `storage request type=16 pid=9->pid=1` retryou limpo ate tick=4000 SEM corrupcao — o pause/serialize do TCG remove a concorrencia real-time que a dispara, entao watchpoint hardware AO VIVO nao consegue observar a classe #5. CONCLUSAO: classe #5 e uma corrida de timing SMP real-time genuina, nao-gating (boot de hardware e UP, limpo; smp2 e flag experimental off por padrao), escrevendo campos de process_t por caminho onde todos os guards atuais calam E que a propria observacao esconde. Se retomada, abordagens NAO-observacionais: (a) auditoria estatica de todo caminho que pega endereco derivado de campo de mensagem/IPC e escreve atraves dele; (b) write-trap in-guest (remapear o bloco vitima read-only via PTE e pegar o #PF do escritor) que NAO pausa o guest; (c) aceitar como instabilidade SMP experimental conhecida-aberta. NAO queimar mais rodadas em watchpoint gdb de endereco fixo (provado contraproducente).

PROXIMO PROBE CONCRETO (classe #4): instrumentar o branch ring3-vs-ring0 em sys_task_create/syscall.c (imprimir modo escolhido + runs_in_user_mode + entry por task criada; assert: entry em janela de arena exige modo ring3) e rodar real-time smp2 ate a segunda carga lang — se a trace mostrar stub em ring0, o fix e forcar o modo pelo PARAMETRO do syscall (nunca herdado do chamador).
ASSINATURA DEFINITIVA (smp2 icount, rodadas acumuladas): os headers corrompidos contem ENDERECOS DE RETORNO DE FUNCAO DE KERNEL (0x2a9ef=spinlock_lock_irqsave, 0x2ebd6=process_create_with_stack) e FRAGMENTOS ASCII de enderecos impressos ("2ebd","62d0" = substrings de %x do serial printf). Isto e corrupcao em FORMA DE STACK / overflow-forward de buffer vizinho, NAO uso errado da API de heap (nenhum guard de heap disparou), NAO duplicacao de pid (corrigida), NAO UAF de transfer (corrigido). O dono do primeiro next envenenado e consistentemente pid=2/pid=3 (alocados em process_create_with_stack=0x2ebd6). Proximo passo concreto: (1) identificar o bloco alocado IMEDIATAMENTE ANTES da vitima 0x391da950 (header em 0x391da940) e auditar seu escritor — o prehdr ja mostra cauda auto-referente (0x391da94c, 0x391da98c) sugerindo que o vizinho e outra process_t/waitable apontando para dentro da vitima; (2) canario de borda no heap: padding de 16 bytes apos cada payload verificado em kernel_free (pega o overflow-forward no momento do free, com allocpc do transgressor); (3) conferir se os buffers char[16] do serial printf (%x) podem estourar com valores grandes — os fragmentos ASCII "2ebd"/"62d0" vem dai. — em scheduler_list_diag, verificar se o `next` do prev eh cabeca de bloco valida (magic ALLOC e payload dentro de [heap_start,heap_end)) ANTES de seguir, e dumpar 16 words ao redor do prev quando nao for; (2) check de `g_cursor[cpu]`/`g_head` sanos no entry do find_next (cursor e o unico ponteiro de lista que NAO vem de um next de task); (3) suspeita nova: scheduler_add_task com LIST-CYCLE-AT-ADD aborta o append silenciosamente (p->next==NULL guard) — task criada mas nunca linkada + registro de launch apontando pra ela pode produzir referencias penduradas; verificar se algum launch retornou pid sem task na lista nos logs.
Watchpoint de HW em `g_head` (0x1f84a0, .bss) e no par de words `0x391d5CE0` — e auditoria de `scheduler_timeout_tick_hook` (arrays `woke_tasks[16]`/`woke_inline[16]` com `woke_inline[woke_count - 1u]` indexado APOS o incremento — verificar off-by-one e o cap 16 vs wake de >16 tarefas no MESMO tick). AUDITADO 16/10: sem off-by-one (`woke_count++` pos-incremento indexa certo); cap 16 so descarta o REGISTRO de trace/evento, nunca o estado da task (que virou READY antes do cap). Nao e o mecanismo do stall.
- [x] **ITEM 1 "Chamar smp_scheduler_enable()" JA ESTAVA FEITO** (16/10): chamado em `kernel/entry.c:796` apos bring-up; real-time smp2 mostra `smp: scheduler enabled cpus=2` + dispatches `sched: cpu=1 ...` (AP roda tasks pid=5/pid=9). A nota "zero callers" estava stale.
- [x] **RACE DE WAKEUP DO STORAGE NO AP — FECHADO (16/10, medido):** real-time smp2 no HEAD atual completa a primeira cadeia de storage — serial mostra `service: storage request type=16 from pid=9 to pid=1` → `fs: sys_storage_load returned` → `fs: persistent image invalid, rebuilding` → `init: storage smoke begin` → `init: storage smoke ok` → assets carregam. O stall antigo em `fs: sys_storage_load` NAO existe mais; a parede do smp2 agora e SO a corrupcao de lista (abaixo). (A reserva de physmem do heap + atomic pid + refcount de transfer + stacks 32K coletivamente fecharam o caminho.)
- [ ] **PAREDE ATUAL DO SMP2 (classe #5, assinatura REFINADA 16/10):** dois boots real-time gravados com o census estendido (guardas novos `sched-head:` e `process-destroy: STILL-LINKED` no mesmo commit):
  - Run A (NIC default): `sched-list: BAD-HEAD cpu=1 node=0x390aef80 hdr{size=0x680 magic=ALLO allocpc=0x0} pid=0 kind=<addr-heap> state=1 next=0x1` e o MESMO node impresso imediatamente depois com header DIFERENTE (`size=0x1 magic=0x1000000`) — o bloco esta SENDO REESCRITO AO VIVO (dono novo, bytes de storage/FAT: `prehdr{0x6 0x68516 ...}`). g_head fica pendurado → find_next=NULL → dispatch de tasks novas para → boot congela antes de `desktop: session ready` (mas o init/desktop host JA rodaram — o congelamento e POST-storage-load).
  - Run B (`-nic none`, controle): reproduz SEM nenhuma NIC — os drivers e1000/rtl8139 desta semana sao INOCENTES. Assinatura: `BAD-NODE-AT-ADD/FIND node=0x390748f0 kind=4 pid=0x1DA2...` (kind invalido, passo ~0x10/0x30 — padrao process_t-como) + `exception: #UD kernel-mode cpu=0 eip=3 cs=0`.
  - `process_destroy` nao e o caminho conhecido do free: auditoria dos 3 sitios
    de destroy (execve `loaded`, launch register-fail, create stack-fail) e do
    `elf_loader.c:262` (o unico `kernel_free` de process_t fora do destroy)
    mostra todos PRE-LINK — o proc do elf_load nunca passa por
    `scheduler_add_task` (grep: add_task so existe em syscall.c:2016/2479/3757).
    O guard `process-destroy: STILL-LINKED` (recusa o free e nomeia o caller)
    fica no tree como prova viva; **rodada de controle COM o guard buildado
    (`smp_nonic.log` 16/10): 0 disparos na serial inteira enquanto a parede
    reproduz normal (`launch failed`+`#UD eip=0/3`)** — `process_destroy` esta
    EXCLUIDO como libertador do bloco linkado por via conhecida; resta o probe
    do registro de blocos-linkados checado em TODO `kernel_free` (abaixo).
  - **STATUS HONESTO DO GATE 16/10:** `make validate-smp` agora FAIL em
    `smp2-experimental`/`smp4-experimental` — os marcadores de bring-up
    (online cpus, lapic armed, scheduler enabled) PASSAM, mas a lista atual do
    tool exige `userland.app: shell start`, que a parede classe #5 impede em
    tempo-real. Reproduzido IGUAL no build 2bc67f93c antes desta rodada (inclusive
    `-nic none`) — logo NAO e regressao desta rodada; o "[x] bring-up markers
    PASS" de 14/10 continua verdadeiro para os marcadores de bring-up, mas o
    gate como um todo esta vermelho por causa da parede (registrado aqui para
    nao existir verde-falso por omissao).
  - **PROXIMO PROBE CONCRETO (desenhado, nao executado):** registro de 4-8 slots com os ponteiros de process_t ATUALMENTE LINKED (adicionados em scheduler_add_task, removidos so em terminate-definitivo); `kernel_free` verifica contra o registro e, em hit, imprime ORCAMENTO-ZERO `"free: TASK-BLOCK 0x.. pid=.. caller=0x%x"` com o PC do chamador. Isto distingue em UMA linha free-ilicito vs escrita-alias e nomeia o writer sem gdb (gdb suprimia a corrida).
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

- [x] Placeholders desonestos REMOVIDOS (15/10): `/bin/js` era um REPL FALSO que ecoava a linha digitada como "Result: <linha>" fingindo avaliar; `/bin/ruby` e `/bin/python` afirmavam "maps to JS engine internally" (MENTIRA — não mapeavam a nada) com cara de sucesso. Os tres agora emitem diagnostico explicito e legivel por maquina de "NOT IMPLEMENTED (ENOSYS)" em vez de fingir. Prova executada (boot rescue, `js` digitado, serial): `js: NOT IMPLEMENTED (ENOSYS) - no JavaScript engine is linked into this build.` ... `app: main returned` + `init: task-event terminated pid=12` (app roda, informa e sai limpo). Exit code mantido 0 de proposito: a shell resgate MORRE em rc!=0 (`userland/modules/shell.c:332-336`) e a convencao desta userland p/ "comando nao executou" e exit 0 + mensagem (`busybox` "unknown command"); a garantia anti-falso-verde vem da ausencia total de comportamento fake, nao de um rc que mataria a sessao. `js_main_real.c` (consumidor honesto da API QuickJS) permanece no arvore como caminho futuro documentado.
- [ ] quickjs: reativar bloco comentado (Makefile:805-828) e `quickjs_wrapper.c`; app `js` real em vez do eco de placeholder — ATTENZIONE: o fake-eco nao existe mais (linha acima); falta o porto libc freestanding p/ linkar quickjs.c (60k linhas), entao NAO e so "descomentar"
- [ ] micropython e mruby: mesma coisa, um por vez, com heap/stack por classe de app respeitando MEMORY_ADAPTIVE_RAM_POLICY
- [ ] Java: `java`/`javac` caseiros (VJ01) — registrar como experimental honesto ou apontar caminho de substituicao; nao prometer JVM
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

- [x] docs/README.md Project Status HONESTO (15/10): a tabela dizia "✅ Complete" para rede (e1000/rtl8139/7260), Wi-Fi WPA2, USB BOT/NVMe e POSIX/X11 — tudo falso. Verificado linha a linha contra o codigo e reescrito com semantica explicita (✅ = ha report em build/ ou target validate-*; ⚠️ = codigo sem validacao; ❌ = fachada/dead code com gap no plano): e1000/rtl8139 marcados DEAD CODE (grep: zero callers fora do proprio arquivo), Wi-Fi marcado FACHADA (wifi_read_pci_bar0 le BAR0 de bus 0:slot 0:func 0 FIXO), WPA2 marcado ORFAO (cripto real sem caminho 802.11/CCMP), USB BOT/NVMe marcados NAO VALIDADOS, rede virtio marcada FUNCIONAL com evidencia do validate-network-datapath.
- [x] KNOWN_BUGS.md header corrigido (15/10): dizia "Todos os bugs conhecidos foram corrigidos" — FALSO (classe #5 aberta) — e "Rede: completa — ... WiFi Intel 7260 com WPA2 real" — FALSO. Agora tem secao "Abertos" (classe #5 com natureza/nao-gating + a inconsistencia nova do USB host count) e o status geral descreve e1000/rtl8139/WiFi como sao.
- [x] NETWORK_AUDIO_PANEL_PLAN.md corrigido (15/10): a linha "ping works via ICMP client but `ping localhost` is fabricated" estava DESATUALIZADA pelo trabalho de 14/10 — loopback 127/8 e real no kernel agora (socket→enqueue→recv, provado pelo validate-network-datapath). Resto do arquivo ja marcava Wi-Fi como NOT functional corretamente.
- [x] FINALIZATION_EXECUTION_PLAN.md linha 20 corrigida (15/10): a linha juntava "DHCP/ARP/ICMP/UDP/TCP client" num unico [x] FUNCIONAL. Agora separa: DHCP/ARP/ICMP client + responders + loopback = FUNCIONAL (com comando de prova); UDP=so DHCP e TCP cliente sem validacao end-to-end = linha propria aberta.
- [x] hw_validation.md linha 30 corrigida (15/10): "USB mass-storage via UHCI = Validated" ambiguo/otimista — agora diz BIOS-boot media validada (phase6 usb-bios-boot) e backend runtime nativo NAO verificado.
- [x] P4/AGENTS.md "SMP gated por flag" verificado e NAO era falso (15/10): o kernel LE o flag — `kernel/entry.c:699` e `:788` testam `BOOTINFO_FLAG_EXPERIMENTAL_SMP`. O item P4 que dizia "nenhum codigo do kernel o le" ficou stale; corrigido na prioridade P4 (abaixo).
- [ ] `make` deve sair sem warnings: alvo `build/libksh.a` — ja deduplicado em Build.compat.mk:132-135 (comentario explica); varrer os 229 TODO/stub/n.i. do grep inicial e converter cada um em: feito, bug em KNOWN_BUGS.md, ou linha neste plano
- [ ] `universal-rule.md` ganha um paragrafo novo so se nascer contratu — registrar decisoes em docs/guidelines como DECISIONS pontuais

### P9 — achados de codigo durante a varredura de honestidade (15/10)

- [x] BUG DE DIAGNOSTICO NO CAMINHO DE BOOT CORRIGIDO: `kernel_debug_printf` (kernel/drivers/debug/serial.c) fazia `++p; switch(*p)` SEM parsear largura/flags, entao TODO `%02x`/`%04x`/`%08x` imprimia o specifier LITERALMENTE — 34+ sites, incluindo a enumeracao PCI inteira (`pci: %02x:%02x.%x %04x:%04x`), o BDF do USB e o match de driver. Um log de boot que mente e a mesma classe de defeito que um teste que mente. FIX: parse de flags/width + helper `serial_emit_field` (zero-pad quando `%0N`, espaco caso contrario, `-` alinha a esquerda; sinal tratado fora do padding; INT_MIN seguro). PROVA: boot e grep — `pci: 00:00.0 8086:1237 class=06 subclass=00 progif=00 irq=0`, `usb: UHCI at 00:01.2 (8086:7020)`, `bsd_usb: UHCI at 00:01.2 IO=c040 IRQ=11`, e ZERO ocorrencias de specifier literal no log inteiro (antes: `pci: %02x:%02x.%x`). Regressao: validate-network-datapath PASS + phase6-boot-smoke 6/6.
- [x] FLAKE DE FAMILIA (sendkey/rescue-shell) generalizada (15/10): validate-network-surface, validate-abi-compat-surface E validate-audio-stack falham TODOS no mesmo ponto — o harness digita 's','s',Enter para escolher rescue shell e o boot vai para DESKTOP (`desktop: session ready` no log), entao o primeiro marker interativo (`shell: command X`) nunca chega. Atribuicao honesta: `validate-audio-stack` falhou IDENTICO com as mudancas desta rodada STASHADAS (HEAD 9d8ab6412 limpo) — nao e regressao; e timing de HMP sendkey do seletor de rescue no host. Os tres validadores precisam de hardening do seletor (esperar o menu de boot publicar antes de digitar) antes de servirem de gate.
- [x] USB host: `kernel_usb_host_controller_count()` retornava 0 mesmo apos `uhci` inicializar — CORRIGIDO (15/10, ddb1a26ef). Causa raiz: `kernel_usb_host_probe_cb` (que popula `g_kernel_usb_host_state` via `kernel_usb_host_store_controller`) era CODIGO MORTO — nenhum `kernel_pci_enumerate` o invocava, entao `.controllers` ficava 0 mesmo com controlador presente. FIX: `kernel_usb_host_init` chama `kernel_pci_enumerate(kernel_usb_host_probe_cb, &g_kernel_usb_host_state)`. Prova: boot mostra `usb-host: controller=uhci pci=8086:7020 loc=0:1.2 irq=b bar=4 io=c040 ports=2` + `[14] usb: init complete (1 controllers, 0 devices)`; `validate-network-datapath` PASS; `validate-phase6-boot-smoke` 6/6. Registrado em KNOWN_BUGS (item corrigido 11).
- [ ] SESSAO INTERATIVA trava no meio (o 'timing de sendkey' da linha 212 era so a CASCA; sob o seletor closed-loop de 13ae59214 ha um stall REAL): modo (i) `boot mode: rescue shell` mas `shell: ready` nunca aparece (ticks seguem); modo (ii) shell da prompt, consome `netmgrd status` inteiro mas o comando nunca retorna (sem `shell: command exit netmgrd rc=`). MEDIDO com contadores `input-diag` (adicionados em `kernel/drivers/input/{input,keyboard}.c`): producao de input SAUDAVEL no stall (`scancodes`/`produced`/`consumed` sobem juntos, `drops=0`); NENHUM `service: request timeout` (budget 16 intacto) -> descarta IPC com timeout; console host inocentado (echo e `sys_text_write` kernel-direct). Watchdog de infinite-wait no scheduler tentado e REMOVIDO (ruidoso, idle legitimo). Instrumento de contadores fica. Proximo passo: rastrear o wait SEM timeout no lado consumidor apos dequeue (`mk_input_service_read_key`/host de input) e o round-trip MK_SERVICE_NETWORK/transfer do app `netmgrd` (modo ii). Ver item 0.6 no Plano de Fechamento Definitivo.

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

### P4 — adendo 16/10 (mesma rodada do fix da camada de serviços)

- Os `deferred inputsvc/consolesvc/networksvc launch failed` medidos antes NÃO eram rejeição real:
  eram SUCESSOS mal reportados (`mk_service_launch_task` devolvia pid>0 e os callers tratam !=0 como
  falha — KNOWN_BUGS #14). As traces nomeadas (`launch: FAIL`, `service: prepare/restart FAIL`)
  ficaram mudas porque não havia falha. O fix (normalizar sucesso a 0) foi provado no UP (5/5 `launch ok`)
  e moveu o smp2 real-time para diante pela primeira vez: `desktop: session ready` atingido em tempo-real
  (antes só com -icount), com os 5 serviços deferred ok.
- **Parede restante do smp2 (classe #5, agora pós-desktop):** `exception: Page Fault user pid=9 eip=f000d957`
  + `#UD kernel-mode cpu=1 eip=3 cs=8 b0=f0 b1=53 (halting)` logo após session ready — executar VA 3 em
  modo kernel é salto/call através de ponteiro de função NULL+3 no AP. Instrumentos já no tree: census
  expandido (parte 8/9), `sched-head`, STILL-LINKED, WATCHED-FREE (0 hits em todos os runs 16/10),
  `heap-sweep` forense (armado; sem BAD-HEAD neste face). **PROXIMO PROBE (desenhado, não executado):**
  no handler de #UD kernel-mode, dumpar as primeiras ~8 words do stack kernel que casem com o intervalo
  .text (backtrace bruto orcado) — isto nomeia o CALL SITE do salto para 3 sem gdb (gdb suprime a corrida).
- Gate status 16/10: UP `validate-phase6-boot-smoke` 6/6 (re-rodado com o build final); `validate-smp`
  permanece FAIL em smp2/smp4 (exige `userland.app: shell start`; o crash pós-session-ready ainda o
  impede) — vermelho honesto, sem verde por omissão.

### P4 — adendo 2 de 16/10 (backtrace + W^X no texto: classe #5 nomeada e parcialmente fechada)

- **Correção de registro (honestidade):** o `heap-sweep` da rodada anterior tinha o hook INALCANÇÁVEL
  (dentro do bloco `if (prev != NULL ...)` enquanto BAD-HEAD/BAD-NODE-IN-TIMEOUT chamam com
  `prev==NULL`) — o "armado; 0 hits" NÃO era evidência, era bug do instrumento. Hook movido para o
  topo do diag nesta rodada; passou a capturar imediatamente.
- **FACE 1 FECHADA com causa raiz nomeada:** com o sweep funcional, o primeiro boot smp2 real-time
  reportou `heap-sweep: OVERFLOW block=0x390adf20 size=0x1020 allocpc=0x5114b` + DESYNC no vizinho →
  `0x5114b = smp_init+0x2c2` = **`kernel_malloc(AP_STACK_SIZE)` com AP_STACK_SIZE=4096**: a stack do AP
  (que roda `smp_ap_entry`/schedule/IRQs para sempre) exauria e pisava para trás no heap — o bloco
  adjacente corrompido era o do pid=1 (g_head), fechando semanas de "corrupção fantasma" com o mesmo
  remédio já validado da classe #1 (workers 8K→32K): **AP_STACK_SIZE 4096→32768**. Neste mesmo boot o
  backtrace do #UD já havia provado outra face: bytes em execução != ELF (`83 f8 20`→runtime `0f 0f`)
  = .text reescrito durante execução, e essas páginas NÃO estão no pool do physmem (base=0x100000),
  ou seja: escrita por VA aliado/envenenado, não reciclagem de frame.
- **FACE 2 PEGA PELA ARMADILHA:** implementado `paging_protect_kernel_text()` — marca
  `[__kernel_text_start,__kernel_text_end)` supervisor-RO em PTEs 4K (split do huge page, API já
  existia) + **CR0.WP=1** (sem isto ring0 ignora R/W). Símbolos linker `__kernel_text_start/end`
  adicionados ao kernel.ld. UP boota limpo com a proteção (`paging: kernel text write-protected
  [10000,65000) wp=on` → `desktop: session ready`): não existe escritor legítimo de .text. No smp2
  real-time com 32K-AP-stack: ZERO faces de heap/lista (overflow #1 morto), e o escritor foi PEGO em
  flagrante: `Page Fault err=3 (write, kernel) eip=0x47fab cr2=0x394db` com
  **`0x47fab = kernel_text_render_graphics+0xd1`** — o shim gráfico de texto do console escreve fora
  da capacidade do backbuffer (laço `target[py*pitch+px]` sem checagem de capacidade do buffer vs
  `scr_h*pitch`) e o destino vazado cai dentro do próprio .text. **FIX PENDENTE (próxima rodada):**
  bounds reais no renderer (capacidade da backbuffer/fb em vez de scr_w/scr_h apenas; auditar
  pitch×height vs tamanho alocado em kernel/drivers/video/text.c + video.c).
- Instrumentos que ficam: backtrace one-shot do #UD kernel-mode (ud-bt), W^X do kernel text (trap
  permanente anti-reenescrita de código), heap-sweep com hook corrigido, AP stack 32K.
- Gates 16/10 parte 2: `make` -Werror verde; UP `validate-phase6-boot-smoke` 6/6 com wp=on (relatório
  re-gerado); smp2 real-time: sem OVERFLOW/BAD-HEAD/heap-sweep neste build; morre agora NO NOMEADO
  `kernel_text_render_graphics` PF (1 ocorrência) — progresso de diagnosticabilidade, validate-smp
  segue FAIL (honesto).

### P4 — adendo 3 de 16/10 (face-2 FECHADA com bounds; face-1 REABERTA por releitura honesta)

- **Face-2 fechada:** o shim `kernel_text_render_graphics` so conferia geometria do MODO
  (`px<scr_w, py<scr_h`) e nunca a capacidade real do buffer; com o W^X da rodada anterior ele
  foi pego escrevendo dentro do .text (`PF err=3 eip=kernel_text_render_graphics+0xd1
  cr2=0x394db`). Fix: `kernel_video_target_capacity()` (shadow = bytes mallocados; LFB direto =
  janela reservada publicada por `kernel_video_set_fb_window()`) + clamp `idx < cap` nos 3 loops
  + linha de rows que nao cabem na janela. O setter foi movido para ANTES de
  `kernel_video_init()` porque o primeiro boot com o clamp revelou (pelo proprio log honesto
  `text-render: no known capacity ... window=0`) que o shim renderiza durante video-init, bem
  antes de memory-init — sem isso o boot inteiro perderia texto (regra: instrumento que grita
  vale mais que hipotese elegante). PROVA: UP `validate-phase6-boot-smoke` 6/6 com o clamp
  ativo e sem nenhum nocap; smp2 real-time com ZERO Page Fault/ud-bt desde o fix.
- **Face-1 REABERTA (correcao do registro de parte 2):** o canario do heap fica no FIM do
  payload — ACIMA de `stack_top` — entao pilha crescendo para baixo nao pisa o proprio canario;
  o padrao observado (canario do bloco da stack do AP + cabecalho do vizinho de cima) e escrita
  PARA CIMA alem do topo da stack, e com 32K o sweep segue acusando o mesmo bloco
  (`OVERFLOW size=0x8020 allocpc=smp_init+0x40b` no run pos-bounds). "AP_STACK 4096→32768
  fechou a face-1" estava ERRADO como conclusao (o 32K fica, defensivo, mas nao eliminou a
  assinatur). Hipoteses vivas nomeadas no KNOWN_BUGS (classe #5): stack_top obsoleto patcheado
  no trampolim entre retries de INIT/SIPI, accounting off-by-no-free/coalesce, ou ponteiro
  antigo para a regiao. Proximo passo concreto ao disparar OVERFLOW em bloco de stack: dumpar
  os 8 words apos o cabecalho do vizinho + comparar `*(trampolim+STACK_PTR)` vs
  `stack+AP_STACK_SIZE` atual.
- Gates 16/10 parte 3: make -Werror verde; UP phase6 6/6 (build final com clamp); smp2 real-time
  avanca para a face-1 pura (a face-2 nomeada sumiu). validate-smp segue vermelho no smp2/smp4
  (face-1 ainda trava dispatch) — honesto, sem verde por omissao.

### P4 — adendo 5 de 16/10 (witnesses de campo: WATERMARK-FRAME 0 hits; sched-rsp OK; nova face = salto para o BLOB userland)

- Dois witnesses novos entregues e medidos em smp2 real-time:
  1. `sched-resume: WATERMARK-FRAME` (sanitize_resume_frame): frame fora da
     stack da task OU >=3 words == 0xA5A5A5A5. **0 disparos** — a face do
     `GP err=a5f0` nao se reproduziu neste build; o detector fica armado e
     orcado (8).
  2. `sched-rsp` (ESP real do CPU vs stack da task atual, chamado do
     smp_timer_irq_handler): `sched-rsp: OK cpu=1 pid=4 rsp=390d794c
     stack=[390cf9e0,390d79e0)` x2 (baseline) e **0 OUTSIDE** — o ESP do AP
     esta dentro das stacks declaradas das tasks nos momentos amostrados; a
     hipotese "AP operando com esp acima do stack_top declarado" NAO se
     sustenta no ponto amostrado.
- OVER-detail dump (parte 4) classificou a face-1 de um run anterior: as
  words eram uma cadeia de frames com retaddr `kernel_tss_set_kernel_stack`
  escritas ACIMA do topo do bloco de 32K da stack do AP — porem o sched-rsp
  (acima) diz que o esp do AP estava em stacks validas nos ticks. As duas
  leituras se reconciliam se o dano vier de ESCRITA via ponteiro velho/alias
  (nao do push do proprio AP): pr proxima rodada — ligar o OVER-dump a um
  trace de `kernel_tss_set_kernel_stack`/`esp0` (quem chama, com qual valor)
  orcado, ja que a retaddr desta funcao apareceu nas words capturadas.
- RUN ATUAL deu ainda outra face: `#UD eip=0x11394` com bytes `00 00`;
  resolucao: `userland_entry+0x684` — salto para DENTRO do blob de userland
  embutido (regiao .text no layout do linker), nao reescrita de codigo.
  O ud-bt bruto apontou `kernel_e1000_ioctl+0xb4` mas **ninguem chama
  kernel_e1000_ioctl no kernel** (grep: so a definicao) — backtrace bruto e
  heuristico (varre stack; pode capturar residuos), registrar assim e nao
  tratar a cadeia como culpa.
- Gates: make -Werror verde; UP phase6 6/6 (relatorio build/); smp2 segue
  vermelho por classe #5 com faces alternando (OVERFLOW-de-alias, GP-writer,
  salto-ao-blob) — todas agora com witness no tree.

### P4 — adendo 6 de 16/10 (auditoria de ponteiros achou UAF LATENTE de execve; corrigido; face-1 segue aberta)

- A auditoria pedida no adendo 5 (stale pointers pos-terminate/swap) encontrou em `sys_execve` um UAF REAL porem LATENTE: `kernel_free(current->stack)` era feito enquanto o proprio syscall executava sobre essa stack, e o TSS esp0 so era reposto no proximo dispatch. Latente porque nenhum userland compilado chama SYSCALL_EXECVE (grep: unicos 'execve' estao em arvores vendored nao-buildadas de links2/breakpad) — logo NAO explica as assinaturas de escrita-alias da face-1; mas e o MESMO tipo de buraco que as explica, e fica fechado na fonte.
- Fix: `process_t.pending_stack_free` (campo append, interno-kernel, ABI intata; zerado nos memcpy-wholesale de fork/thread); execve ESTACIONA a stack velha + repoe esp0 imediatamente; liberacao de verdade no switch-away ou terminate (executam fora da stack estacionada). Ordem scheduler->heap ja estabelecida. KNOWN_BUGS #15.
- Gates: UP `validate-phase6-boot-smoke` 6/6 com o fix (rc=0, build/); smp2 real-time re-rodado sem regressao (resultado no commit). Face-1 permanece ABERTA com o proximo passo do adendo 5 (trace de kernel_tss_set_kernel_stack + auditoria stale g_current/service->process/launch-record) — agora sem o confounder do execve-UAF no caminho.

### P4 — adendo 7 de 17/10 (face nova: queda para o heap EM EXECUCAO sem tocar o caminho de dispatch)

- Witness `sched-esp0: SUSPECT` (scheduler_sanitize_resume_frame: todo esp0 setado fora do heap
  com pid/stack/size/caller, budget 8) ARMADA; 0 hits na face medida.
- Run real-time smp2 17/10: `#UD kernel-mode cpu=1 eip=0x390e7584` com o proprio ESP da trap no
  heap (`ud-bt: sp=390e7554`), **sem** BAD-FORENSICS, **sem** OVERFLOW/BAD-HEAD, **sem**
  SUSPECT-esp0, **sem** sched-rsp OUTSIDE, e log final visivelmente CORROMPIDO por traces
  entrelacados durante o halt ("hscheduler: timeout pid=15489520 cla...").
- INFERENCIA NOVA (estreita o espaco): a queda para dentro do heap NAO passou por context
  salvo (senao BAD-FORENSICS/SUSPECT pegariam no dispatch) e o esp ja estava no heap no
  momento do #UD — logo a corrupcao atingiu FRAMES VIVOS da pilha em execucao (retorno ou
  ponteiro de funcao dentro de um frame atual), exatamente o que a nota da classe #3 ("the
  trashing lands during execution") vinha dizendo. Testemunhas de tempo-de-dispatch NAO
  enxergam essa janela; a face-1 precisa de witness INTRA-EXECUCAO.
- Proximas candidatas (uma por rodada, para nao virar ruido): (1) guarda de water mark no
  TOPO da stack da task atual checado no tick da propria task (se as frames vivas ja
  estao no territorio 0xA5, o esp afundou — pega o esgotamento em flagrante no
  proximo tick); (2) canary de borda de 16B ACIMA do esp0 (topo declarado) verificado a
  cada sched-scan — um estouro para cima (iret/pop acima do topo) fica nomeado; (3) os
  traces entrelacados do halt sugerem serial-lock no caminho de panic — auditar o
  dump de panic para nao perder evidencia (baixa prioridade).
- Estado UP: gate phase6 em execucao para este build (resultado no commit). Faces
  documentadas da parede classe #5 em smp2: (a) alias-writer no bloco da stack do AP
  (OVER-dump com cadeia tss_set), (b) GP err=a5f0 watermark-frame, (c) salto ao blob
  (eip=userland_entry+0x684, 00 no ELF), (d) queda ao heap em execucao (este adendo),
  (e) face-1 classica BAD-HEAD com bloco vivo. Todas com ao menos uma testemunha armada.

### P4 — adendo 8 de 17/10 (1ª witness intra-execução armada; W^X entrega o PRIMEIRO escritor em flagrante)

- `tick-guard: STACK-CANARY-BAD`: a cada tick, o canario-cauda do bloco da stack da task
  em execucao em CADA cpu (g_current[] so e mutado sob g_scheduler_lock; leitura
  cross-cpu aqui e segura; heap lock sob scheduler lock = ordem estabelecida). Pega
  estouro PARA CIMA em flagrante, com o dono ainda corrente. Armada; 0 hits na face do run.
- Run real-time smp2 (mesmo build): apos `#UD cpu=1 eip=3 (b0=f0 b1=53)`, o W^X de .text
  produziu o PRIMEIRO escritor pego escrevendo: `Page Fault err=3 eip=0x1ba53 cr2=0x1d081`
  (write, kernel, protecao em pagina RO). Resolucao por nm: eip=`mk_network_virtio_poll_queues`
  (comeco), cr2=DENTRO de `mk_network_progress_state+0xa5b` — AMBOS em .text.
- CONTRADICAO APARENTE RESOLVIDA = EVIDENCIA-CHAVE: a primeira instrucao do escritor no ELF e
  `cmpl $0x0,[0x18f7ac]` (LEITURA; nao explicaria err=3 nem cr2=0x1d081). Conclusao: os BYTES
  em execucao em 0x1ba53 DIFEREM do ELF — o "escritor" e na verdade um ponto de .text JA
  corrompido com `f0 53` (mesmo par impresso no `#UD eip=3 b0=f0 b1=53` desta e de faces
  anteriores), decodificado como `lock ...` -> RMW que o W^X barra. PAR `f0 53` plantado em
  multiplos pontos de .text e a assinatura UNIFICADORA das faces (a)-(d)+(este): algo grava
  fragmentos de 2 bytes em enderecos de codigo BAIXOS (0x1ba52, 0x1d081, 0x11394, 0x390e7584-
  familia heap). PROXIMO PASSO CONCRETO (1 por rodada): no handler de PF por violacao de .text,
  dumpar 16 bytes AO REDOR do cr2 E do eip (com budget), para ver se os vizinhos dos pontos
  corrompidos mostram o padrao f0-53 / outros fragmentos — o padrao espacial nomeia o mecanismo
  (linear-overflow vs disperso = tabela de ponteiros/objeto sendo escrito com base errada).
- Faces consolidadas continuam (a)-(e) no adendo 7; este run = variante de (a/b/d) com o
  writer agora NOMEADO pelo proprio W^X (primeira vez que um run entrega eip+cr2 de escrita
  ilegal em codigo sem nenhum watchdog de dispatch precisar disparar).
- UP gate: rc=0 phase6 6/6 com as duas testemunhas novas (tick-guard + W^X ja doado) no build.

### P4 — adendo 9 de 17/10 (dissecção espacial armada; face revelada contem ASCII DECIMAL — o escritor copia TEXTO por ponteiro velho)

- Witness `pf-text:` (page_fault_handler em idt.c): ao violar .text, dumpa 11 words ao redor
  do cr2 E do eip (budget 8). Armada; 0 hits nesta face (o run morreu pela via #UD/heap, nao
  por PF de .text).
- LEITURA NOVA do OVER/DESYNC-dump deste run: os dados plantados sao ASCII DECIMAL —
  `allocpc=0x33373130` = "0173", `sp=0x333931c8` = "1903", `prehdr` com ponteiros internos
  0x390b5fxx — i.e. o escritor esta COPYANDO TEXTO DE DEBUG (digitos de %d/%u) para dentro da
  cauda do bloco da stack do AP, atraves de um ponteiro velho. O buffer[16] do serial printf foi
  auditado (hex<=8, dec<=10 — nao e estouro do proprio printf). Suspeitos naturais: qualquer
  caminho que grave logs/eventos em buffers apontados por `process_t*`/`launch record`/
  `service->process` STALE apos terminate (o terminate NAO libera o bloco, mas um
  launch-record ou fila com copia-texto ainda pode referi-lo e escrever via VA velho quando o
  HEAP real do bloco for reinvestido — EXATAMENTE o mecanismo que produz ASCII no vizinho).
- PROXIMO PASSO CONCRETO (1 por rodada): auditoria dos consumidores que COPIAM para
  buffers derivados de process_t* de terceiros (task-event stream/eventos de servico com
  texto), checando `state==TERMINATED` antes de escrever; e verificar se `mk_launch_release_pid`
  deixa record->process apontando para task morta enquanto o pid-slot ja foi reinvestido.
- Faces: este run = (a) alias-writer com conteudo agora identificado como TEXTO; probe v2
  (smp_probe_watch) continua apos session-ready e para no exception — metodologia melhorada.

### P4 — adendo 10 de 17/10 (auditoria dos dois suspeitos do adendo 9: sem escritor confirmado; backdoor pid-0 do transfer FECHADO)

- AUDITORIA NEGATIVA (honesto): (1) handlers FS adquirem/escrevem/soltam na ordem certa
  (data_write→read(fd,buffer,size)→release, tudo dentro do caso switch; nenhum uso da
  pointer depois do release); scan por janela apos cada `mk_transfer_release` nos 22 sitios:
  0 flags reais (2 falsos-positivos = atribuicao de parametro de saida). (2) o retry-worker
  roda como TASK com pid proprio (mk_service_retry_worker_entry) — nao e contexto pid-0.
  (3) o task-event stream escreve em arrays ESTATICOS (subscription->events / stream->events
  = storage embutida no registro estatico), nao em heap — descarta o vetor "copia de texto
  para buffer heap de outro dono" por essa via.
- UNICO DEFECTO CONCRETO da auditoria e FECHADO: `mk_transfer_has_access` dava acesso TOTAL a
  qualquer transfer quando o contexto nao tinha task (pid==0: idle/AP-hlt/pretarefas). Agora
  pid-0 so alcanga transfers de owner_pid==0 (boot, mesmo dominio). Este era o UNICO
  caminho code-review-encontrado pelo qual codigo sem-task poderia escrever no buffer de uma
  task viva — compativel com o conteudo textual (caminhos com digitos) plantado na face-(a).
  NAO se afirma que era o escritor das faces (a)-(e): as faces continuam ocorrendo com a
  regra fechada (run 17/10d morreu cedo na familia eip=3/cs=1, sp em .bss, retaddrs
  spinlock_unlock). UP nao regressa (todos os fluxos legitimos carregam pid real).
- Gates 17/10d: make -Werror; UP phase6 6/6 com a regra nova (rc=0, build/). Face-1 segue
  aberta; proxima pista candidata: o novo sp=0x1fc2e8 (.bss) + retaddrs spinlock sugere
  frame corrompido LENDO de .bss — investigar writes em dados do scheduler durante a janela
  storage-load com a witness tick-guard ja armada (0 hits ate aqui = escrita nao e estouro de
  stack de task viva).

### P4 — adendo 11 de 17/10 (tick-guard dispara em VALORES LEGITIMOS = janela de publicacao de esp0; hipotese .bss-clobber REFUTADA; nova face: tudo dentro de UM bloco de stack)

- A witness tripla do tick-guard (STACK-FIELDS-BAD / STACK-CANARY-BAD / ESP0-MISMATCH, com
  `kernel_tss_get_kernel_stack()`) disparou 7x em smp2 real-time — mas TODOS os valores eram
  topos de stack LEGITIMOS de outras tasks (pid=5 corria, esp0=topo-do-pid=12; depois o
  inverso). Diagnosticado na fonte: `scheduler_sanitize_resume_frame` escrevia o esp0 DEPOIS
  do unlock, enquanto `g_current[cpu]=next` e publicado sob o lock -> a guard de outro cpu
  amostra a janela real (nao-corruucao). FIX: esp0 publicado junto com g_current, sob o mesmo
  lock (`kernel/process/scheduler.c`, bloco de dispatch; sanitize mantem suas checks).
- Re-run smp2 com o fix: **tick-guard 0 hits** e UP phase6-boot-smoke verde (rc=0).
  Conclusao honesta da rodada: REFUTADO (ao nivel de amostragem de tick) o clobber de
  stack/stack_size/esp0/canary por lixo tipo .bss — FIELDS/CANARY/SUSPECT/WATERMARK/DESYNC/
  DOUBLE-DISPATCH/TR-MISMATCH todos 0 hits. O estado do task e consistente; o lixo mora em
  OUTRO lugar.
- NOVA FACE (f) da classe #5, medida no run pos-fix: storm de crash APES de `storage request
  type=16 from pid=9 to pid=1` alternando 9<->1: `user-frame mismatch` (cs de frame =
  endereco heap), `spinlock: stuck lock=0x390e76ec`, `#UD cpu=0 eip=7 cs=0x10082 b0=f0 b1=c3`,
  `#UD cpu=1 eip=f cs=0x30303030` (ASCII "0000"!), `GP err=e93c eip=0x390e7508 cs=8`,
  `ud-bt sp=0x390e75b4`. CROSS-CONSISTENTE: 0x390e7a5c / 0x390e7508 / 0x390e76ec / 0x390e75b4
  todos dentro de UM bloco de stack de task [0x390d8080,0x390e8080) (64KB, do pid=5) —
  multiplos "donos" apontando para o mesmo bloco. HIPOTESE-LIDER AGORA: ALIASING de bloco de
  stack (alloc devolve um bloco que outra task viva ainda usa / free com referencias), o que
  explica as witnesses de campo passarem: o bloco e legitimo, a sobreposicao e interna.
- HIGIENE DE LEITURA: no storm de crash a serial esta INTERLEAVED (linhas coladas — e.g.
  "kud-bt" = fim de "stuck" + "ud-bt"; "expected=216" != USER_CS_SELECTOR 0x1B; "cpu=226934"
  impossivel). So valores repetidos cross-linha sao evidencia; campos unicos de linha de crash
  devem ser tratados como possivel mistura de duas printfs.
- Proxima sonda (round seguinte): witness de alias no alocador — em `kernel_malloc` apos
  sucesso, verificar sobreposicao [p,p+n) contra ranges [stack,stack+size) de tasks vivas
  (lista g_head + g_current[]); disparar `heap-alias: new block overlaps live task stack
  pid=...`. Se aliasar, nomeia o par de tasks no momento exato da dupla posse.

### P4 — adendo 12 de 17/10 (witness heap-alias armada: LADO-MALLOC da face (f) REFUTADO; smp2 deste build chegou a session-ready com 1 excecao)

- ARMADA: `scheduler_stack_overlap()` (scheduler.c, walk capped sem lock) chamada de
  `kernel_malloc` apos sucesso — qualquer bloco entregue sobrepondo [stack,stack+size) de
  task viva (nao-TERMINATED) nomeia o par (`heap-alias: alloc=[..) overlaps live pid=..
  allocpc=..`, budget 6). Cobre os 5 sitios de stack (process_create/fork/thread/elf-load/AP)
  e QUALQUER alocacao (buffer de app sobre stack viva tambem).
- MEDICAO: make -Werror verde; UP phase6-boot-smoke rc=0 (witness ativa no boot inteiro,
  0 hits = sem falso-positivo); smp2 real-time: **0 hits de heap-alias** -> o lado-malloc do
  aliasing (allocator devolvendo bloco de task viva) esta REFUTADO nesta execucao.
- NOTACAO DO RUN (nao claimar como fix): este build passou de `desktop: session ready` com
  UMA unica excecao (`#UD cpu=1 eip=3 b0=f0 b1=53` por volta da janela storage type=16; o
  restante do boot seguiu, com `sched-dispatch: BAD-FORENSICS` no fim — residuo 0.8 ja
  catalogado). Run anterior pos-fix tinha tido storm multi-exception ANTES do session-ready.
  TIMING-SENSITIVE: a witness anda a lista de tasks em cada malloc e perturba o timing — a
  diferenca nao e atribuida a correcao de bug, e sim registrada como variacao de face.
- Proximo passo da face (f) (lado-FREE): witness simetrica em `kernel_free` — se o range
  liberado bate com [stack,..) de task VIVA, nomeia `heap-free-live-stack pid=.. freepc=..`.
  Se o alias entra pelo free (stack liberada com dono vivo; o proprio dono continua usando o
  bloco, sem nunca haver um novo alloc), o malloc-witness NAO dispara — e este e o gap que a
  sonda seguinte fecha.

### P4 — adendo 13 de 17/10 (witness FREE-side armada: 0 hits; mas o tick-guard pegou a CADEIA do crash em flagrante — record pid=0 com campos impossiveis e frame emprestado do pid=1)

- FREE-SIDE ARMADA: `heap-free-live-stack` em `kernel_free` antes do coalesce (mesmo
  helper `scheduler_stack_overlap`, budget 6) — pega stack liberada com dono vivo SEM
  nunca haver re-alloc. UP session-ready: 0 hits (sem falso-positivo no boot inteiro);
  smp2: 0 hits tambem. Logo o mecanismo da face (f) NAO passa pelo free de stack viva.
- FLAGRANTE (run 17/10g, smp2 real-time, tick ~1116, cpu=1): cadeia lida em ordem —
  (1) `sched-dispatch: BAD-FORENSICS cpu=1 pid=1 ... ctx=0x390be120 stack=0x390b6600
  size=32768 frame{ebp=0x2babc esp=0x1ec064(.bss, vizinho de g_cpu_tss@0x1ec0b0)
  eip=0x6 cs=0x390be164(heap DENTRO da stack do proprio pid=1)}`; (2) `timeout pid=1
  event=154988`; (3) switch `old=5/1 next=0/2` — o record do "next" pid=0 traz
  `stack=0x390b6fa4` (DESALINHADO: %8=4, payload de kernel_malloc e 8B-alinhado SEMPRE;
  e mora dentro de [0x390b6600,+32K) = stack do pid=1!) e `size=166074=0x2890A` (nenhum
  kernel usa tamanho de stack nao-potencia; valores legitimos vistos: 16384/32768/65536/
  262144); (4) `tick-guard: STACK-CANARY-BAD cpu=1 pid=0` — o primeiro hit real do
  STACK-CANARY-BAD e um RELATO CORRETO sobre record-lixo, nao artefato; (5) `sanitize
  cpu=1 old=5 next=0 frame=0x390be120 eip=6` — o "next" pid=0 usa o MESMO contexto do
  pid=1 (0x390be120) — e iret->GP err=a eip=8->PANIC.
- LEITURA: um record de task (process_t) esta sendo SOBRESCRITO com endereco vivo de
  objeto do kernel (.bss/TSS-adjacente, enderecos internos da stack de outro pid), OU
  g_current/find_next esta lendo um process_t JA LIBERADO/reinvestido — os dois
  formatos implicam "memoria do RECORD virou outra coisa", nao "ponteiro da task
  corrompido". FIELDS-BAD nao disparou porque stack/size passam nas checagens simples
  (dentro do heap window, 1024..4MB) — a testemunha decisiva e o header do bloco do
  proprio record.
- PROXIMO PASSO (1/rodada): no momento CANARY-BAD/FIELDS-BAD, dumpar o HEADER heap do
  bloco do process_t (16B antes do record: {size,magic,next,reserved=allocpc}) e
  traduzir allocpc via nm — NOMEA qual alocacao e duena da memoria onde o record mora.
  Se allocpc != kernel dos sitios de process_t, o bloco foi reinvestido = UAF do record.

### P4 — adendo 14 de 17/10 (flagrante do adendo 13 em build com sweep-on-hit NAO replicou a face; nova decodificacao: eip NO MEIO de instrucao com bytes == ELF => retorno corrompido no caminho audio, nao escrita em text)

- BUILD NOVO (witnesses da rodada anteriores + `kernel_heap_forensic_sweep(t)` nos ramos
  FIELDS-BAD/CANARY-BAD + dump `victim-hdr{size,magic,allocpc}` do bloco do proprio record):
  UP smoke rc=0. smp2 real-time: heap-alias 0 hits, heap-free-live-stack 0 hits e NENHUM
  hit de CANARY/FIELDS nesta execucao — a face ROTACIONOU (ver abaixo), entao o resultado
  da sweep-on-hit fica PENDENTE (nao e evidencia de "record integro", e ausencia de gatilho).
- FLAGRANTE DA RUN ANTERIOR (17/10g, ja registrado no adendo 13) permanece como a melhor
  captura: record pid=0 com stack/size imposiveis + iret com eip=6 no contexto emprestado
  do pid=1 -> GP -> panic.
- NOVA DECODIFICACAO (run 17/10h, smp2): `#UD cpu=0 eip=0x25db4 cs=8 b0=ff b1=ff` — nm:
  `0x25db4 = mk_audio_refresh_topology_snapshot+0x3b`; objdump: em 0x25db1 ha
  `e8 67 d4 ff ff call 2321d <mk_audio_backend_current_is_usable>` — ou seja, b0/b1 sao os
  bytes REAIS do ELF no MEIO da instrucao: eip PAROU 2 bytes ANTES do alvo de retorno
  legitimo (0x25db6 = push da call). Isto e um salto/retorno DESALINHADO (return address
  corrompida em byte baixo, ou eip carregado de contexto lixo), NAO texto escrito —
  coerente com W^X limpo e pf-text 0 hits. O `eip=3 b0=f0 53` irmao continua sem decode
  (abaixo de .text: bytes da regiao IVT do BIOS — pode ser conteudo REAL de RAM la, o que
  tambem aponta para "eip veio de dados", nao "texto plantado").
- ud-bt do mesmo evento: cadeia `spinlock_unlock_irqrestore+0x11 / spinlock_lock_irqsave+0xa
  / scheduler_schedule_frame+0xb1 / kernel_heap_is_block+0x7b` repetida — a stack era um
  caminho scheduler/spinlock/heap legitimo no momento do #UD.
- SINTese honesta da face-1 agora: (i) records de task aparecem com campos que sao
  enderecos vivos de OUTROS objetos (adendo 13); (ii) eips de crash caem no MEIO de
  instrucoes validas a pequenas distancias do alvo correto (este adendo). Ambas as
  assinaturas sao de PONTeiros/RETORNOS DESLOCAADOS, nao de lixo aleatorio nem de texto
  plantado. Proxima sonda (1/rodada): no `sanitize_resume_frame`, quando o frame e de
  KERNEL-mode, checar `eip` contra os limites do ELF de cada func? — mais barato e direto:
  witness no RET de IRQ (`idt` epilogos): comparar o eip restaurado com a tabela de
  chamadas? Ja existe WATERMARK-FRAME; o gap real e o ESPACO entre dispatch e iret.
  Candidato mais forte: a stack do `scheduler_schedule_frame` do AP recebe o FRAME via
  IRQ-trampoline do smp.c — auditar o eplogo de `smp_timer_irq_handler` (push/pop do
  frame) contra reentrancia (IRQ de outro dispositivo no meio do eplogo no AP).

### P4 — adendo 15 de 17/10 (sweep-on-hit PAGOU: record da parede é o process_t do INIT, header legitimo, payload envenenado; vizinhos fisicos nomeados por allocpc; CANARY-BAD desambigado em modos)

- INSTRUMENTO NOVO: `kernel_heap_block_validate(ptr, hdr[4])` (heap.c) separa os 3 modos que
  `kernel_heap_block_canary_ok` colapsava em "0": rc -2 ponteiro nao-e-bloco (desalinhado/
  fora-heap), -3/-4 cabecalho do bloco esmagado (exaustao para baixo atinge o PROPRIO header),
  -5 canary de cauda (estouro para cima). tick-guard agora reporta
  `STACK-PTR-NOTBLOCK / STACK-HDR-SMASHED / STACK-CANARY-BAD + words do header` +
  `BOTTOM-TOUCHED` (8 words de marca 0xA5 na BASE consumidos = stack encostou no fundo).
- FLAGRANTE ATRIBUIDO (run hd, smp2, tick ~275, cpu=1): `LIST-CYCLE-IN-FIND node=0x390b5f80
  hdr{size=0x680 magic=ALLOC allocpc=0x3072f} pid=1 state=2 next=0xa prev=self` — o node
  envenenado e o PROPRIO process_t do INIT: bloco ALOCADO LEGITIMAMENTE
  (allocpc=process_create_with_stack+0x11f) mas PAYLOAD pisada: `next` = 0xA (inteiro
  pequeno onde deveria ser ponteiro) e a lista entra em ciclo (prev==node). O record NAO
  foi reinvestido (UAF de record REFUTADA neste caso); o veneno entra por ESCrita no payload.
- VIZINHOS FISICOS (chain): logo ABAIXO do record, bloco 0x30 de `ramfs_read+0x119`
  (cnry=1 no momento) cujo payload contem FRAGMENTOS ASCII de hex ("090b" "129d" "6f20")
  + ponteiro cru 0x390b5fd8 para DENTRO do record; abaixo deste, o bloco 0x8020 (=AP_STACK
  32K+32) alocado por `smp_init+0x42c` com CANARY DE CAUDA ESMAGADA contendo {1,
  0x390b5fb5, 0x390b5fa4, 1} — enderecos internos do payload do record do init. Zona de
  escrita contigua: [canary-do-bloco-AP-stack, payload-block-ramfs, payload-do-record].
  Em leitura posterior o MESMO header aparece com valores DIFERENTES (face "live-changing"
  confirmada).
- HIPOTESE AGORA (medida, nao mais rotacao): o escritor enche a janela acima do topo da
  stack do AP e o record do init com dados que contem TEXTO-de-ENDERECO (pedacos ASCII
  "090b"/"129d"/"6f20" = digitos de um %x impresso) e ponteiros apontando para dentro do
  record — assinatura de caminho fs/printf gravando em buffer cujo BASE foi calculado a
  partir do ponteiro do record/stack errado (off-by-um-ponteiro), com o `ramfs_read+0x119`
  como vizinho imediato. Proxima sonda (1/rodada): auditoria de bounds do buffer de 0x30
  alocado no caminho de leitura do ramfs (linha 217 copia `toread` no buf do CHAMADOR, sem
  bounds do lado do chamador; conferir quem chama com buffers pequenos — storage load de
  pid=1 com `fs: sys_storage_load begin` no log imediatamente antes do hit) + dump
  dirigido das 8 words em [record-64, record+64) no momento STACK-CANARY/PTR.
- GATES: make -Werror verde; UP phase6-boot-smoke rc=0 (witness nova ativa, 0 falso-
  positivo: BOTTOM-TOUCHED/PTR/HDR todos quietos no UP ate session-ready); smp2: parede
  inalterada (vermelho honesto), mas pela primeira vez a parede foi NOMEADA em termos de
  objeto-alvo (record do init) em vez de rotacao de eip.

### P4 — adendo 15a de 17/10 (CORRECAO do adendo 15: resolucao feita com nm STALE; com a tabela do build atual, o flagrante conta outra historia — e melhor)

- ERRO MEU REGISTRADO: as resolucoes "ramfs_read+0x119 / font_row_bits / kernel_debug_printf
  como allocpc" usadas no adendo 15 vieram de um /tmp/ksyms.txt de build ANTERIOR. Com
  `nm -n` do MESMO ELF do teste, as mesma addresses dizem:
  - 0x3072f = process_create_with_stack (record do init: confirmadissimo alocacao legitima)
  - 0x348b1 = **ramfs_create+0x69** → o bloco de 0x30 abaixo do record e o `n->data` de um
    arquivo ramfs criado com ~16 bytes (`n->data = kernel_malloc(size)`, call-site 0x348ab)
  - 0x51bd6 = smp_init+0x229 → bloco 0x8020 = AP_STACK_SIZE (32768+32): a stack do AP ✓
  - 0x480ad = **serial_emit_field+0xb7** e 0x4850e = **kernel_debug_printf+0x2b2** → os
    "headers" lidos como lixo na regiao contem ENDERECOS DE RETORNO do proprio caminho de
    printf/serial
  - 0x25e6c = mk_audio_refresh_topology_snapshot+0xf3
- LEITURA CORRIGIDA (o que de fato se encaixa): os bytes plantados em [canary-da-stack-do-AP
  → n->data-do-ramfs → payload-do-record-do-init] sao uma CADEIA DE FRAMES DE EXECUCAO
  REAL (retaddrs de serial_emit_field/kernel_debug_printf + ponteiros para buffers ramfs e
  para o record). Isso e exatamente o padrao de "uma task passou a executar com ESP
  apontando para essa janela": os pushes para baixo da execucao escrevem frames de
  printf/ramfs nessa regiao, e as testemunhas (BAD-HEAD/LIST-CYCLE/STACK-*) leem frames
  como se fossem headers/nodes. Nao ha "escritor misterioso de texto": o escritor e a
  propria execucao, na stack errada.
- HIPOTESE-ALVO AGORA (unica que explica faces (a)-(f) juntas): em algum ponto do caminho
  storage-load do init no AP, `task->context` (ESP salvo) ou o `stack` da task passam a
  apontar para [fim-da-stack-do-AP .. record-do-init], e a task CONTINUA executando ali.
  O `next=0xa` do record = campo de frame de printf escrito sobre o record, nao um valor
  plantado. Proxima sonda (1/rodada): na hora do BAD-HEAD/LIST-CYCLE, comparar
  `node->context`/`node->stack` com a janela do record e checar em process_terminate/
  mk_service_restart/reinvestimento de slot se alguma referencia antiga de pid=1 (record
  ou context) sobrevive — auditando o ciclo de vida do process_t do INIT (servico storage
  e reiniciavel: `restarts=` no log).

### P4 — adendo 15b de 17/10 (auditoria estatica da "escrita acima do topo": candidatos limpos; sonda runtime e o proximo passo)

- Auditados os caminhos que PODERIAM escrever em [stack_top, +N): (1) `process_setup_initial_context`
  (process.c:269-270) calcula `top - sizeof(*frame)` — correto; (2) `fork` (syscall.c:2349):
  malloc e memcpy AMBOS com `current->stack_size` — tamanhos batem, sem overrun; (3) `thread`
  (syscall.c:3739): nao copia stack (context novo no topo) — limpo; (4) tramp AP: stackptr
  readback match=1 medido em execucao anterior; (5) nao ha indexacao `top[+k]` em nenhum
  call-site (`grep stack+size` mostra apenas os dois usos ja checados).
- O proximo passo e RUNTIME e ja tem forma definida: testemunha em `sanitize_resume_frame`
  para frame >= stack_top (a atual WATERMARK-FRAME so olha wm/fora-do-range; falta o modo
  "frame COMECA no topo exato", que e exatamente a janela medida 0x390b5f30=canary do bloco
  do AP) + idem para o eplogo de IRQ no AP quando g_current[cpu]==NULL (idle hlt: ESP do
  handler pode estar no topo do bloco do AP e qualquer push "para cima" por bug de frame
  cairia la). Se o RESUME-FRAME-AT-TOP disparar, a face-1 fecha no emissor.

### P4 — adendo 16 de 17/10 (anel de instalacoes de esp0 armado + prova de que o AP BOOTA na stack dele; face rotacionou para o wake-up PERDIDO do storage — sem crash desta vez, hang aos 360s)

- ARMADO: (1) ring de 12 `{cpu, esp0, caller}` no UNICO escritor de esp0
  (`kernel_tss_set_kernel_stack`, cpu.c), dumpado automaticamente no panic e no halt do
  #UD (`tss-ring:` linhas); (2) bounds da stack do AP por cpu em `smp.c`
  (`smp_ap_stack_bounds`); (3) log one-shot do frame REAL do AP no primeiro C
  (`smp: ap entry cpu=1 frame=390b5f2c stack=[390adf30,390b5f30) in=1`) — **PROVADO: o
  trampoline arma o AP dentro do proprio bloco** (elimina "AP bootou em stack reciclada");
  (4) `IDLE-OUTSIDE` no sched-rsp: ESP do AP SEM task comparado com os bounds proprios.
- RUN (smp2 real-time, build novo): NAO CRASHOU. TIMEOUT 360s com HANG: `init: storage
  smoke begin` aparece, `fs: sys_storage_load returned` NAO; census terminal mostra
  pid=1 `state=2(BLOCKED) ev=2 owner=2` (canal do storage nunca sinalizado) e
  `sel5=249369` de `calls=289741` (~86% das selecoes) — pid=5 consome o scheduler.
  Nenhuma excecao, nenhuma testemunha nova disparou (ring/IDLE so fariam em crash/idle).
- LEITURA: a parede do smp2 tem DUAS faces que alternam com jitter: (A) corrupcao/crash
  na janela storage-load (adendos 11-15, vitima = record do init, janela = acima do topo
  da stack do AP); (B) ESTA: wake-up perdido do storage (item ORIGINAL do P4 reopen) —
  init bloqueia no canal e o completion nao chega. (B) e o mesmo padrao do bug de wakeup
  ja fechado? no: o fechado era RACE de wakeup no worker do AP; aqui o pedido NUNCA
  completa (nenhum `returned`), com o cpu=0 monopolizado por pid=5. Prioridade: nomear
  pid=5 e o seu loop (launch order: pid5 = primeiro servico deferred da lista
  input/console/network — confirmar via traco `first dispatch`), entao decidir se o
  monopolio he' starvation real (sem yield no loop do pid5) ou apenas timeslice-heavy.
- GATES: make -Werror verde; UP validate-phase6-boot-smoke rc=0; `in=1` no UP? (UP nao
  tem AP: o log do UP smoke nao contem a linha — coerente). smp2: sem crash => sem
  evidencia de fix; parede PERSISTE (agora como hang). Proxima rodada: face (B) —
  instrumentar o pedido storage pendente de pid=1 (fila do host + wakeup do worker) e o
  loop de pid=5.

### P4 — adendo 16a de 17/10 (anel PAGOU: o AP CONGELA as instalacoes de esp0 aos 195 enquanto o BSP segue; idle do AP é legitimo na propria stack; PF err=3 no endereco historico 1ba53 revivido)

- WITNESS IDLE v2 (corrigida no mesmo dia): v1 comparava com janela ±4096 e dava
  falso-atributo ("on live pid=1" para rsp DENTRO da stack de boot do proprio AP).
  v2 = precedencia IDLE-BOOT (stack propria) > IDLE-TASK (dentro de stack viva;
  com STATE do dono — idle sobre stack de task BLOCKED é colisao) > IDLE-ORPHAN.
  Medido: `IDLE-BOOT cpu=1 rsp=390b5e64 boot=[390adf30,390b5f30)` — o AP ocioso
  esta NA STACK DELE, correto. Duas hipoteses mortas: trampoline errado (in=1) e
  idle-ESP-orfao.
- ANEL DE INSTALACOES (medido em crash real, run idle3, morte aos ~8s):
  `tss-ring: ud installs=371 totals{cpu0=176 cpu1=195} live_esp0{cpu0=390e8080
  cpu1=391426e0}` → nos dumps seguintes `cpu1` PARA DE SUBIR (195→195→195)
  enquanto `cpu0` continua (211→397): **o AP nao executa mais NENHUMA
  installacao de esp0 desde ~195 — ele para de despachar MUITO ANTES de morrer**,
  mas continua rodando codigo (os #UD sao cpu=1!). live_esp0 cpu1 = 0x391426e0 =
  topo legitimo da stack do pid=9 (256K) — no momento da morte o esp0 do AP NAO
  esta apontando para a janela envenenada; a janela fica sem explicacao pelo
  lado do TSS. Fica um padrao novo e duro: "AP roda sem dispatchar" = ou esta
  preso num loop sem preemption (timeslice nunca estoura? g_timeslice restante),
  ou o tick dele (LAPIC) parou de chegar (handler nao roda => schedule_frame
  nunca) — o congelamento de installs e a assinatura do AP sem tick.
- REVIVIDO: `exception: Page Fault err=3 eip=1ba53 cs=8 extra=1d081` (face (b)
  historica) no MESMO run — o par f0 53 agora lido como `LOCK PUSH EBX` (f0=LOCK
  prefixo invalido em push => #UD exatamente com b0=f0 b1=53 quando a execucao
  salta para endereco cujo byte e f0). Coerente com salto por eip-lixo para
  regioes que contem f0 53 (IVT em 0x3: conteudo do vetor, nao plantacao).
- NOVO ALVO (1/rodada): instrumentar a CHEGADA DO TICK no AP: contadores
  `ap tick count` (smp_timer_irq_handler++ por cpu) e dump junto do anel no
  crash + witness no idle-hlt do AP ("AP-HLT-SINCE %u ticks" budgeted). Se o
  tick parou: causa provavel = LAPIC timer do AP desarmado/ISR sem EOI em
  algum caminho (vector 0x53? ou o spurious). Se o tick continua e installs
  param: o schedule_frame do AP esta retornando cedo (guardar motivo).
- GATES: make -Werror verde; UP phase6-boot-smoke rc=0 (witness v2 + anel
  ativos, sem ruido no UP); smp2: crash persiste (vermelho honesto), anel
  disparou 3x + 1 panic com dados acima.

### P4 — adendo 17 de 17/10 (CAUSA-RAIZ DA ESPIRAL FECHADA: idle-loop aninhado do schedule_frame; fix em arvore; parede da classe #5 PERSISTE com nova assinatura)

- MEDICAO (testemunha IDLE v2): `IDLE-ORPHAN cpu=1 rsp=390b65f0/6550/64b0/6410` — ESP do AP
  descendo EXATAMENTE 160B por tick, DENTRO do bloco do record do init. Cadeia causal
  agora fechada na fonte: no caminho sem-tasks, `scheduler_schedule_frame` entra em
  `for(;;){sti;hlt;find_next}` SEM retornar; o tick LAPIC chama `smp_timer_irq_handler` →
  `scheduler_schedule_frame` de novo → `find_next` NULL → NOVO loop aninhado → +160B de
  trap-frame por tick, nunca unwind. Frames de execucao (printf/serial/ramfs retaddrs —
  os "fragmentos" das faces a-e) corroem de cima para baixo: stack da ultima task → record
  do init (next/state/context → LIST-CYCLE/BAD-HEAD) → bloco ramfs → canary do bloco do AP.
  Um mecanismo explica: "AP roda sem despachar" (installs congelavam: so o loop mais
  interior roda), ESP0-MISMATCH "legitimo", WATERMARK-FRAME, PF-text plantado, eip=3/7
  (iret lendo dados de frame corrompidos como eip), e o "cs segurando endereco de stack".
- FIX (causa-raiz, nao sintoma): bit `g_sched_idle_mask` por cpu, setado sob
  g_scheduler_lock ao entrar no loop idle e limpo ao break; `schedule_frame` rejeita
  re-entrada do topo (devolve o frame do proprio tick; hlt da iret de volta ao loop, que
  JA re-polla find_next — nenhuma wakeup perdida por construcao). Trace `reentry-suppressed`
  budgeted (testemunha nunca silenciosa). ABI: nenhuma mudanca; userland: intocado.
- PROVA DO FIX (2 runs smp2 real-time): `reentry-suppressed cpu=1/cpu=0 rsp=390be0e0`
  ESTAVEL (sem descida), ZERO IDLE-ORPHAN (antes: 4+ por run), installs AP SANS CONGELAR:
  `totals{cpu0=584 cpu1=247}` / `{cpu0=318 cpu1=511}` ticks cpu1=285; UP phase6-boot-smoke
  rc=0; build -Werror verde.
- ESTADO DA PAREDE (honesto): ABERTA, mais funda. Runs pos-fix morrem em ~429-434 linhas
  (antes: 408-526 variavel) com NOVA face: `GP err=1000 eip=390e7636 cs=8` (eip dentro da
  stack do pid5) e `#UD cpu=1 eip=2009c cs=0 b0=c6 b1=5b` (eip em .text com CS=0). A
  espiral era MAIOR fonte de corrupcao mas nao a unica — proxima sonda: o que pula para
  dentro de stack do pid5/para .text com CS=0 no caminho storage (mesma janela type=16);
  candidatos: saved-context de task morta re-dispatchado (auditar g_cursor/GHOST com o
  novo equilibrio), ou iret para frame em meio-de-dados (WATERMARK-FRAME agora com
  concorrencia reduzida pode finalmente pegar in-flagrante). validate-smp: segue
  vermelho honesto (smp2 ainda morre), porem o mecanismo fechado e uma parede a menos.

### P4 — adendo 18 de 17/10 (sobrevivente mapeado: context do RECORD sobrescrito por fora (saves legítimos EXONERADOS por witness); face alterna poison/hang/profundo; 3 testemunhas permanentes armadas)

- FLAGRANTE SEM ESPIRAL (run ctx): `WATERMARK-FRAME pid=5 kind=1 cpu=1 frame=8
  stack=[390d8080,390e8080) outside=1` — o AP acordou o worker do storage com
  `context=8` (lê a IVT do BIOS: eip==cs==0xf000d957 = vetor real em 0x08, lixo
  coerente). Run anterior (surv): `pid=9 frame=390e7a5c` — context do record do
  desktop host apontando para DENTRO da stack do pid=5. Duas vítimas, mesma
  forma: o CAMPO context do record foi escrito por fora.
- EXONERACAO DOS SAVES LEGITIMOS: witness nova `sched-ctx-save:
  OUTSIDE-OWN-STACK` embrulhou os 4 `current->context = frame` do
  schedule_frame (uniquos escritores dinâmicos) — 0 hits em 3 runs, inclusive
  nos runs que MORRERAM de context corrompido. Conclusao medida: o writer nao e
  o caminho de save; o record e sobrescrito por um escritor externo (forma
  desconhecida ainda) OU o context e corrompido apos o ultimo save valido.
- MAPA ARMADO: WATERMARK-FRAME agora dumpa `OFFSETS` (offsetof de next/context/
  stack/state/pid/cpu) + `RECORD{24 words}` da vitima — no proximo flagrante a
  FORMA do veneno no record nomeia o objeto-fonte (stride de subscription/
  mailbox vs cadeia de frames vs outro struct), como o OVER-dump fez com a
  espiral. Nenhum run desta rodada reproduziu o poison (face rotacionou para
  hang startx/`clear-with-live` e #UD simples) — resultado do mapa PENDENTE,
  honesto.
- INVARIANT `clear-with-live-task`: primeiras amostras em pid1/6/8 com
  `state=2 curcpu=-1 ctx=<valido>` = o fluxo PROJETADO (block salva contexto no
  switch-out e o ramo no-task null o g_current). Refinada para
  `state==RUNNING && g_current==current` (a violacao real que gera GHOST-RUNNING);
  apos refinamento: 0 hits em smp2. A witness de hang (overdue pid=9 state=2
  deadline avancando de 4 em 4 = wait re-armado) continua na familia do item
  0.6/`host-wait de task-exit`, NAO e a classe #5.
- ESTADO DA PAREDE: o fix da espiral (adendo 17) permanece — faces agora:
  (i) poison de record por escritor externo (crashes eip=3/8/heap), (ii) hang
  do ciclo storage/startx (wait re-armado, 0.6). Proxima sonda (1/rodada): o
  RECORD map no proximo WATERMARK-FRAME + cruzar com `kernel_heap_watch_pc` do
  allocpc dos records (process_create_with_stack) para ver WATCHED-FREE — um
  free indevido de record-vivo tambem explicaria context=lixo.
- GATES: make -Werror verde; UP validate-phase6-boot-smoke rc=0 com as 3
  testemunhas novas ativas; smp2: vermelho honesto inalterado (3 runs: poison
  capturado 1x, hang 1x, #UD simples 1x).

### P4 — adendo 19 de 17/10 (witness CTX-OUTSIDE armada p/ corrupção intra-execucao; EXONERADO por medicacao o free-de-record-vivo; face DOMINANTE agora = hang original do P4: host pid=1 em wait sem timeout enquanto pid=9 re-arma wait de 4 ticks)

- ARMADA `tick-guard: CTX-OUTSIDE`: RUNNING tasks tem `context` (ponteiro do
  frame de resume) checado CONTRA O PROPRIO stack validado no tick (antes só
  dispatch via WATERMARK-FRAME). Justificativa medida: run 17/10k-po2 morreu
  em #UD eip=3 SEM WATERMARK — frame intacto no dispatch, corrompido entre
  dispatch e iret. Violacao chama sweep+RECORD. Status: armado; poison face nao
  rotacionou nos 3 runs seguintes (0 hits — honesto: nao-evidencia).
- EXONERADO POR MEDICACAO: `WATCHED-FREE` (allocpc dos records ja vigiado desde
  add_task, scheduler.c:1545) — ZERO hits em 6+ runs smp2 incluindo runs com
  record-corruption capturada. A hipotese "record liberado enquanto vivo (UAF)"
  esta MORTA por observacao. Resta: escritor externo sobre record vivo.
- FACE DOMINANTE AGORA (hang, 2 de 3 runs): sequencia identica nos runs —
  `storage request type=16 from pid=9 to pid=1` → pid=9 entra em wait com
  deadline de 4 ticks e RE-ARMA para sempre (`overdue pid=9 ... deadline=669/
  677/681/...` avancando de 4 em 4) → o HOST `block pid=1 kind=1 svc=2
  event=2 owner=2 deadline=0` — sleep SEM TIMEOUT em canal de evento do proprio
  servico. Este e o item ORIGINAL do P4 (wake do storage worker) sob nova face:
  quem deveria sinalizar o waitable do host (pid=1) ao chegar o request? Se o
  signal cai na janela block-SIGNALED abortada, o host dorme para sempre e o
  cliente roda o loop de timeout+rearm — dead-lock de wake-loss com watchdog
  proprio (sched-watch). Proxima sonda (1/rodada): auditar mk_service/mailbox
  push → kernel_waitable_signal no caminho storage-request→host, e o loop
  `scheduler_block_current_ex` (abort SIGNALED existe; falta o re-check do
  mailbox apos abort?) — reproduzir UP-falso-positivo? UP nunca hanga (sem AP
  host path). Teste-alvo: `storage request` logado + host dorme sem signal.
- GATES: make -Werror verde; UP validate-phase6-boot-smoke rc=0 (CTX witness
  ativa, 0 falso-positivo); smp2: 3 runs = 2 hang + 1 #UD simples, vermelho
  honesto, sem regressao.

### P4 — adendo 20 de 17/10 (wake-loss do cliente storage REFUTADO por medicacao; hang = TEMPESTADE de requests de task-userland pid=5; crash face: eip DENTRO da stack do pid=9)

- INSTRUMENTOS NOVOS (permanentes, budgeted): (1) `ipc_queue_stats()` —
  profundidade da fila + pending/wake/timeout/waiters do waitable de qualquer
  task; (2) trace do timeout do cliente agora inclui hostQ/hostW + cliQ/cliW;
  (3) `storage waitloop:` — uma linha por iteracao do wait do cliente (budget
  16) com `recv` e os IDs da reply; (4) `first-of-kind req` — dedupe por
  (src,svc,type), nomeia a PRIMEIRA vez de cada tipo de request.
- MEDICAO-CHAVE (runs qs/wl, 7 runs): TODAS as iteracoes amostradas tem
  `recv=276 r{src=1 dst=9 type=16}` = a reply do storage CHEGA, e do tamanho
  certo, com os IDs certos. O timeout do cliente nunca disparou
  (`request timeout` = 0 hits em 5 runs com a traca nova). A HIPOTESE DE
  WAKE-LOSS DO CLIENTE ESTA MORTA por evidencia — o canal storage
  request→reply funciona.
- REDIRECAO DO HANG: census dos runs-hang: `calls=309430` em ~15.7k ticks
  (~20 entradas schedule_frame POR TICK) com `sel5=269291` — pid=5 e uma task
  USERLAND (first dispatch `eip=userland_entry+0x0 kind=1 service=1`) em
  TEMPESTADE request/reply contra o host service=1; os demais serviços
  (owners=2,4,5...) ficam BLOCKED atras. O hang nao e wake-loss: e uma
  task-userland que nao bloqueia (loop vivo de IPC), monopolizando o dispatch
  enquanto a cadeia de servicos espera. Proxima sonda: o trace
  `first-of-kind` num run-HANG (este morreu aos 8s antes da tempestade) para
  nomear svc/type do loop de pid=5 + checar se o host do service=1 responde
  (depth do host) — e entao: starvation de scheduling vs bug de app.
- CRASH FACE (wl7): `#UD cpu=1 eip=391026c3 cs=8` — eip DENTRO do bloco de
  stack do pid=9 ([391026e0,391426e0): 0x391026c3 fica 29B ABAIXO do BASE —
  no bloco heap imediatamente anterior, agora rotulo-me: alvo = o que executa
  la. A witness CTX-OUTSIDE continua a arma certa (ainda nao pegou: poison nao
  rotacionou nos runs com ela).
- GATES: make -Werror verde; UP validate-phase6-boot-smoke rc=0 com os
  instrumentos novos; smp2 7 runs: faces {crash eip-heap, hang tempestade,
  crash f0 53} — vermelho honesto, mas o mapa de candidatos encolheu de
  "wake-loss storage" para "livelock de pid=5 + escritor intra-stack".

### P4 — adendo 21 de 18/10 (ctx-save PAGOU o primeiro write-site: pid=9 sendo salvo com ESP vivo dentro da stack do INIT; SUSPECT narrowing; sane pid-bound; veneno OSCILA no record)

- nc1 (hang, 533 linhas) produziu a cadeia mais limpa da campanha:
  (1) `WATERMARK-FRAME pid=9 cpu=0 frame=390be1b0 stack=[391026e0,391426e0)
  outside=1 eip=390b5f80 cs=390e76b4` — resume de pid=9 com context apontando
  para 0x390be1b0, que é o MEIO da stack do INIT (init=[390b6600,390be600),
  topo 0x390be600; 0x390be1b0 = topo-0x450 = região de trap-frame VIVO do
  init). eip=0x390b5f80 = payload do record do init (dados como código);
  (2) `sanitize cpu=1 old=5 next=9 frame=390be1b0 eip=40842 cs=202` — o MESMO
  frame envenenado reaparece num dispatch na OUTRA cpu — dois cpus resumes do
  mesmo valor;
  (3) `sched-ctx-save: OUTSIDE-OWN-STACK site=preempt-skip pid=9 frame=390be1b0`
  — PRIMEIRO write-site nomeado: o tick em cpu=0 salvou `current->context =
  frame` com frame=ESP vivo dentro da stack do init, OU SEJA, cpu0 EXECUTAVA
  pid=9 COM ESP na stack do init antes do save (a save é só o mensageiro);
  (4) O RECORD dump do MESMO momento mostra `ctx=391423e4` (VALIDO!) no offset
  0xc — o campo foi reescrito com valor válido entre o dispatch envenenado e a
  leitura. O veneno OSCILA: dois escritores com visões diferentes do record.
- LEITURA UNIFICADORA: 0x390be1b0 é ESP de trap do init. Dois cpus resumindo
  com o mesmo frame = DUPLA EXECUCAO de pid=9 (um dispatch legítimo + um
  resume stale), e o "salvamento" de um cpu grava o ESP do OUTRO no context.
  A janela de dupla posse sobreviveu ao fix da espiral: candidatos剩下的 que
  criam RUNNING-sem-dono: (a) inline_wake do timeout hook (task->state=RUNNING
  sem garantir que NINGUEM mais o despachou), (b) GHOST-RUNNING repair
  (pid=10 cur=0 visto em nc3 era o SINTOMA), (c) wake que seta READY mas a task
  ainda esta em mid-switch-out. Próxima sonda (1/rodada): no ctx-save OUTSIDE,
  dumpar `g_current[cpu]`, eip/cs do frame, e o owner legitimo do range do ESP
  (qual task tem stack cobrindo 0x390be1b0) — se o dono for pid=1 com
  state=RUNNING na OUTRA cpu, está provado o par A/B: inline_wake do timeout
  criando um segundo dono vivo.
- INSTRUMENTOS DA RODADA: node_sane agora exige `pid < 65536` (hg3 passou um
  `timeout pid=823182467` porque o bound só exigia pid>0); SUSPECT esp0
  narrowed p/ `runs_in_user_mode` (pg4 pegou falso-positivo: kernel task com
  esp0=boot-idle legitimo — tasks do kernel nunca fazem ring3->ring0).
- GATES: make -Werror verde; UP phase6-boot-smoke rc=0 (bounds novos ativos);
  3 runs smp2: 0 SUSPECT (narrowing limpo), 0 BAD-NODE, 1 CTX-OUTSIDE
  capturado + cadeia WATERMARK/sanitize/preempt-save — a face de poison agora
  tem WRITE-SITE e OSCILACAO documentados.

### P4 — adendo 22 de 18/10 (DUPLA POSSE PROVENHA ao nivel do census: pid=1 RUNNING em cpu=1 no MESMO tick em que o preempt-skip de cpu=1 trata pid=5 como current; WHO-line foi comida por serial overrun — corrigida em 2 linhas curtas; 4 runs p/ amostrar o poison)

- EVIDENCIA (who4, o run que ALCANCOU o desktop): linha 807 census t=4000:
  `1:1:1:0:2:2` = pid=1 state=1 RUNNING current_cpu=1. Linha 809, mesmo
  instante: `sched-ctx-save: OUTSIDE-OWN-STACK site=preempt-skip pid=5
  kind=1 cpu=1 frame=390be1b0` = o tick de cpu=1 salvou ESP=0x390be1b0 (DENTRO
  da stack do pid=1) como context de pid=5. Dois registros afirmam cpu=1 no
  mesmo tick: pid=1 (state RUNNNING@cpu1) e pid=5 (g_current[1] que o tick
  serviu). A DUPLA POSSE nao e mais teoria de rotacao de face — esta no
  snapshot do census. sel5=11695/t4000 ~3/s = pid5 ativa; logo quem corrompeu
  o ESP de cpu=1 para a pilha do init foi o RESUME de pid=5 com context ja
  envenenado (0x390be1b0 reaparece identico em nc1 — o mesmo enderego entre
  runs = offset estrutural do init-frame em pilha de ~2KB usados).
- INSTRUMENTO CORRIGIDO: a WHO-line original (~170 chars) NAO CHEGOU ao
  capture (serial overrun sob carga smp2 — mesmo mecanismo que truncou
  `stack=[390d8080,1055)`); agora splitada em 2 linhas <=80 chars
  (`WHO owner=.. st=.. rng=..` / `WHO cpu=.. cur=.. eip=.. cs=..`). LIÇÃO
  gravada: testemunhas serial em smp2 devem caber em ~80 chars.
- AMOSTRAGEM: who5-8 (build com WHO curto): 0 hits — a face poison e rara
  (~1/8 runs ate hoje; nc1 1/3, who4 1/4). Os 4 runs: 2 hang + 2 crash sem
  poison. WHO fica armada (budget 6) e o proximo flagrante virá com owner +
  g_current da cpu salvadora nomeados.
- ESTAD DA CAMPANHA (honesto): classe #5 reduzida a UM mecanismo: janela de
  dupla posse entre o timeout inline-wake (`task->state = RUNNING` se
  g_current[cpu]==task) e o dispatch legítimo na outra extremidade. Próximos
  passos concretos: (a) capturar WHO com owner=1 e cur=5 (previsao da
  hipotese) OU refutar; (b) auditar o par inline-wake vs switch-out no
  timeout hook — o wake NAO deve marcar RUNNING se a task ja saiu da cpu
  (current_cpu>=0 mas g_current[cpu]!=task = ghost em formacao); (c) manter o
  GHOST-repair como rede de seguranca mas nomear a FORMACAO, nao o sintoma.

### P4 — adendo 23 de 18/10 (WHO ainda sem flagrante (0/4 runs); w2-1 mostra o AVAL final da cadeia: ret deslocado para DADOS da stack do pid=5 executa garbage que ESCREVE em .text — W^X nega (PF err=3 cr2=238fb))

- SAMPLING who5-8/w2-1..3 (build WHO-curto, budget 6->12): face poison 0 hits;
  faces observadas: 2 hang, 5 crashes (eip=3/7/0x217/heap) — a janela de
  dupla-posse do ctx-save e rara (~1/8), WHO continua armada.
- w2-1 (PF em 418): `pf-text: err=3 eip=390e75bc cr2=238fb` — eip = DENTRO do
  payload da stack do pid=5 ([390d8080,390e8080)) = o CPU estava EXECUTANDO
  dados de stack (bytes empurrados como instrucoes); o window[-32..+12] do
  CR2 mostra codigo real de .text (prologo/epilogos mov/movb/xor) — a escrita
  foi NEGADA por W^X (CR0.WP), provando o instrumento e dando a primeira
  imagem do AVAL: `ret` com return-address corrompida caindo em dados de
  stack legitima. O PRIMEIRO salto deslocado continua sem flagrante (ele nao
  passa pelos 4 saves nem pelo dispatch sanitize — senao WHO/CTX teriam
  pegado): candidato remanescente = buffer de retorno corrompido no TOPO da
  pilha por overflow de LOCAL (memcpy/strncpy de path/mensagem 276B em
  buffer local menor no caminho storage). Proxima sonda (1/rodada): auditar
  buffers locais do caminho do host storage (mk_service_request/
  ipc_receive/sys_storage_load handlers): todo `char buf[N]`/`uint8_t
  frame[..]` com copy de dados externos, checar bounds N vs fonte; o
  `request_copy` de 276B em stack de task de 32K+ e visivel — os buffers
  locais menores nos handlers do init sao os suspeitos.
- ESTADO HONESTO: a parede da classe #5 reduziu a UMA assinatura final
  (execucao-em-dados) com dois estagios: estagio-1 (ainda sem testemunha:
  overflow de local corrompendo return/saved-esp) e estagio-2 (cascos: WHO/
  CTX/WATERMARK pegam quando o dano toca context/salva; pf-text/W^X pegam
  quando toca .text).

### P4 — adendo 24 de 18/10 (auditoria estatica do estagio-1 NEGATIVA — todos os caminhos de copy do storage/fs estao clampados; overflow do return-slot exige captura dinamica; WHO/CTX sao a aposta)

- AUDITORIA NEGATIVA documentada (procura pelo escritor do return-address no
  caminho storage, candidates de `char[N]` local): (1) `mk_transfer_copy_from/
  to` clampam `size <= slot->size` E os syscalls validam a janela do usuario
  via `sys_user_ptr_ok` — o dst kernel-side e heap slot, nunca stack local;
  (2) fs paths viaam por transfers de tamanho dinamico (`strlen+1`) com
  validacao host-side (`mk_fs_validate_path_transfer` checa tamanho+terminador
  ANTES de usar) — sem buffer local fixo no caminho; (3) `tail_sector[512]` e
  lido com exatamente 1 setor; `verify[512]` idem; `realpath_path` limita
  `len>=256` antes do copy; (4) drivers ata/nvme/ahci: buffers locais casam
  com o tamanho do setor/estrutura lida; (5) `ipc_receive` clampa `tocopy <=
  bufsize`; `g_current[]` e mutado EXCLUSIVAMENTE sob g_scheduler_lock (grep
  repo-wide: init/reuse/idle-null/terminate-clear) — inline_wake nao pode
  criar RUNNING sem g_current correspondente na mesma janela.
- CONCLUSAO: o estagio-1 (corrupcao do slot de retorno/ESP salvo ANTES do
  primeiro iret ruim) NAO e reproduzivel estaticamente no caminho storage —
  o escritor e dinamico (timing/estado), exatamente o dominio das testemunhas
  WHO/CTX/WATERMARK. A taxa da face poison (~1/8 dos runs) manda o metodo:
  amostrar em lote (6-8 runs por rodada) ate o WHO nomear owner+cur no
  momento da save; a previsão da hipotese inline-wake e `owner=1 cur=1` com
  `task pid != 1` na MESMA linha WHO.
- GATES desta rodada: make -Werror verde; UP validate-phase6-boot-smoke rc=0
  (WHO-curto build); smp2: 6 runs amostrados com budget 12 (0/6 poison; faces
  2 hang + 4 crash, pf-text capturando a negacao W^X em w2-1).

### P4 — adendo 25 de 18/10 (lote 6/6 WHO: 0 poison; DESCOBERTA: WAKE ATRASADO ~220 TICKS ate nos runs que alcancam o desktop — o hang e o mesmo eixo com atraso maior)

- LOTE (build WHO-curto budget 12, 6 runs): resultados: 2 DESKTOP READY (b3
  60s, b6 44s), 1 hang, 3 crashes; WHO 0 hits (taxa poison cumulativa ~1/12).
- NOVA MEDICAO (b3/b6 — os runs BONS): `sched-watch: overdue pid=9` em
  stream continuo com `deadline` avancando de 4 em 4 e tick ~220 a frente do
  deadline (ex: deadline=805 visto em tick=1025) — ou seja: a wait de pid=9
  expira e e RE-ARMADA (~4 ticks por ciclo) mas o scan de overdue observa
  expiracoes ~220 ticks ATRASADOS. Leitura: os wakes do timeout-hook do
  storage-client estao SENDO processados com atraso de centenas de ticks
  (o hook roda no tick da BSP; se o scan/`service: timeout` delay de pid=9 e
  ~200ms, o handoff request→reply do fs fica 200ms por byte-ciclo — e nos
  runs-HANG esse atraso vira INFINITO: mesmo eixo, duas magnitudes).
- HIPOTESE UNIFICADA do hang atual: starvation do timeout-hook (varredura
  por tick com ~20 schedule_frame calls/tick de pid=5 + locks disputados) →
  wakes de `deadline` atrasam centenas de ticks (runs bons) ou nunca (hangs).
  A classe #5 (poison/dupla-posse) pode ser CONSEQUENCIA colateral do mesmo
  atraso: janelas longas sem schedule_frame na cpu = trap-frames stale.
- PROXIMA SODA (1/rodada, barata): no trace de wake do timeout-hook,
  imprimir DELTA `late=tick-deadline` (quantificar a distribuicao do atraso
  por cpu/periodo) + checar custo da varredura do hook (SCHED_LIST_WALK_CAP,
  cadencia 1000-tick do census) vs os ~20 calls/tick do pid=5.
- GATES: make -Werror verde; UP smoke rc=0 (build ja amostrado); 6 runs
  honestos documentados. NENHUMA mudanca de codigo desta rodada alem das
  testemunhas existentes — proxima rodada instrumenta o delta de wake.

### P4 — adendo 26 de 18/10 (CORRECAO do adendo 25: o "wake 220 ticks atrasado" era ARTEFATO de copia stale do ring; discrinador live-vs-ring mostra waits SAUDAVEIS; face nova: #UD f0 53 migrou para o bring-up do AP, antes do scheduler-enable)

- DISCRIMINADOR ARMADO (overdue scan agora imprime `live=` e `ch=`): ov4
  (hang) `deadline=8716 live=8926 ch=1f97c8 tick=8925` e run bom `deadline=778
  live=1001 tick=1000` — o wait DEADLINE VIVO esta no futuro imediato (saudavel,
  re-armado a cada ciclo); o `deadline=` do stream e a COPIA STALE do
  block-ring (a scan compara grace pelo RING, nao pelo vivo). Logo a leitura do
  adendo 25 ("wakes atrasam ~220 ticks") esta REFUTADA — retiro a hipotese de
  wake-lag; o hang continua sendo "reply que nao chega", mas NAO por atraso de
  wake de timeout: a wait de pid=9 esta viva e re-armando normalmente.
- FATO NOVO (ov2/ov3): duas estagnacoes ANTES de `smp: scheduler enabled`
  (120s) — ov3 terminal: `#UD eip=3 cs=8 b0=f0 b1=53` (a MESMA assinatura da
  janela storage, agora no bring-up do AP); ov2 para no `lapic id=1 enabled=1`
  do AP sem marcador seguinte. A face f0 53 NAO esta presa a janela storage —
  aparece em qualquer fase com timing adverso. WHO/CTX continuam 0 hits em 10
  runs amostrados — as faces de morte atuais (bring-up/pre-storage) nao passam
  pelos 4 saves nem pelo dispatch de task de usuario.
- PROXIMA SODA (1/rodada): o epilog da IRQ de bring-up do AP — mesma firma do
  adendo 17 (iret de um handler cuja FRAME foi corrompida): ao entrar no
  `smp: scheduler enabled` window, dumpar um snapshot do frame atual do AP
  (esp0/eip) via trap de um vetor de calibracao? Mais barato: o #UD de ov3
  aconteceu na CPU que imprimiu "lapic: id=1 enabled=1" = O PROPRIO AP
  morrendo cedo no caminho `smp_ap_entry` posterior — auditar o caminho
  `smp_ap_entry` pos-`scheduler bring-up` (park loop hlt + wake ipi) contra o
  ESP do trampoline: os mesmo 160B/tick da espiral NAO (idle mask cobre
  schedule_frame), mas o park-loop do AP fora de schedule_frame pode re-entrar
  por IPI de wake (`smp_wakeup_ipi_handler`): audit-lo quanto a reentrancia no
  mesmo espirito do adendo 17.

### P4 — adendo 27 de 18/10 (auditoria do park-loop do AP: reentrancia LIMPA; bug real encontrado = gating INVERTIDO na API de IPI (>=32 rejeitado no register, <32 exigido no dispatch) — corrigido; lote 4/4 sem stall pre-scheduler; WHO ainda 0/14)

- AUDITORIA DO PARK (sonda do adendo 26): `smp_ap_entry` park = `sti;hlt` +
  `schedule()` APENAS com scheduler-enabled; wake-via-IPI stub NAO chama
  schedule_frame (so EOI); o tick ja esta coberto pela idle-mask do adendo 17.
  Veredito: NENHUMA reentrancia no park. As estagnacoes pre-scheduler (ov2/
  ov3) NAO se reproduziram no lote pos-fix (0/4) — eram faces de timing, nao
  o park.
- BUG REAL ENCONTRADO E CORRIGIDO (gating de API, dormente): `smp_ipi_dispatch`
  exigia `vector < 32u` e `smp_register_ipi_handler` REJEITAVA `vector >= 32u`
  — mas os unicos vetores IPI deste kernel sao 0x82/0x83 (>=32, por definicao:
  0-31 sao excecoes de CPU). Consequencia: QUALQUER handler registrado via API
  publica jamais rodaria, e o wake-handler direto (linha de init) so funcionava
  por acidente de rota (IDT stub proprio). Corrigido para o dominio correto
  (>=32 no dispatch; register rejeita <32) e EOI passa a ser de responsabilidade
  do handler quando existe (sem duplo-EOI). Sem mudanca observavel de
  comportamento (rota ativa nao passa pela tabela) — prova: UP smoke rc=0 +
  4 runs smp2 sem regressao de wake/timer.
- LOTE POS-FIX (4 runs): fix1 `desktop: session ready` em 16s (mais rapido
  desta campanha), fix2-4 crashes no mesmo eixo com variantes NOVAS mapeadas:
  (a) fix2 `Page Fault USER pid=9 eip=0xf000d957 cs=0x1b` = o context salvo de
  pid=9 ganhou EIP do lixo da IVT (leitura em frame=8 — mesmo par de bytes das
  capturas antigas), e o handler de PF de usuario ABATEU o task corretamente
  (contencao de falha funcionando); (b) fix3 `GP err=3c0 eip=0x369
  cs=0x390be1ac` — o ENDERECO-FAMILIA do init-ESP (0x390be1a0-30) aparece como
  CS de um iret: frame lido com DESLOCAMENTO de ~64B abaixo do topo da pilha
  do init, nao lixo randomico; (c) fix4 = #UD eip=3 f0 53 classico.
- ESTADO: WHO/CTX 0 hits em 14 runs — os caminhos de morte atuais NAO passam
  pelos 4 saves nem pelo dispatch de resume (sanitize so ve o frame no
  momento do dispatch; o deslocamento acontece depois/entre). Proxima sonda
  (1/rodada): medir O CONTEUDO do topo da pilha do init — witness no
  `switch-out`/`block-save` do schedule_frame: apos gravar context, guardar o
  VALOR do frame de todos os tasks num snapshot por-cpu (last-saved-frame +
  tick) e comparar no momento do GP/#UD (dump via kernel_panic: "last-frame
  cpu=N pid=.. frame=.. tick=..") — transforma "frame apareceu deslocado" em
  "o ultimo save legitimo foi X em T; o resume usou Y != X" = nomeia a janela
  entre-save e resume onde o conteudo some.

### P4 — adendo 28 de 18/10 (save-ledger armado + wakes agora sob lock (trafos eram MENTIRA de post-unlock: `timeout pid=957248588` era o print lendo record ja-poisoneado, nao o walk); node_sane endurecido p/ canary; 0x390be1b0 = ESP legitimo recorrente do init — o veneno COPIA ESPs vivos entre records)

- TRUTHFULNESS: `scheduler: timeout pid=...` era impresso DEPOIS do unlock
  re-dereferenciando woke_tasks[] — lg3 mostrava pid=957248588 (0x390e744c,
  ESP do topo da stack do pid5) porque o record foi re-escrito na janela
  unlock->print. FIX: snapshot {pid,class,evt,owner} sob o lock (hs1-6: todos
  os wakes agora mostram pid=9/valores small-legit). A "face fantasma" era
  artefato de TRACE — menos um candidato real.
- NODE_SANE endurecido: kernel_heap_is_block aceitava INTERIOR-pointers (palavras
  de trap-frame em [p-16] decodificam como header valido); agora usa
  kernel_heap_block_validate (exige canary de cauda intacto). Lote 6 runs:
  BAD-NODE 0, GHOST 0 — walk ainda nao achou interior-pointer; defensor fica.
- SAVE-LEDGER ARMADO + PAGOU no 1o dump (hs4, morte aos 58s): cpu=0 salvou
  pid=1 f=390be1b0 t=2198/2199/2200/2200 — **0x390be1b0 e o ESP de trap
  VIVO e legitimo do init** (preempt-skip de init parado no mesmo ponto a
  cada tick). RELEITURA das capturas nc1/who4: o "veneno" copiado nos
  context/pid-fields de pid=5/pid=9 nao e endereco fabricado — alguem copia
  o ESP vivo do init (ou de outra task) PARA OS CAMPOS DE OUTRO RECORD. O
  ledger da a assinatura por-tick; a busca agora e o COPIADOR (strncpy/memcpy
  que trata words[4] de record como buffer), nao mais "frame deslocado".
- hs4 morte: `#UD cpu=0 eip=7 cs=390d7a00 b0=f0 b1=c3` + installs=55791
  {cpu0=4793 cpu1=50998} = o storm IPC de pid=5 ja censitado (8-10 switches
  por tick no caminho storage/lang: `lang: try_run begin` na janela da morte);
  o ledger cpu1 fecha pid5 f=390e7644 (topo legitimo). Nada novo no storm;
  a face de morte continua o COPIADOR cruzado.
- PROXIMA (1/rodada): grep dos escritas de 4+ words em process_t* vindas de
  fora do scheduler: todo `(uint32_t *)proc` / cast `process_t *` com writes
  em offsets baixos (0..0x90 = pid..next..context) — especialmente handlers
  que copiam STRUTURAS de task (ex: snapshot p/ /proc-like, event publish
  que le e ESCREVE no record, mk_service_register que salva `process` pointers).

### P4 — adendo 29 de 18/10 (CTX-DRIFT 0/6 = pointer NAO muda apos selecao; dr4 deu a CENA COMPLETA: cpu=0 entrou no kernel com pid=9 current mas ESP na stack do INIT = o ring3->ring0 carregou um esp0 velho; o "copiador" e a PROPRIA CPU empurrando frames no stack errado)

- LOTE 6 runs (build com CTX-DRIFT): 1 desktop 18s, 3 hangs, 2 crashes;
  CTX-DRIFT 0 hits EM TODOS — o `context` lido na selecao e o MESMO no
  momento do iret. O pointer envenenado ja estava la ANTES da selecao; nao ha
  trampling no gap. Combinado com CTX-OUTSIDE 0/14 (saves nunca salvaram frame
  fora do proprio stack... ate agora).
- dr4 QUEBROU O PADRAO E FECHOU A MECANICA (WHO curto pagou as 2 linhas):
  `sched-ctx-save: OUTSIDE site=preempt-skip pid=9 cpu=0 frame=390be094
  stack=[391026e0,391426e0) sw=2` + `WHO owner=1 st=1 rng=[390b6600,390be600)`
  + `WHO cpu=0 cur=9 eip=1 cs=1`. Leitura: g_current[0]==pid=9 (cur=9) mas o
  ESP real do cpu0 ao tick estava EM 0x390be094 = DENTRO da stack do INIT
  (owner=1, init state=RUNNING simultaneo!). frame em [0x390be094] =
  eip=1/cs=1 (lixo de dados de stack). pid=9 tinha sw=2 — no SEGUNDO switch
  dele. Cadeia: o PRIMEIRO ring3->ring0 de pid=9 em cpu0 carregou um esp0
  APONTANDO PARA A STACK DO INIT (o valor do task anterior do cpu0 NUNCA foi
  reposto para pid9 em algum caminho), a syscall handler rodou e empurrou na
  stack do init (destruindo frames do init — dai o init "RUNNING@cpu1/0"
  fantasma do who4!), e o tick seguinte salvou aquele ESP como context do
  pid=9. O mesmo esp0-velho explica: GP err=0x19aa (seletor-lixo lido do
  frame empurrado no lugar errado) e as mortes eip=heap das faces antigas.
- LOGICA FECHADA (reconcilia TUDO): nao existe "copiador misterioso" — a CPU
  e o writers: ring3->ring0 com esp0 stale empurra 20-24B de frame no bloco
  da vitima; handler+syscall empurram mais; os campos de record dos antigos
  "envenenados" = trap frames empilhados no lugar errado. O DOUBLE-DISPATCH/
  ESP0-MISMATCH/CTX-DRIFT ficam 0 porque o bug NAO e de ponteiro de task nem
  de post-selecao: e de UM CAMINHO DE DISPATCH/SETUP que nao publica esp0
  junto com g_current. Suspeitos onde esp0 nao e publicado: (1) o idle-mask
  reject path (adendo 17) retorna SEM publicar — ok, nao muda current;
  (2) scheduler_complete_wait inline_wake mantem RUNNING — ok, mesmo stack;
  (3) o PRIMEIRO dispatch do pid=9 em cpu0: quem o despachou? pid=9 nasceu
  no fork/execve do desktop host — `launch` -> scheduler_add_task (READY,
  sem cpu) -> find_next o pega... publish sob lock faz kernel_tss_set_
  kernel_stack(pid9 top) ✓ a menos que o dispatch tenha vindo pelo caminho
  "reuse" (next==current, que NAO publica esp0 — assume que esp0 ja esta
  certo... para um task que NUNCA rodou nesta cpu, esp0 aqui = do task
  ANTERIOR = stack do init!!). O reuse path e o buraco: `next == current`
  (timeout hook re-seleciona o mesmo task) pula o bloco de publish. Mas
  current nunca rodou aqui... — sonda da proxima rodada: checar ESP0==top de
  g_current[cpu] no ENTRY do handler de syscall (int 0x80/0x90): mismatch
  ai = flagra o exato momento do ring3->ring0 armado errado, com valores
  de esp0 e do top esperado.
- GATES: make -Werror verde; UP validate-phase6-boot-smoke rc=0 (build do
  CTX-DRIFT); 6 runs smp2 (1 desktop 18s; honesto-vermelho).
