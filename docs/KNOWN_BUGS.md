# Known Bugs

Registro de bugs corrigidos **e** dos que seguem abertos. "Corrigido" aqui
exige evidencia executada; o estado corrente da fila de trabalho vive em
`docs/guidelines/COMPLETION_ACTION_PLAN.md`.

## Abertos

- **Classe #5 (SMP real-time, nao-gating)**: em `smp2` com o flag experimental
  ON e sem `-icount`, o kernel corrompe periodicamente estado de tarefa/lista
  de escalonamento e o boot nao chega ao shell (`BAD-HEAD`/`WILD`/`#UD` com
  inteiros pequenos ou ponteiros de heap gravados em campos de `process_t`).
  Todos os guards de heap/scheduler/waitable ficam silentes, `gdb -s` suprime a
  corrida e anexar watchpoint e contraproducente. Boot de hardware real e UP e
  nao e afetado; `smp2`/`smp4` sao flag experimental off por padrao. Probes
  negativos e proximas abordagens registrados em COMPLETION_ACTION_PLAN P4.
- **Sessao interativa trava no meio: console/entrada param de escoar (15/10)**:
  reproduzido com probe manual fora de qualquer harness. Boot rescue ->
  `shell: ready` em 3s -> digitar `netmgrd status` -> o echo dos caracteres
  PARA no meio da linha (`... user@vibe-os:/ % netmgrd st`) e nunca mais
  aparece caracter algum, enquanto o timer segue imprimindo (`tick=1500..4000`)
  = o guest esta VIVO, apenas o escoamento da sessao interativa parou.
  No harness (`validate-network-surface`) o sintoma irmao: o app `netmgrd`
  imprime 13 linhas do proprio status e PARA no meio da lista
  (`event stream: mailbox` e a ultima), sem `socket rx capacity:` nem o
  `shell: command exit netmgrd rc=`. Ou seja: nao e (so) timing de sendkey do
  harness — ha um travamento real de sessao interativa, que e a CAUSA RAIZ das
  flakes de `validate-network-surface`, `validate-abi-compat-surface` e
  `validate-audio-stack`. Nao reproduzido em UP headless sem interacao.
  ESTADO DA INVESTIGACAO (15/10 parte 3 — instrumentos medidos): (a) o echo do
  shell NAO passa pelo servico de console (`console_write` -> `sys_text_write`
  kernel), entao o host de console nao e o culpado. (b) NENHUM request IPC
  estoura: o budget de `service: request timeout` (16) nunca e consumido em
  nenhum log -> descarta "request preso com timeout". (c) PRODUCAO DE INPUT
  SADA SA (medido com o novo contador `input-diag`): durante o stall os
  contadores avancam (`scancodes`/`produced`/`consumed` sobem juntos,
  `drops=0`), ou seja tecla chega, vira key e e consumida — o gargalo NAO e o
  teclado/8042/mailbox. (d) O stall tem DOIS modos independentes de input:
  (i) `boot mode: rescue shell` aparece mas `shell: ready` NUNCA aparece com o
  timer seguindo (shell trava ANTES do prompt); (ii) shell da prompt, consome
  `netmgrd status` inteiro, mas o COMMANDO nao completa (sem
  `shell: command exit netmgrd rc=`). Um watchdog de infinite-wait no sweep do
  scheduler foi tentado e REMOVIDO: disparava em idle legitimo (shell/host
  bloqueado sem deadline e normal), sinal ruidoso nao-calibrado. INSTRUMENTOS
  QUE FICAM: contadores `input-diag` (orcados) em
  `kernel/drivers/input/{input,keyboard}.c` + host-state peek no trace de
  `service: request timeout` em `kernel/microkernel/service.c`.
  AVANCO (15/10 parte 4 — re-producao com o host peek): o modo (ii) esta agora
  LOCALIZADO por evidencia, NAO mais por hipotese: `printf` do app ring3 vai por
  `write(1)` -> `SYSCALL_WRITE` -> `sys_fs_write` -> `mk_filesystem_service_write`
  (transfer + `mk_service_request(MK_SERVICE_FILESYSTEM)`), NAO pelo caminho de
  teclado/redes que eu vinha suspeitando. O `netmgrd` imprimiu 13 linhas e
  TRAVOU dentro de um `write` de stdout; na MESMA re-producao o budget de
  `service: request timeout` (agora com host-state) NAO disparou nenhuma vez ->
  o request de filesystem do app NAO esta estourando timeout, esta preso mais
  fundo (host FS e provavelmente o seu storage aninhado sem ser despachado /
  flow-control), e o app espera a resposta indefinidamente. A hipotese antiga
  "round-trip MK_SERVICE_NETWORK" estava ERRADA e foi substituida por esta
  medicao.
  REFINAMENTO DECISIVO (15/10 parte 5 — evidencia em boot rescue-shell digitando
  `js`, apos remover os placeholders falsos de js/ruby/python): o app roda,
  escreve a saida completa no console e SAI LIMPO — serial mostra `app: main
  returned`, `app: atexit done`, `lang: app returned js` e `init: task-event
  terminated pid=12 prio=app class=app` (o evento de terminacao FOI publicado).
  A shell, porem, NUNCA imprime `shell: command exit js rc=` nem volta ao
  prompt, e os comandos seguintes digitados nunca rodam. => o stall (ii) NAO
  esta no app / console / storage; esta no WAKE do host-wait da shell quando o
  processo filho termina (caminho `host_wait_for_task_exit` em
  bootstrap_hosts.c + assinatura de task-event). Isso também reinterpreta o
  netmgrd: ele provavelmente TERMINOU; a shell e que nunca acordou. OBSERVAR
  TAMBEM: `sched-dispatch: BAD-FORENSICS ... ctx=0x13fff...` ainda dispara em
  app ring3 saudavel (o fix de 15/10 cobriu ebp/esp, mas nao o check de
  frame/ctx quando o contexto salva na janela da stack de usuario) — trace e
  ruido a calibrar em passada propria.
  DECISAO DE RAMO MEDIDA (15/10 parte 6 — trace 'host-wait: stalled'
  adicionado em userland/bootstrap_hosts.c host_wait_for_task_exit): no stall
  re-produzido com o binario novo, a shell (pid=10) NAO esta no loop de polling
  (nenhum 'host-wait: stalled' em ~10s, quando 256 polls o imprimiriam) e NAO
  retorna do wait; antes do launch os waits de 4 ticks do pid=10 venciam
  corretamente (deadlines 8522→8562 avancando). => o modo (ii) esta PRESO
  DENTRO de um unico kernel_waitable_wait_timeout com deadline curto que NUNCA
  dispara. OBSERVAR: o budget do trace 'scheduler: block' e 24 e esgotou ANTES
  do launch, entao a ausencia de block-traces apos js nao e prova por si so.
  RAMOS A DESCARTAR COM O PROXIMO INSTRUMENTO ('sched-watch: overdue-wake' no
  proprio sweep, budgetado): (A) o sweep nao visita o no da shell (lista
  truncada/orfao — mesma familia das assinaturas LIST-CYCLE/BAD-NODE das
  classes SMP) — se o overdue disparar p/ pid=10 no stall, 0.6 CONVERGE para a
  causa da classe #5; (B) a task nao esta BLOCKED (stuck noutro lado do
  receive) — se nao disparar, instrumentar scheduler_task_event_receive.
  CENSUS + AGING (15/10 parte 7 — instrumentos 'sched-watch: overdue' e
  'sched-census' em kernel/process/scheduler.c): o census (pid:state:cpu de
  todo no alcancavel, 0=READY 1=RUNNING 2=BLOCKED 3=TERMINATED, cpu=9 e -1)
  mediu o stall de forma identica de t=2000 a t=16000: pid=10 (shell) e pid=11
  (loader) em READY com current_cpu=-1, enquanto pid=5 ficava RUNNING na cpu0 o
  tempo TODO — inanicao, nao lista truncada nem deadline perdido (o detector
  overdue ficou silencioso). Causa apontada no codigo: scheduler_task_score nao
  tinha NENHUM termo de envelhecimento (base = tier*16 + afinidade…), entao uma
  tarefa de tier melhor que re-escala sempre vencia a disputa e a task recem-
  acordada nunca era despachada. FIX APLICADO (parcial): aging por tempo desde o
  ultimo run (cap 64 pts > span de 4 tiers) em scheduler_task_score. EFEITO
  MEDIDO: pid=10 deixou de ficar READY eterno e passou a BLOCKED (ou seja, a
  shell VOLTOU A RODAR), mas o stall AINDA nao fecha (sem 'shell: command exit
  js rc='); os estados seguintes permanecem identicos entre amostras. => resta
  um SEGUNDO fator; proximo instrumento: census estendido com wait_deadline/
  wait_channel do bloqueado + o flag g_scheduler_preemption_ready e
  g_timeslice_remaining[0] (a tentativa de incluir os dois ultimos nao compilou:
  as variaveis sao declaradas abaixo do census — mover declaracao ou expor via
  acessor). Hipoteses vivas: wait INFINITO (deadline 0) no caminho pos-comando
  (fs_flush/host_wait), ou preempcao desligada nesse boot.
  CENSUS ESTENDIDO + SEL-COUNTERS (15/10 parte 8 — a hipotese de INANICAO da
  parte 7 foi FALSIFICADA por medicao). Tres instrumentos novos em
  kernel/process/scheduler.c: (1) contadores de entrada g_sched_frame_calls /
  g_sched_frame_preemptive_true / last_cpu / last_cur_pid; (2) g_sched_sel_count
  por pid (incrementado em AMBOS os caminhos de dispatch: next!=current e
  self-pick next==current) — livre de orcamento, logo imune ao alias de amostra;
  (3) census em DUAS fases modulares (0 e 1025), ambas multiplas de 25 (o census
  so roda na cadencia de 25 ticks) e com 2000%4==0 vs 1025%4==1. O alias era
  real: com a amostra so na fase 0 (multipla do timeslice de 4 ticks), ts0 lia
  sempre 4 e parecia NUNCA decrementar; com a fase 1025, ts0 mede 2/3/4 — ou
  seja a path preemptiva DECREMENTA o timeslice e o scheduler PREEMPTA
  (pre==tick: exatamente 1 chamada preemptiva por tick). E os sel-counters sao
  todos NAO-ZERO e crescentes: sel5≈6994, sel10≈1831, sel11≈6922, sel12≈6882 em
  t=8000. => nem "shell nunca e selecionada" nem "timeslice nao anda": a shell
  (pid=10) E despachada centenas de vezes. A assinatura REAL do stall esta no
  serial do MESMO boot: 'host-wait: stalled pid=10 polls=256..4096 alive=1' e
  'host-wait: stalled pid=11 polls=256..4096 alive=1' — os dois HOSTS
  (bootstrap_hosts.c host_wait_for_task_exit) ficam girando; o census mostra os
  pids 10/11 como BLOCKED (state=2) com wait_deadline re-armando para ~agora
  (ex.: 8001 quando t=8000) a cada amostra, i.e. a tarefa esperada fica viva e
  BLOQUEADA num wait temporizado que se re-arma, nunca termina -> o host nunca
  ve o EXIT e o comando nao retorna. => 0.6 NAO e inanicao de CPU; e o par
  wait/exit-notify (o filho bloqueia e nao termina). A guarda de aging segue
  valida como robustez (evita uma classe real de inanicao) mas NAO era a causa
  deste stall. PROXIMO INSTRUMENTO: mirar o lado do FILHO bloqueado (qual wait
  temporizado re-arma) + o storage service pid=1 (a ultima linha de syscall
  antes do stall e 'service: storage request type=17 from pid=12 to pid=1').
  0.6 permanece ABERTO; nada aqui foi claimado como corrigido. Nota: os
  'sched-dispatch: BAD-FORENSICS' reapareceram para pid=12 (frame ring3 com
  regs_in_win=0) — mesma familia do bug #10, ruidoso mas nao fatal.
  PROXIMO PASSO (nao feito aqui): medir o lado do HOST de filesystem
  no momento do stall (estado/agendabilidade do task fs e do storage que ele
  encadeia) — o peek atual so cobre o caminho que ESToura timeout, preciso de um
  que mostre o request que NUNCA recebe reply sem timeout. 0.6 permanece ABERTO;
  nada aqui foi claimado como corrigido.

## Status geral

- Migração para microkernel: concluída
- Vídeo: estável em QEMU — DRM nativo (BGA, i915, radeon, nouveau; i915 commit
  gated) + VESA/LFB com fallback; campanhas em silício real abertas (P6)
- Áudio: validado em QEMU — driver HDA nativo, AC97, PCSPKR fallback; hardware
  real aberto (P5)
- Rede: **virtio IPv4 client valida em QEMU** (DHCP/ARP/ICMP + responders,
  `validate-network-datapath`); e1000/rtl8139 sao dead code (zero callers) e o
  Wi-Fi Intel 7260 e fachada (BDF fixo 0:0:0, scan hardcoded) — ver P3; o
  modulo WPA2 (PBKDF2/PTK/4-way) tem cripto real mas esta orfao
- Jogos: corrigidos — Doom com resolução de assets via aliases, todos os ports BSD com launch correto
- Memória: heap com free-list allocator, physmem até 4GB, paging com suporte a 4KB pages
- GUI: reescrita com microui — desktop dinâmico, modular, 492 linhas (vs 7000+ antes)
- USB: discovery de controladores funciona; runtime mass-storage (BOT)/NVMe tem
  codigo mas sem relatorio de validacao
- POSIX/Compat: camada BSD integrada (libc, pthreads, socket); X11/X-Like em andamento

## Bugs corrigidos

### [x] 1. Teclado estava saindo do modo de vídeo para o shell e causando piscar de tela

- Comportamento observado: no boot para desktop, o sistema lançava `startx`, mas o `userland.app` seguia entrando em `shell_start()`, deixando shell e desktop consumirem teclado ao mesmo tempo.
- Causa raiz corrigida: o conflito de input não era só vídeo; o shell de boot continuava vivo no caminho desktop e competia pelo mesmo stream de teclado, provocando escape de foco e flicker de apresentação.
- Correção aplicada: o `userland.app` agora faz handoff para o desktop e retorna logo após um `sys_launch_app(\"startx\")` bem-sucedido, sem iniciar o shell interativo no mesmo boot path.

### [x] 2. Som não está saindo — CORRIGIDO

- **Causa raiz:** `playback_buffer` sem dreno no soft backend — `mk_audio_service_pump_async()` não consumia o buffer síncrono.
- **Correção aplicada:** adicionado dreno do `playback_buffer` ao final de `mk_audio_service_pump_async()`, resetando head/tail e zerando fill a cada tick.

### [x] 3. Internet / rede está incompleta — CORRIGIDO

- **Causa raiz:** parser DHCP (`network.c:1269`) exigia `sizeof(struct mk_dhcp_packet)` (548 bytes), mas pacotes DHCP reais de OFFER/ACK podem ser menores.
- **Correção aplicada:** tamanho mínimo relaxado para `offsetof(struct mk_dhcp_packet, magic) + 4u`.
- **Drivers adicionados:** VirtIO-Net completo, e1000, rtl8139, WiFi Intel 7260 com WPA2 real (PBKDF2-SHA1, 4-way handshake).

### [x] 4. Doom não encontra os arquivos do jogo — CORRIGIDO

- **Causa raiz:** `doom_try_resolve_wad_candidate()` só tentava o caminho exato, sem fallback para nomes alternativos.
- **Correção aplicada:** adicionado loop que tenta 6 aliases conhecidos (doom.wad, doom1.wad, doomu.wad, doom2.wad, tnt.wad, plutonia.wad).

### [x] 5. Craft estava alterando as cores do desktop

- Comportamento observado: o jogo Craft modificava a paleta global durante a execução e podia sair sem restaurar o estado anterior.
- Correção aplicada: o encerramento foi consolidado em um único caminho de cleanup em `craft_finish_run()`, usado tanto no fechamento explícito quanto na saída natural do jogo, garantindo a chamada de `craft_upstream_stop()` antes de limpar o estado.

### [x] 6. Ports BSD de jogos abriam um terminal em vez de iniciar o jogo

- Correção aplicada: entradas de jogos BSD do Start Menu agora são detectadas como apps destacados e lançadas via `sys_launch_app_argv()`; se o launch falhar, só aí o desktop cai no fallback de terminal.

### [x] 7. Audio Player tinha sumido da busca/menu do desktop

- Correção aplicada: o item voltou a se chamar `Audio Player`, com metadata compatível com a busca do menu e preservando o launch do app gráfico.

### [x] 8. Som de boot não tocava no bootloader

- Correção aplicada: o bootloader agora gera/carrega `VIBEBOOT.RAW` a partir do WAV de boot e o reproduz no `stage2` logo após renderizar o fundo do loader, antes de iniciar o contador.

### [x] 9. Rede: todo `socket()` de fato travava o serviço de rede (deadlock de spinlock não-recursivo) — CORRIGIDO (14/10)

- **Comportamento observado:** LATENTE ate 14/10 — nenhum caminho que passou num teste verde criava um socket de fato (`ping localhost` era fabricado; o roundtrip de gateway usa envio ICMP privado sem socket). A primeira smoke de loopback no kernel (que chama `mk_network_socket_allocate` a partir do tick de rede) travou o boot no primeiro tick: o servico de rede parou de avancar logo apos `network: dhcp discover sent`. Isso prova que QUALQUER app usando `socket()` travaria o servico (coerente com os timeouts de `shell: command` no harness flaky de network-surface).
- **Causa raiz:** `network_queue_raise_ready_callback()` era invocado ENQUANTO `g_network_queue_lock` estava sob posse do chamador (`network_queue_register_socket`, `..._rx_enqueue`, `..._tx_enqueue`). O callback registrado (`mk_network_queue_ready_callback`) chamava `network_queue_get_rx_depth()` / `network_queue_get_tx_depth()`, que readquiriam a MESMA spinlock nao-recursiva (`spinlock_lock_irqsave`) -> self-deadlock na mesma CPU com IRQs desligados. Como `register_socket` ja levanta `READY_SEND` no create, QUALQUER `socket()` pós-init travava.
- **Correção aplicada (na causa raiz, aditiva/internal):** `raise_ready_callback` passou a receber handle/flags/rx_depth/tx_depth POR VALOR (campos lidos sob a lock pelo proprio chamador); o callback nao chama mais nenhuma API que re-entra a lock. Assinatura `network_queue_ready_event_cb_t` e interna kernel-kernel (grep: zero uso em userland) — ABI nao tocada. Provas: `make validate-network-datapath` PASS com `network: loopback smoke ok bytes=21` em UP (com e sem NIC); `validate-phase6-boot-smoke` 6/6 PASS; smoke anterior ao fix travava o boot no primeiro tick (reproducao ao vivo).

### [x] 10. Forense de dispatch disparava em falso para tarefas ring3 e inundava o serial — CORRIGIDO (15/10)

- **Comportamento observado:** cada dispatch de uma tarefa de usuario imprimia `sched-dispatch: BAD-FORENSICS ... frame{... cs=0x1b ...} regs_in_win=0`, repetidamente a cada tick; o volume afogava os marcadores que os validadores interativos leem do serial (o `netmgrd status` "nao terminava" para o harness porque a saida dele ficava soterrada).
- **Causa raiz:** `scheduler_dispatch_forensics()` validava `frame->ebp`/`esp_dummy` SEMPRE contra a janela da stack de KERNEL. Mas um frame de retomada ring3 carrega registradores de USUARIO — o `ebp` aponta legitimamente para a stack do usuario (`[user_stack_top - VIBE_APP_STACK_SIZE, user_stack_top)`), nunca para a stack de kernel. Resultado: falso positivo sistematico em toda tarefa de usuario com `ebp` nao-zero.
- **Correcao aplicada:** quando `frame->cs == USER_CS_SELECTOR`, `ebp` e aceito se for 0, estiver na janela de usuario OU na janela de kernel (frames construidos guardam um ou outro) e `esp_dummy` em qualquer das duas; os checks de `eip`/`cs` (que pegaram a classe #4) continuam valendo. Provas: log de boot com ZERO `BAD-FORENSICS` apos o fix (antes: repetido por tick); `validate-network-datapath` PASS; `validate-phase6-boot-smoke` 6/6.

### [x] 11. USB host: contagem de controladores ficava 0 apos o UHCI iniciar — CORRIGIDO (15/10)

- **Comportamento observado:** log de boot se contradizia — `usb: UHCI at 00:01.2 (8086:7020)` / `bsd_usb: 1 UHCI controller(s) initialized` seguidos de `usb: no controllers found` / `usb: not found`.
- **Causa raiz:** `kernel_usb_host_probe_cb()` (que popula `g_kernel_usb_host_state` chamando `kernel_usb_host_store_controller`) era CODIGO MORTO — nenhum `kernel_pci_enumerate` o invocava, entao `kernel_usb_host_controller_count()` (e toda a API de info/listagem) lia `.controllers == 0` mesmo com um controlador PCI de classe 0x0C/0x03 presente e inicializado.
- **Correcao aplicada:** `kernel_usb_host_init()` passa a chamar `kernel_pci_enumerate(kernel_usb_host_probe_cb, &g_kernel_usb_host_state)` apos a varredura. Provas: boot com `usb-host: controller=uhci pci=8086:7020 loc=0:1.2 irq=b bar=4 io=c040 ports=2` e `[14] usb: init complete (1 controllers, 0 devices)` (antes: `usb: no controllers found`); `validate-network-datapath` PASS; `validate-phase6-boot-smoke` 6/6.
