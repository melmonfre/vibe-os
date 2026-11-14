# Known Bugs

Registro de bugs corrigidos **e** dos que seguem abertos. "Corrigido" aqui
exige evidencia executada; o estado corrente da fila de trabalho vive em
`docs/guidelines/COMPLETION_ACTION_PLAN.md`.

## Abertos

- **[latent, avoided] SMP bring-up: `g_smp_cpu_stage` pode ficar OBSOLETO por corrida de escrita.**
  O BSP grava `SIPI2_SENT(4)` no slot i APOS enviar o SIPI, sem barreira contra o AP, que ja
  escreve `AP_ENTER(5)/AP_LAPIC(6)/AP_MARKED(7)/AP_SCHED(8)` no MESMO slot; com retry entao, o
  BSP pisca 4 POR CIMA do 8 do AP vivo (medido 20/10: `ioapic: affskip cnt=2 en=1 st1=4` com
  `smp: online cpus=2/2` e ap idx=1 rodando). Nao ha dano funcional hoje porque a contagem
  (`g_smp_started_cpus`/`smp_wait_for_cpu`) e o `started` da topologia (setado pelo proprio AP em
  `kernel_cpu_mark_started` depois do LAPIC init, nunca re-limpo) sao as fontes usadas; o array
  serve apenas diagnostico (`smp_dump_cpu_stages`, traco de affskip). Afirmar "online" por stage
  e proibido: consumidores novos devem ler `kernel_cpu_state(i)->started`. (A afinidade IOAPIC da
  rodada 19i nasceu deste teste e usa `started`.)


- **Classe #5 (SMP real-time, nao-gating)**: em `smp2` com o flag experimental
  ON e sem `-icount`, o kernel corrompe periodicamente estado de tarefa/lista
  de escalonamento e o boot nao chega ao shell (`BAD-HEAD`/`WILD`/`#UD` com
  inteiros pequenos ou ponteiros de heap gravados em campos de `process_t`).
  Todos os guards de heap/scheduler/waitable ficam silentes, `gdb -s` suprime a
  corrida e anexar watchpoint e contraproducente. Boot de hardware real e UP e
  nao e afetado; `smp2`/`smp4` sao flag experimental off por padrao. Probes
  negativos e proximas abordagens registrados em COMPLETION_ACTION_PLAN P4.
  **REFINADO 16/10 (dois boots real-time medidos):** (1) o antigo stall de
  wakeup do storage NO smp2 sumiu — `fs: sys_storage_load returned` +
  `init: storage smoke ok` rodam, o AP despacha tasks; (2) a parede atual e o
  `g_head`/no do meio virando bloco de OUTRO dono AO VIVO: o node impresso duas
  vezes seguidas muda de header (`size=0x680 magic=ALLO` -> `size=0x1
  magic=0x1000000`; payload com bytes de metadados de storage/FAT) e o boot
  congela POST-load (sem dispatch de tasks novas); (3) controle `-nic none`
  reproduz igual — os drivers e1000/rtl8139 recem-postos sao INOCENTES; (4) o
  novo guard `process-destroy: STILL-LINKED` (recusa liberar task linkada; no
  mesmo commit) NAO disparou em run de controle inteiro com o guard buildado,
  e a auditoria dos 4 sitios de free de process_t (destroy x3 + `elf_loader.c:
  262`, unico `kernel_free` direto) mostra todos pre-link (`scheduler_add_task`
  so em syscall.c:2016/2479/3757) — `process_destroy`/destroy-paths estao
  EXCLUIDOS; o libertador (ou escritor-alias) e outra via.
  **FECHAMENTO PARCIAL 16/10 parte 2 (as duas faces finalmente NOMEADAS):** (1) o "registro de
  blocos linkados + kernel_free" descobriu-se JA EXISTIR (watch por allocpc, `WATCHED-FREE`) e dar
  0 hits — a premissa de free estava errada; (2) o hook do `heap-sweep` estava INALCANCAVEL
  (dentro de `if (prev!=NULL)`, e os diags chamam com prev=NULL) — o "0 hits" era artefato do
  instrumento, corrigido; com o sweep vivo, `heap-sweep: OVERFLOW ... allocpc=smp_init+0x2c2`
  apontou para o bloco da stack do AP (4096) como regiao danificada. **CORRECAO 16/10 parte 3
  (releitura do layout do heap):** o canario fica no FIM do payload (heap.c: `block + size -
  CANARY`), i.e. ACIMA do `stack_top = stack + size`; pilha que cresce para BAIXO nunca toca o
  proprio canario — entao "canario do bloco da stack + cabecalho do PROXIMO bloco" danificados
  = escrita para CIMA alem do topo da stack do AP, NAO exauriao para baixo como a conclusao de
  parte 2 afirmava. A conclusao "face-1 FECHADA com AP_STACK 32K" esta **REABERTA**: com 32K o
  sweep segue acusando o MESMO bloco (`OVERFLOW block size=0x8020 allocpc=smp_init+0x40b`,
  novo run) — o aumento da stack nao eliminou a assinatur (nem a provou, nem a refutou como
  causa; apenas mudou o alvo do padrao). O que a escrita acima do topo pode ser (trampolim com
  stack_top obsoleto entre retries? off-by no accounting do heap? antigo ponteiro aliasado?)
  fica como questao aberta da classe #5 com proximo passo concreto: ao disparar OVERFLOW num
  bloco de stack, dumpar alem do canario os 8 words SEGUINTES (cabecalho do proximo) e
  cruzar `stack_top` gravado no trampolim vs o stack_top do malloc atual (o retry de
  INIT/SIPI pode deixar um valor antigo patcheado). O 32K fica (defensivo, custo zero).
  (3) a face-2 (bytes de .text != ELF em execucao) foi pega pela armadilha nova
  `paging_protect_kernel_text()` (.text supervisor-RO + CR0.WP; UP boota limpo com wp=on):
  no smp2, `Page Fault err=3 (write/kernel) eip=kernel_text_render_graphics+0xd1
  cr2=0x394db` — o shim grafico de texto do console escrevia `target[py*pitch+px]` checando so
  scr_w/scr_h do MODO, nunca a capacidade real do buffer, vazando para dentro do .text.
  **FACE-2 FECHADA 16/10 parte 3:** capacidade real exposta por
  `kernel_video_target_capacity()` (shadow = tamanho mallocado; LFB direto = janela reservada
  publicada por `kernel_video_set_fb_window()` ANTES de qualquer render — o setter tardio em
  memory-init foi pegado pelo proprio log honesto `text-render: no known capacity ... window=0`,
  que mostrou o shim renderizando antes do memory init), e os 3 loops do renderer clampados por
  `idx < cap` (+ linhas que nao cabem na janela descartadas). PROVA: UP phase6 6/6 com o clamp;
  smp2 real-time pos-fix com ZERO Page Fault/ud-bt (o escritor nomeado sumiu; a parede voltou a
  ser apenas a face-1, reaberta acima). Backtrace one-shot de #UD kernel-mode (`ud-bt`) entrou
  junto e foi o que provou a face-2 (b0/b1 != ELF).
- **Face (f) da classe #5 — tudo dentro de UM bloco de stack (17/10e)**: apos o fix de
  publicacao de esp0 sob lock (tick-guard: 0 hits), o smp2 real-time crasha com storm
  pos-`storage request type=16`: `user-frame mismatch` com cs=heap-address, `spinlock: stuck
  lock=0x390e76ec`, `#UD cpu=0 eip=7 cs=0x10082`, `#UD cpu=1 eip=f cs=0x30303030` (ASCII
  "0000"), `GP err=e93c eip=0x390e7508`, `ud-bt sp=0x390e75b4` — 0x390e7a5c/0x390e7508/
  0x390e76ec/0x390e75b4 todos dentro de [0x390d8080,0x390e8080) (bloco de stack de task de
  64KB): multiplos "donos" apontando para o mesmo bloco. HIPOTESE-LIDER: aliasing de bloco
  (alloc devolve stack ainda em uso), o que explica as witnesses de campo do task passarem.
  ATENCAO metodologica: nesse storm a serial esta INTERLEAVED — apenas valores repetidos
  cross-linha sao evidencia. CORRECAO de leitura (adendo 17): atribui na epoca "cpu=226934/
  expected=216" a mistura de duas printfs; a aritmetica posterior (o valor esperado e
  sempre a constante 0x1B, e o anel de esp0 bate as somas por cpu) mostra que a linha
  tambem pode ser args de printf lidos de regiao ja corrompida — indistinguivel no storm.
  A regra permanece: so valores cross-consistentes entre linhas sao evidencia. Explicacao
  final para TODAS as faces chegou no adendo 17: o idle-loop do schedule_frame aninhava
  ~160B de trap-frames por tick no AP, corroendo record/buffers/canary de cima para baixo.
  Sonda armada (17/10, adendo 12): witness `heap-alias` em
  kernel_malloc contra ranges de tasks vivas — LADO-MALLOC REFUTADO (0 hits no smp2; UP
  limpo) e `heap-free-live-stack` em kernel_free (0 hits = o mecanismo NAO passa pelo
  free). FLAGRANTE (adendo 13): tick-guard CANARY-BAD pegou o switch `old=5 next=0` com
  o RECORD pid=0 trazendo stack=0x390b6fa4 (desalinhado %8=4, dentro da stack do pid=1)
  e size=166074 (nao-potencia) e o iret usando o contexto 0x390be120 EMPRESTADO do
  pid=1 (eip=6 -> GP -> panic). Face rotacionou no run seguinte para `#UD cpu=0
  eip=0x25db4` com bytes IGUAIS ao ELF (retorno 2 bytes antes do alvo legitimo dentro
  de `call` em mk_audio_refresh_topology_snapshot) — leitura atual: ponteiros/retornos
  DESLOCADOS, nao texto plantado (W^X/pf-text limpos). Sweep do header do bloco do
  record armada para o proximo flagrante. Ver
  `docs/guidelines/COMPLETION_ACTION_PLAN.md` P4 adendos 11-14.
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
  TAMBEM: `sched-dispatch: BAD-FORENSICS` ainda dispara em app ring3 saudavel
  — HIPOTESE ORIGINAL (contexto em `ctx=0x13fff...` na janela da stack de
  usuario, check `frame_in_window`/ctx) estava ERRADA; corrigida na medicao
  (o ctx estava na stack do KERNEL, `in_win=1`; o que falhava era `ebp`). FECHADO
  em 30/10 como KNOWN_BUGS #16 / item 0.8 (frame KERNEL mid-syscall de task
  ring3). Este trace era ruido, nao prova do stall 0.6.
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
  regs_in_win=0) — mesma familia do bug #10; FECHADO em 30/10 como #16
  (frame KERNEL mid-syscall de task ring3 — ruido, nao prova do stall 0.6).
  WAIT NOMEADO + TASK-EVENT DROP (15/10 parte 9 — o mecanismo do stall ficou
  nomeado). Estendido o census com dois campos por no:
  wait_event_kind (0=NONE 1=WAITABLE 2=QUEUE 3=SIGNAL 4=COMPLETION) e
  wait_owner_service (MK_SERVICE_*: 2=STORAGE 4=VIDEO 5=INPUT 6=CONSOLE
  7=NETWORK), formato pid:state:cpu:deadline:kind:owner. Medido no boot normal
  (t=35025..40000): os servicos 1..8 aparecem BLOCKED em wait de QUEUE (kind=2)
  com deadline=0 (wait infinito de fila ociosa — SAUDAVEL, cada um no seu
  owner: 1→2 STORAGE, 2/3→4 VIDEO, 6→5 INPUT, 7→6 CONSOLE, 8→7 NETWORK). Ja os
  pids 9 e 10 aparecem BLOCKED em kind=2 (QUEUE) com o deadline RE-ARMANDO para
  ~agora a cada amostra (ex.: 35027 quando t=35025) e owner=0 — que e exatamente
  o loop de host_wait_for_task_exit (bootstrap_hosts.c) chamando
  sys_task_event_receive com HOST_TASK_EXIT_POLL_TICKS=4: espera temporizada de
  fila que vence, re-arma, vence, re-arma. E o serial do mesmo boot traz
  'host-wait: stalled pid=10/11 polls=4096 alive=1' + o traco do init:
  'init: task-event launched pid=12 ... q=32 drop=181 tick=663' (pid=11: q=32
  drop=25). drop= e event->class_dropped_events = kernel_mailbox_dropped() da
  mailbox da CLASSE (scheduler.c:876/875; q= class_pending_depth = mailbox_count)
  => a mailbox de eventos de task por CLASSE ENCHEU (q=32, saturada) e DERRUBOU
  eventos. Mecanismo do 0.6 nomeado: um host aguarda o evento TERMINATED do
  filho, o evento foi DERRUBADO pela mailbox cheia, entao o host nunca ve o EXIT
  e gira para sempre no poll de 4 ticks — a sessao interativa trava no meio.
  NAO e escalonamento (falsificado, parte 8) nem lista truncada. PROXIMO PASSO
  (nao feito): achar POR QUE a mailbox de classe satura/derruba no boot (taxa de
  emissao vs drenagem dos assinantes; possivel leak de assinante que nao drena)
  e garantir que TERMINATED de filho de host NUNCA seja derrubado (fila dedicada
  ou entrega garantida / flush). CAMINHO CONFIRMADO no codigo: o receptor
  scheduler_task_event_receive (scheduler.c:1812) le a mailbox PROPRIA da
  assinatura (subscription->mailbox), e scheduler_publish_task_event a alimenta
  com kernel_mailbox_try_send (scheduler.c:879) — try_send DERRUBA quando a
  mailbox enche. Logo a mailbox de um assinante que acumula eventos pode descartar
  o TERMINATED do filho em silencio; o host fica preso em kernel_mailbox_wait
  (timeout=4, kind=2 QUEUE, deadline re-armando) exatamente como o census mediu.
  RETRACAO DA ATRIBUICAO CAUSAL (15/10 parte 10 — a parte 9 over-claimed e esta
  nota corrige o registro; "concluido = provado, nao pretendido"). Lendo o codigo
  do caminho de wake do host, a hipotese "a mailbox de task-event derrubou o
  TERMINATED do filho -> o host nunca acorda" NAO SE SUSTENTA:
  (a) o host assina APENAS TERMINATED (host_subscribe_task_exit ->
      sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_TERMINATED,...)); eventos
      de LAUNCHED (o burst que enche a fila) NAO vao para a mailbox do host, logo
      ela nao 'inundava' de ruido — volume baixo;
  (b) a mailbox que mostrou drop=181 (q=32) e a CLASSE-STREAM
      (g_task_class_streams), que e ESCREVE-APENAS: nao existe NENHUM
      try_receive/mailbox_wait nela (grep: stream->mailbox so aparece em init,
      count e dropped; SYSCALL_TASK_EVENT_RECV/89 le a SUBSCRIPTION, nao a classe
      stream). Ela NUNCA e drenada, entao class_pending_depth satura em 32 e
      class_dropped_events sobe sem limite — o drop=181 e um artefato de um buffer
      sem consumidor (CODIGO MORTO que produz telemetria enganosa), NAO prova de
      perda de entrega;
  (c) o fallback de LIVENESS do host (host_wait_for_task_exit ->
      host_task_pid_alive -> sys_task_snapshot) cobre a perda de evento: a varredura
      da snapshot FAZ `continue` em PROCESS_TERMINATED (scheduler.c:2047, antes do
      bloco de resumo E do bloco de entradas), entao o pid de um filho encerrado
      NAO APARECE na snapshot -> alive() falso -> host ACORDA mesmo com o evento
      perdido. alive=1 persistente significa que o FILHO esta de fato vivo (nunca
      chegou a TERMINATED), nao que o evento sumiu.
  (d) Alem disso, o boot medido na parte 8/9 NERA o stall: o probe nao alcancou o
      rescue shell ('rescue selected: False') — os 'host-wait: stalled pid=10/11
      alive=1' amostrados eram supervisores do DESKTOP normal esperando apps de
      longa duracao (comportamento ESPERADO, vivo=correto), não o stall de 0.6.
      As refutacoes (inanicao, flood, lista truncada) continuam validas pois sao
      medidas estruturais do scheduler/eventos; mas a observacao do stall EM SI
      exige reproduzir o modo rescue com o comando preso.
  => 0.6 NAO e problema de entrega de evento. O que fica vivo como alvo, do ramo
  rescue da parte 7: a shell presa num kernel_waitable_wait_timeout de 4 ticks
  que NUNCA dispara, sem nem re-pollar. Acao: registrar a classe-stream consumida-zero como
  limitacao honesta (wire um consumidor OU remover — DECISAO de arquitetura da
  mantenedora, nao mexo sem ela); a busca do 0.6 real volta para o caminho
  exit/reap do filho e o waitable-wait da shell. 0.6 permanece ABERTO.
  PROXIMO PASSO (nao feito aqui): medir o lado do HOST de filesystem
  no momento do stall (estado/agendabilidade do task fs e do storage que ele
  encadeia) — o peek atual so cobre o caminho que ESToura timeout, preciso de um
  que mostre o request que NUNCA recebe reply sem timeout. 0.6 permanece ABERTO;
  nada aqui foi claimado como corrigido.

- **[ABERTO — campanha MEDIDA, 7 mecanismos corrigidos] #19 `netmgrd status` nunca completa — item 0.6 modo-iii / P3.**
  **Sintoma canonico (gate `make validate-network-surface`, vermelho):** o harness digita
  `netmgrd status`, a linha nunca chega a `busybox_main` (nenhum `shell: command netmgrd` e
  nenhum `shell: command exit netmgrd rc=0`), e o comando nunca roda. Em 19_run13 o serial
  registra 13 banners `shell: ready` e ZERO `shell: command`/`^C`: todas as submissoes foram
  vazias; `input-diag` fecha em `scancodes=36 produced=18 consumed=15 drops=0` (todas as 15
  teclas fisicas — 14 chars + Enter — consumidas pelo servico de input, nenhum drop no driver).
  **Refutacao por medicao (01/11, sonda nova `g_ipc_wake_tick`/`g_wake_max_d` no
  scheduler.c):** wake→dispatch maximo de **1 tick** para o host de input (p=6) e para a shell
  (p=11) — census `w6=1 w11=1`, zero linhas `wake-stale` (limiar 20 ticks, budget 12 nunca
  gasto). A hipotese original "READY nunca despachado / inanicao do scheduler" esta
  **REFUTADA** como causa do stall atual: boost -128 + aging entregam o despacho em ≤1 tick.
  O fingerprint antigo `host-state=0 hostQ=1 hostW{pend=1 wake=0 waiters=0}` era o host
  READY por ~1 tick entre a wake e o dispatch, nao inanicao sostenida.
  **Mecanismos MEDIDOS e corrigidos nesta campanha (runs 19_run1..11, todos no tree):**
  (A) boost de wake IPC perpétuo (wait_result/class/budget nunca limpos) → consumo one-shot no
      dispatch; sem ele o score ficava -32/-24 para sempre e mascarava o aging;
  (B) clamp `base_score < 0 → 0` colava todo waiter envelhecido no MESMO 0 de um tier-0 fresco
      (empate resolvido por ordem de varredura) → clamp removido; tier-6 no teto = -31 estrito;
  (C) lost wakeup na janela enqueue→block do `kernel_waitable_signal`: o pop do head ainda RUNNING
      fazia `scheduler_complete_wait` recusar (-1) e a task bloqueava FORA da chain com o pending
      ja contado → peek de `state==BLOCKED` antes do pop + auto-consumo pos-block no waiter;
  (D) magnitude do boost de wake (-32/-24) nao vencia um incumbente tier-0 fresco dentro do budget
      de reply (medido host tier-6 pontuando 64+ por ~384-512 ticks) → -128 (abaixo do cap de
      inanicao 16*7+3-128=-13; efeito limitado pelo one-shot de A);
  (E) yield apos poll sem tecla no `read_line` (shell e `vibe_app_read_line`) tirava a shell do
      ciclo SIGNALED e a jogava no pool de aging tier-6 (770-1170 ticks ate o proximo poll) → yield
      removido dos dois loops; o poll IPC bloqueante e o proprio pacing;
  (F) budget de reply default 32 ticks < latencia real do handshake (3 pernas × preempcao de 4
      ticks) → timeout -> retry re-enviava request EM VOO -> duplicatas -> `unexpected reply`
      flood (cliQ 1→16, pend 1→31, deferred overflow) → budget 128 (precedente FILESYSTEM);
  (G) `fs_tick()` por iteracao no loop de leitura disparava writeback de storage (ate 4
      `sys_storage_write_sectors` bloqueantes por step) e encadeava a shell atras do storage
      (`waiting_src=1`, linha fragmentada) → throttle 1x/100 ticks no read_line; o loop
      principal de comando e o desktop seguem chamando fs_tick por tick.
  **RODADA 02/11 — a causa-raiz do input path foi fechada por medicao (runs 19_ns10..22):**
  (H) *dispatch steal do boost de wake*: o fix A consumia o boost limparndo
  `wait_result/class/owner` NO dispatch, mas e nesse exato momento que o waiter recem
  acorda le `wait_result` apos o yield — o wake era roubado; o task re-bloqueava sem
  rodar o `ipc_receive`, fila inchando sem dreno (medido: `ipc-pos head=0` apos cada
  pop-wake, `hostQ` 1->16, `waiters=1 wake=0`; e 16 `request timeout` no boot desktop
  contra 0 no HEAD-A/B). Fix: consuncao one-shot toca APENAS `wake_boost_budget` (o
  ramo -128 ja o exige !=0), o resto e do waiter. Prova do efeito em cadeia: o JAM do
  storage host sumiu (smoke desktop: 16->1 timeouts, `storage smoke ok`) e a linha
  digitada passou a SUBMETER (`shell: command netmgrd` pela 1a vez).
  (I) *perda de teclas no caminho PS/2->read_line* — duas causas medidas: (i) o ISR
  lia 1 byte por IRQ (backlog atras de janela IRQ-off nao tinha nova borda) -> passou a
  drenar em loop, mesma forma de `kernel_keyboard_poll` (gate 19_ns11: `rk 115 8` em
  diante o `status` comecou a chegar); (ii) o modelo de consumo tinha DOIS consumidores
  da mailbox de teclas (host via KEY_READ + shell lendo mailbox direto) e a espera curta
  do shell deixava reply orfao (`unexpected reply` flood). Fix: `MK_MSG_INPUT_KEY_READ_WAIT`
  (109, aditivo) servido pelo HOST (consumidor unico, bloqueado na mailbox com o timeout
  do payload), `SYSCALL_INPUT_KEY_WAIT` (231, aditivo) + `sys_poll_key_wait`; janela do
  shell 30 ticks << budget de reply 128 (a 100 ticks o reply chegava a cliente ja
  retirado: `request timeout svc=5 type=109 cliQ=1`, 19_ns12).
  (J) `fs_tick` NAO tem lugar no caminho de leitura interativo: mesmo throttled a cada
  100 ticks, um passo de writeback encadeava a shell em espera STORAGE (budget 8192) e
  todo reply INPUT nesse janela virava deferred/`unexpected reply` (`waiting_src=1`
  medido no 19_ns13 exatamente entre chars digitados). Removido do read_line; o loop de
  comando e o desktop seguem mantendo o writeback.
  **RODADA 02/11 (parte 2) — CAUSA RAIZ DO #19 ENCONTRADA E CORRIGIDA (run 19_ns25):**
  o stall nao era scheduler, IPC nem input: era **loop infinito no `vsnprintf` do
  runtime de app** (`lang/sdk/app_runtime.c`, `vibe_vsnprintf_internal`). No loop de
  emissao de digitos, o decremento do indice vivia DENTRO do `if (str)`:
  `while (pos > 0 && written + 1u < size) { if (str) { str[written] = tmp[--pos]; } ++written; }`.
  O `vibe_vfprintf_helper` faz DUAS passadas — a primeira com `str == NULL` e
  `size == (size_t)-1` para medir o tamanho — e nessa passada `--pos` nunca executava,
  logo `pos` nunca caia e o laco girava para sempre. Medido: `sched-rip p=13
  eip=100003d6` repetindo com o MESMO eip, e o disassembly mostrando
  `test %esi,%esi; je` pulando `dec %ecx` (esi = str). O app congelava exatamente
  depois de imprimir `socket rx capacity: 4096` (um `%s`) e alcancar
  `event queue depth: %u` — o PRIMEIRO `%u` do bloco de status, que e a linha 1238
  nomeada no relato original do bug. Fix: o indice avanca incondicionalmente, so o
  STORE e condicional. Prova (19_ns25): o status imprime ate o fim —
  `event queue depth: 16`, `listen backlog max: 1`, `state file:
  /runtime/netmgrd-status.txt` — e o app retorna limpo (`app: main returned`,
  `app: atexit done`, `init: task-event terminated pid=13`). O stall original do #19
  ACABOU.
  **RODADA 02/11 (parte 3) — starvation de WAKE POR TIMEOUT (itens 0.13/0.14):** a "shell
  que nao acorda" do host_wait_for_task_exit era escalonamento do wake de TIMEOUT, medido
  em cadeia com a sonda nova (`g_tmo_wake_tick`/`tmo-stale`/`wT1x`): (ns28) `pid=11
  d=304 cls=2` — poll IPC de 30 ticks da shell esperando 304+ ticks por re-despacho;
  (ns30) `pid=11 d=380 cls=9` — o poll de 4 ticks do host_wait; (ns31) `pid=6 d=63
  cls=3` — o proprio host de input sob o retry rate agora boosted. Wake por TIMED_OUT nao
  recebia boost e a classe IPC ainda pagava +12. Fix: wake por timeout de espera COM
  budget (IPC/SUPERVISION/INPUT) recebe o MESMO one-shot slice do wake SIGNALED (sweep
  arma wake_boost_budget; score -128 com budget !=0; consumo no dispatch; depois IPC
  volta ao +12 retry-penalty — nunca supera o host que deve responde-lo; esperas
  infinite deadline=0 nunca saem varridas, nunca caem aqui). Prova ns32: ZERO tmo-stale
  no run, `rk` seguindo cada char digitado ate `netmgrd ` completo. Testemunhas
  evt-pub/evt-rec permanecem orcadas: `evt-pub: pid=13 match=2 cnt0=1` prova que o
  TERMINATED chegou a subscriptions casadas. O marcador final de exit ainda nao virou
  em-gate por WALL-CLOCK do TCG 2-core (sendkey round-trip ~30s; janela do harness
  subida a 1800s — run ns33 em curso). NAO e o #19 original (esse fechou na parte 2);
  barra real continua sendo silicio (0.14 + P0). e o gate so fica verde quando o
  marcador chegar.
  **RODADA 03/11 (parte 5) — CAUSA RAIZ FINAL DO 0.6: contrato de status do
  `SYSCALL_TASK_SNAPSHOT` quebrado (19_ns37, marker chegou):** o handler retornava
  `count` (numero de tarefas) no sucesso, mas TODO o resto do userland usa a
  convencao `!= 0 -> falha` (`lang_task_pid_alive`, `busybox_task_pid_alive`,
  `host_task_pid_alive`; taskmgr consumia o retorno como contagem). Qualquer boot
  com uma unica tarefa retornava >0 -> os helpers de vida liam "falha" ->
  `return 1 (alive)` PERPETUO: o host do app pid=12 esperava `while(alive(13)) yield`
  para sempre mesmo com 13 ja TERMINATED, nunca saaia, e o `shell: command exit
  netmgrd rc=0` esperava um TERMINATED(12) que nunca existia. E a RESTRACAO das
  partes 8-10: o `host-wait: stalled ... alive=1` que levou a concluso
  "o filho esta MESMO vivo" era este artefato de canal de status — os filhos
  MORRIAM sim. Fix: `sys_task_snapshot` retorna 0 no sucesso (canal de status
  limpo; contagem ja vive em `summary->total_tasks`, que e escrita sempre);
  taskmgr adaptado no mesmo commit a ler `total_tasks` (comportamento identico:
  antes usava `count` retornado, que era o mesmo numero). ABI: canal de status
  consertado em syscall existente sem novos numeros nem structs (-1 erro / 0 ok
  e a convencao de todo o resto do kernel); callers kernel-side: nenhum alem da
  tabela. PROVA (19_ns37, gate): `init: task-event terminated pid=12 ... tick=53805`
  (o host AGORA morre apos o filho), `shell: command exit netmgrd rc=0` na serial
  do gate pela PRIMEIRA VEZ na historia da campanha — a cadeia
  launch->exec->wait->exit-marker esta fechada de ponta a ponta; gate seguiu
  rodando os demais comandos (relatorio final abaixo, por run).
  **RODADA 02/11 (parte 4) — retracao honesta do 0.14 + entrega PROVAADA (19_ns34/35):**
  A atribuição "cada sendkey custa ~30s wall (harness/TCG)" estava ERRADA — benchmark
  direto no monitor do QEMU mediu 10 round-trips `hmp()` em 0.01s (conexao nova) e a
  digitacao completa de `netmgrd status` (15 teclas + ret) em 0.9s de wall. O custo real
  de wall e o boot (menu+ready ~220s no perfil do gate) e o tempo de execucao do app sob
  o slowdown do TCG, nao o teclado. Correcao registrada aqui porque o item 0.14 fora
  escrito com a teoria, nao com a medicao (regra da campanha: medir antes de atribuir).
  MEDIDO entao (19_ns34, testemunhas re-armadas so-para-evento-raro): o TERMINATED do
  app e publicado e a ENTREGA na mailbox da subscription da shell esta provada —
  `evt-dlv: ev=13 sub=11 cnt=1 drop=0` (o evento entrou; nenhum drop). O gap atual e
  estritamente posterior a entrega: consumo no receive (`evt-got` impresso só num
  try_receive bem-sucedido) e/ou o retorno do `host_wait_for_task_exit`
  (`host-exit: pid= via=1-event|2-liveness` no userland). As testemunhas desta rodada sao
  todas disparadas por evento RARO (recebimento/retorno) — a armadilha das rodadas
  anteriores era budget queimado por poll vazio ANTES do momento que interessa.
  **NOTA DE METODO (parte 4, 02/11):** o benchmark que "refutou" o custo de wall do
  sendkey estava errado por construgao — medido contra um boot DESKTOP (sem shell), onde
  a fila ps2 do QEMU enche e `sendkey` retorna descartando: 15 teclas em 0.9s eram
  teclas PERDIDAS, nao entregues. No gate real cada tecla leva ~700-1500 ticks de guest
  (ns34/35: digitacao de `netmgrd status` cruza a janela), e o ns35 expirou ANTES de
  submeter o comando (variabilidade de carga do host 2-core entre runs). O gargalo de
  wall e real e e do ambiente (PS/2 + TCG + monitor), como no 0.14 original — o que
  muda e que o 0.14 agora tem a entrega provada (evt-dlv) e as testemunhas de consumo
  so-evento-raro (`evt-got`, `host-exit`) no tree para o proximo run. Licao lavrada: um
  microbenchmark precisa validar que mede o caminho do fenomeno (entrega), nao um
  efeito-colateral (descarte).
  **Estado do gate apos I/J/H (19_ns22, timeout de comando 900s):** o comando e digitado
  COMPLETO, roda, e o status imprime `backend:`/`transport:`/`ownership:`/`datapath
  executor:`/`socket rx capacity:` — o stall original na linha 1238 do #19 ACABOU. O que
  falta agora e a cauda do status: o proximo passo e no caminho NETWORK do app
  (`type=89 src=13 -> host p=8` BLOCKED `waiters=1 wake=6 tmo=0`, `network degraded
  tick=39493`) + os `scheduler: timeout pid=11 class=9` da shell (retry do wait de
  task-exit a 4 ticks que o app demora >budget para responder). Sonda nomeada da proxima
  rodada: replicar no host de network o mesmo par wake-latencia/dreno medido no de input
  (p=8 esta saudavel em wake w6-like, mas o reply type=89 nao sai em <=128 ticks com o
  app ainda emitindo). Marca-se aqui o fim da frente INPUT do #19; a caixa 0.6 continua
  ABERTO na frente NETWORK ate `shell: command exit netmgrd rc=0` aparecer 3x no gate.

  1. `pending_signals` vaza +1 em cada wake por chain-pop: `kernel_waitable_signal` incrementa
     `pending_signals += count` e NUNCA decrementa no caminho pop+complete_wait; o waiter consome
     o sinal fantasma no topo do proximo wait (medido `cliW{pend=3 wake=0}` em run12; `pend=1` no
     fingerprint do timeout). Cada reply real deixa 1 ciclo extra "phantom wake" — unico caminho
     de sinal ainda desbalanceado, primeiro alvo da proxima rodada;
  2. a linha ainda nao submete apesar de `consumed=15/produced=18`; instrumento JA NO TREE:
     trace `rk <valor>` por poll no `read_line` da shell (budget 48), para revelar o que
     `sys_poll_key` entrega de fato (0 phantom / -1 de erro / Enter mapeado errado / chars
     duplicados pelo retry da F). O harness HMP `/tmp/vg/06_repro.py` foi PERDIDO quando o tmpfs
     foi limpo externamente; o repro canonico passa a ser o proprio gate
     `make validate-network-surface` (o report `build/network-surface-validation.md` embute o
     serial), e o probe rk roda nele.
  **Decisao [!] mantenedora:** as magnitudes de (D) `-128` e (F) `128 ticks` sao constantes
  UNICAS de politica (mecanismo fixo, valor e decisao de arquitetura pela AGENTS.md); o default
  atual esta documentado nos comentarios do tree — manter ou reajustar e da mantenedora.
  `validate-network-surface` segue VERMELHO por este item; nao e regressao (A/B do #18 provou
  que antes a shell nem rodava `netmgrd`).

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
- GUI: desktop dinâmico VALIDADO roda no motor `desktop.c` (11k linhas); a reescrita microui (`desktop_new.c`, 497 linhas) nunca foi integrada ao build e `microui.c`/`mu_renderer.c` sairam do runtime em 13/11 (P8) — permanecem na arvore como caminho futuro documentado
- USB: discovery de controladores funciona; runtime mass-storage (BOT)/NVMe tem
  codigo mas sem relatorio de validacao
- POSIX/Compat: camada BSD integrada (libc, pthreads, socket); X11/X-Like em andamento

## Bugs corrigidos

### [x] 17. USB mass-storage: pipeline de enumeracao/execucao MORTO NO MEIO (duas funcoes `unused`, probe_extra sem USB) — CORRIGIDO (20/10)

- **Comportamento observado:** boot de stick USB (BIOS INT13) chegava ao shell
  fallback com `sys_storage_load failed`; log dizia `usb: init complete (1
  controllers, 1 devices)` mas nenhum read/write por USB existia; nota do
  phase6 ("runtime storage unavailable until native backend exists") era
  tomada como verdade ha rodadas.
- **Causa raiz:** `kernel_usb_host_execute_all_pending()` (stepper
  SET_ADDRESS->GET_DESCRIPTOR->SET_CONFIGURATION) e
  `kernel_usb_host_build_probe_snapshots()` marcadas `__attribute__((unused))`
  sem NENHUM caller; `kernel_storage_probe_extra()` tentava so NVMe/AHCI.
  Logo: nenhum snapshot CONFIGURED existia e o backend "usb" (que ja tinha
  BOT/SCSI completo e registrava block device primario) nunca era tentado.
- **Correcao:** build->execute no fim de `kernel_usb_host_init()` (ordem
  importa: executor consome dispatches derivados dos snapshots) + tentativa
  USB no probe_extra antes de NVMe/AHCI. Cache BOT 4->8 setores (limite do
  pool de TDs). Diagnostico explicito nos caminhos de rejeicao.
- **Provas:** stick emulado sem disco ATA: `storage: usb backend ready
  idx=0 total=524288 start=133120 sectors=391168`; fs/AppFS carregados;
  `init: storage smoke ok (read+write+restore)`; desktop host launched;
  validate-phase6-boot-smoke 6/6 e validate-phase6 6/6 (cenario usb
  redefinido honestamente). Falta: xHCI + validacao fisica (P0). EHCI resolvido em ADENDO 49 (COMPLETION_ACTION_PLAN linha 103): transporte nativo EHCI provado q35+ich9-usb-ehci1 sem disco ATA; causa raiz da estourada de QH (flush_qh 11 dwords vs slot 8) corrigida com passo 64B.


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

### [x] 12. e1000 (Intel PRO/1000): driver nunca transmitia — todas as escritas do anel de TX caíam em silêncio — CORRIGIDO (15/10)

- **Comportamento observado:** com `-device e1000` o driver inicializava (`found device`, `mac=52:54:00:12:34:56`, `link=up`) e contava `tx=1`, mas **zero quadros chegavam ao fio** (`-object filter-dump` com 0 frames) e `rx=0`. Nenhum erro visível: as escritas "funcionavam" (não faultavam) e o ring bookkeeping seguia.
- **Investigação (medição, não teoria):** instrumentação progressiva no próprio driver + model source do QEMU. Os contadores de entrada provaram que a preempção/alocador não tinham culpa; um snapshot `tdh/tdt/tdlen` mostrou **TDLEN=0 imediatamente após a escrita**. Leituras via HMP `xp` (host, através do modelo do dispositivo) confirmaram: RDBAL/RDLEN/RCTL/TCTL retidos (= o que o guest escreveu), TDBAL/TDBAH/TDLEN/TDH/TDT todos 0. A desmontagem de `kernel_e1000_init` provou que o guest escrevia os valores certos nos offsets certos (mesmo `%eax`=mmio_base, `movl $0x31d630,0x420(%eax)`).
- **Causa raiz:** os registradores do anel de TX estavam sendo programados em **0x420/0x424/0x428/0x430/0x438 — a janela ALIAS legada (compat 82543)**. No 8254x os endereços canônicos são **0x3800/0x3804/0x3808/0x3810/0x3818**; 0x420+ é só compatibilidade. O modelo do QEMU implementa **apenas** o conjunto canônico — seu próprio `hw/net/e1000_regs.h` declara `E1000_TDBAL_A 0x00420 /* Alias to TDBAL */`, mas o alias **não está nas tabelas `macreg_writeops`/`macreg_readops`**, então cada escrita em 0x420+ é "unknown register": descartada sem erro e lida como 0. O anel de RX nunca sofreu disso porque seu endereço canônico (0x2800) é o que todos usam. (Um primeiro diagnóstico — máscara `set_dlen` `& 0xfff80` — foi **descartado por aritmética**: `1024 & 0xfff80 == 0x400`, o valor era preservado.)
- **Correção aplicada:** programar o anel de TX pelos offsets canônicos (`kernel/drivers/network/e1000.c`), que também são os do datasheet 8254x — correto em silício real, não um workaround. Somado à robada de bring-up (todos na causa raiz, não no sintoma): o handler de IRQ de RX **deixou de consumir os descritores** (antes drenava `rx_cur..RDH` dentro do ISR e comia o próprio pacote, então `kernel_e1000_recv_packet` nunca via descrição completa); contabilidade de TX por **cursor de reclaim + bit DD** em vez de derivar de TDH; reclaim também no tick (à prova de borda TXDW perdida); programas RA(RAL/RAH) a partir do MAC (unicast real, não só `RCTL_UPE`); `kernel_e1000_init()` chamado em `entry.c` (registro em 2 estágios, o `driver_manager_init` executa o probe); self-check orçado que falha honestamente se o `TDLEN` não voltar; smoke de datapath orçado (ARP who-has 10.0.2.2 → espera a reply real).
- **Provas executadas:** `make validate-network-datapath NIC=e1000` **PASS** — relatório `build/network-datapath-validation-e1000.md`: marcadores `found device`/`link=up`/`datapath probe sent`/`datapath ok arp-reply` = yes, e **2 quadros no fio (1 ARP request nosso + 1 ARP reply do gateway)** ⇒ TX (submit+DD reclaim) e RX (fill+consume) provados ponta-a-ponta. Regressões: `make validate-network-datapath` (virtio) PASS; `make validate-phase6-boot-smoke` 6/6. Escopo honesto: isto prova o **driver**; o **serviço de rede** ainda roteia DHCP/responders só pelo virtio (a abstração de NIC segue `[!]` da mantenedora), então o e1000 ainda não serve tráfego ao userland — o próximo passo é plugá-lo nessa abstração.

### [x] 13. rtl8139 (Realtek 8139): nunca transmitia e recebia lixo — dois defeitos silenciosos independentes — CORRIGIDO (15/10)

- **Comportamento observado:** com `-device rtl8139` o driver inicializava (device encontrado, MAC certo, `link=up`) e contava `tx=1`, mas **nenhum quadro saía** (0 frames no `filter-dump`) e, depois do fix de TX, o RX entregava um pacote com **bytes errados** (o probe de ARP rejeitava a reply que comprovadamente chegou no fio).
- **Causa raiz TX (silenciosa, espelho do bug do e1000):** ao enfileirar, o driver escrevia `TSD = len | RTL8139_TX_STATUS_OWN` — **ligando o bit OWN (0x2000)**. Esse bit marca o descritor como *owned by host*: o próprio modelo do QEMU (`rtl8139_transmit_one`: `if (TxStatus[d] & TxHostOwns) return 0;`) se recusa a transmitir, e o hardware sinaliza conclusão **re-ligando** esse mesmo bit. Ou seja: o driver pedia para o chip NÃO transmitir e ainda contava `tx`. Referência de facto (Linux `8139too`, `rtl8139_start_xmit`): `RTL_W32_F(TxStatus0+..., tx_flag | max(len, ETH_ZLEN))` — **sem** OWN — e a conclusão é lida por `TxStatOK (0x8000)`.
- **Causa raiz RX (silenciosa):** o cabeçalho de cada quadro no ring é **uma palavra de 32 bits**: status nos 16 bits BAIXOS e **tamanho (incluindo o FCS de 4 B) nos 16 bits ALTOS** (`8139too`: `rx_size = rx_status >> 16; pkt_size = rx_size - 4`). O driver lia só 16 bits e tirava o tamanho de `status & 0x1FFF` — que é o layout do **descritor de TX**, não do RX — então `rx_len` era lixo e toda cópia/avanço do ponteiro saía errada.
- **Defeitos secundários corrigidos na mesma passada:** (a) o ISR de RX **drenava o ring** (avançava o ponteiro e escrevia CAPR) e comia os pacotes antes do caminho de leitura — mesma classe do bug de RX do e1000; agora só dá ack e reclaim TX; (b) os defines de IMR/ISR estavam **deslocados um bit** (RER/TER/RXOVW errados), então a máscara habilitava fontes trocadas; (c) `CMD_REQ` era na verdade **RxBufEmpty (bit 0, read-only)** — usado para escrever no ChipCmd e agora usado corretamente como teste de ring vazio; (d) o chip **não faz auto-pad**: quadros < 60 B precisam de padding no driver (provado no fio: nosso ARP saiu com `len=60`); (e) `RCR`/`TCR` são protegidos pelo latch **Cfg9346 (0x50)** no silício real — o driver escrevia sem destravar (o QEMU não exige, o hardware sim); (f) o ioctl respondia `link_up=1` **fabricado** — agora lê MediaStatus (0x58, bit 2 invertido); (g) a tabela de match anunciava `0x8169`, que é **outra família** (C+ mode/MMIO) — removido para não claimar suporte falso.
- **Provas executadas:** `make validate-network-datapath NIC=rtl8139` **PASS** (`build/network-datapath-validation-rtl8139.md`): marcadores + **2 quadros no fio** (ARP request nosso, já com padding, + ARP reply do gateway `52:55:0a:00:02:02`), e o guest consumiu a reply (`rtl8139: datapath ok arp-reply len=64 rx=1`). Regressões: `validate-network-datapath` (virtio) PASS, `NIC=e1000` PASS, `validate-phase6-boot-smoke` 6/6. Ver também #12 (e1000) — os dois bugs de "não transmite" são primos: um bit/endereço plausível que o hardware interpreta ao contrário.

### [x] 14. Camada de serviços: `mk_service_launch_task` devolvia pid positivo no SUCESSO, e todos os callers tratam !=0 como falha — launches bem-sucedidos reportavam "launch failed" — CORRIGIDO (16/10)

- **Comportamento observado:** `init: deferred inputsvc launch failed` + consolesvc + networksvc no boot (smp2 e, antes do fix, tambem o caminho no-UP sem record); audio.c no boot imprimia "audio: launch failed" no mesmo caminho. Nenhuma linha `launch: FAIL`/`service: restart FAIL` disparava — porque nao havia falha nenhuma.
- **Causa raiz:** `mk_service_launch_task` (kernel/microkernel/service.c) terminava com `return pid` (pid>0) no sucesso e `mk_service_prepare_task` usava o MESMO canal de retorno para "ja rodando" (`return service->pid`). Mas a convencao de todos os consumidores e 0=sucesso: init confere `sys_service_restart(...) == 0 ? ok : failed`, `mk_audio_*` confere `!= 0`, e os 4 deferred launchers (fs/console/input/network) propagam direto. Resultado: servir a launcher sem record (input/console/network no momento do restart adiado) lancava a task de verdade e reportava FALHA; o restart via worker_record (record pre-existente: fs/audio) devolvia 0 e "parecia" ok — a assimetria fs/audio-ok vs input/console/network-failed e exatamente esta divergencia.
- **Correcao aplicada:** `mk_service_launch_task` normaliza sucesso para 0 (launch fresco e "ja rodando" via prepared>0); `prepare_task` mantem o pid positivo internamente como sinal de "ja lancado" documentado; falhas continuam -1 e agora com traco nomeado e orcado (`launch: FAIL <why> ...`, `service: prepare FAIL ...`, `service: restart FAIL ...`, incluindo os 4 sitios silentes de `mk_launch_bootstrap` e os 3 de `mk_service_restart_worker_record` — contrato: nenhum -1 silencioso). ABI intata (funcoes interno-kernel; syscalls inalteradas).
- **Provas executadas:** boot UP pos-fix: os 5 `init: deferred *svc launch ok` em sequencia (antes: 3 "failed"). Instrumento `heap-sweep` forense (varredura fisica do heap no momento de corrupcao de lista, nomeia o bloco-fonte de overflow por allocpc) entrou junto, sem ruido no UP.

### [x] 15. sys_execve liberava a propria stack do kernel em uso (UAF latente) — CORRIGIDO (16/10)

- **Comportamento (latent bug encontrado na auditoria de face-1/P4, nao reproduzido):** `sys_execve` (kernel/syscall.c) fazia `kernel_free(current->stack)` e so depois atribuia a nova stack — com o Codigo DO PROPRIO SYSCALL EXECUTANDO sobre a stack liberada, e o TSS esp0 deste cpu ainda apontando para o topo velho ate o proximo dispatch. O bloco voltava ao allocator e qualquer frame de IRQ/trap posterior (ou um ring3->ring0 no meio da janela) escrevia memoria libjada que outro dono podia ter recebido — exatamente a assinatura de "escrita-alias viva" que a classe #5 mostra. So e latente porque NENHUM userland compilado chama SYSCALL_EXECVE (grep: unicos 'execve' em arvores vendored de links2/breakpad, nao buildadas).
- **Correcao (liberacao adiada, sem nunca tocar stack em uso):** campo novo `process_t.pending_stack_free` (append do struct, interno-kernel, ABI intata); execve estaciona a stack velha ali (e libera a anterior, se existir) e REPOOE o esp0 imediatamente (`kernel_tss_set_kernel_stack(nova_stack+size)`) para fechar a janela de trap; a liberacao real acontece no proximo switch-away do task (scheduler_schedule_frame) ou no terminate — ambos executam FORA da stack estacionada. `pending_stack_free` e zerado nos dois memcpy-wholesale de process_t (fork child, thread create) para nao duplicar posse. Ordem de locks scheduler->heap ja estabelecida.
- **Provas:** make -Werror verde; UP `validate-phase6-boot-smoke` 6/6 com o fix (relatorio build/); smp2 real-time sem regressao (ver commit). A face-1 da classe #5 segue ABERTA: este UAF e latente (execve nao executado no boot), portanto NAO e a causa das assinaturas de escrita-alias medidas — o proximo passo (trace de kernel_tss_set_kernel_stack + auditoria stale g_current/service->process pos-terminate) continua valdo.

### [x] 16. BAD-FORENSICS resíduo: task ring3 pré-emptada NO MEIO de um syscall int-0x80 disparava o witness — segundo defeito do bug #10 (fechamento do item 0.8) — CORRIGIDO (30/10)

- **Comportamento observado:** no boot desktop, o app ring3 saudavel (pid=12, `syscall: TASK-CREATE pid=12 ... mode=ring3`) disparava `sched-dispatch: BAD-FORENSICS ... regs_in_win=0` em dispatches sucessivos ate esgotar o budget (medido: 6 linhas). A hipotese registrada no item 0.8 (`ctx` salvo dentro da janela da stack do USUARIO) estava ERRADA e foi corrigida pela medicao: a linha real trazia `in_win=1` (o `ctx=0x39267678` estava na stack do KERNEL da propria task) e o campo que falhava era `ebp=0x37ffdf24`.
- **Causa raiz (lida no layout real):** uma task ring3 pode ser trocada FORA do modo usuario tambem — quando a preempcao/block ocorre no meio de um syscall `int $0x80`, o frame salvo e um frame KERNEL (cs=0x8, eip=0x59bc3=`syscall_stub`, esp_dummy na stack do kernel), mas o `ebp` DENTRO dele e o ebp do APP (stack do usuario): `pusha` em `syscall_stub` preserva os registradores do chamador, e a volta pelo `iretd` do stub ira restaurar o modo usuario a partir das palavras extras do frame. O `scheduler_dispatch_forensics` tinha exatamente esse caso no branch de `cs==USER_CS` (fix do bug #10, 15/10) mas o branch `else` exigia `ebp`/`esp_dummy` na janela do kernel — falso positivo sistematico em task ring3 mid-syscall. NAO ha corrupcao: o mesmo boot nao tinha nenhuma outra testemunha ativa (CTX-DRIFT/CTX-MISMATCH/OUTSIDE-OWN-STACK/user-frame mismatch = 0 linhas), e app rodou normal.
- **Correcao aplicada (minima, cirurgica):** no branch kernel do witness, `ebp`/`esp_dummy` passam a ser aceitos tambem na janela do usuario quando `task->runs_in_user_mode=1` (a task E uma task de usuario — decide a janela, nao o cs do frame). Task de kernel (`runs_in_user_mode=0`) permanece ESTRITA — o alvo original do witness (frame corrompido do host storage pid=1, classe #3) continua 100% alcancavel; os checks de `frame_in_window` (ctx na stack propria), `eip_sane` e `cs_sane` seguem valendo para todos. Branch `cs==USER_CS` esta intacto byte-a-byte.
- **Provas (mesma imagem antes/depois, mesmo perfil run-headless-debug):** pre-fix: 6 linhas `BAD-FORENSICS` (budget esgotado) com `ctx=0x39267678 in_win=1 ebp=0x37ffdf24 esp=0x39267698 eip=0x59bc3 cs=0x8`; pos-fix: **0 linhas**, com o MESMO app pid=12 lancado e vivo (tick=7343, `service: first-of-kind req svc=2 type=17 src=12 dst=1 kind=0 user=1`, `storage smoke ok (read+write+restore)`, `desktop host launched`). Gate: `make` -Werror rc=0; `make validate-phase6` 6/6 PASS (build/phase6-validation.md) apos o fix. Item 0.8 FEITO no DEFINITIVE_CLOSURE_PLAN; o 0.7 fica integralmente FEITO (as duas faces do mesmo witness).

### [x] 18. Escalonamento: o anti-inanicao era codigo MORTO e o teto era menor que o span real de tiers — sessao interativa (rescue shell) nunca rodava comando — CORRIGIDO (30/10, item 0.6 modo-i)

- **Comportamento observado (reproduzido com harness closed-loop, `/tmp/vg/06_repro.py`, boot rescue em core2duo/smp2):** o shell (host tier APP=6) ficava **READY eterno atras do host do init (pid=5, tier DESKTOP_USER=0)** que roda `poll(timeout=0)+sys_yield()` na supervise — um concorrente READY permanente. Census medidos: `sel5=147457` subindo rápido enquanto `sel10` (shell) congelava em 4 por 40k ticks; `shell: ready` nunca chegava, nenhum comando digitado era processado. NAO era o wait de 4 ticks da shell (hipotese antiga do plano) nem deadline perdido.
- **Causa raiz (tres defeitos encadeados no `kernel/process/scheduler.c`):**
  1. `scheduler_account_runtime` zerava `task->last_start_tick` a CADA switch-out. Esse campo e o "tick em que a task comecou a rodar" que o termo de envelhecimento le — zerar significava que toda task READY via `last_start_tick==0` e o guard NAO PODIA disparar para exatamente as tasks que ele protegia. O envelhecimento era codigo morto.
  2. Mesmo vivo, o teto era 64 pontos, justificado como "> span de 4 tiers". Falso: o enum de tiers vai de DESKTOP_USER(0) a BACKGROUND(7) = 112 pontos + ate 3 de afinidade. Um host tier-6 (base 96) atras de um task tier-0 ocupado NUNCA ultrapassaria com 64 — o guard era estruturalmente fraco demais.
  3. `find_next` tinha `if (score == 0) break;` — curto-circuito da varredura que e invalido: o score PODE ser negativo, pelos early-returns de bootstrap/wake-boost do `scheduler_task_score` (-40+tier service fresco, -36/-34/-26 user/app fresco, -32/-24 IPC sinalizado), que contornam o clamp do termo de envelhecimento; parar a varredura no primeiro task zero deixaria a CPU com um task merely-zero enquanto um task de score negativo (ex.: app acabado de lancar) espera mais adiante na lista.
- **Correcao aplicada (minima, sem tocar ABI nem semantica de prioridade):** (1) `scheduler_account_runtime` para de zerar o carimbo — o re-dispatch sempre re-carimba (`scheduler_schedule_frame`), e o guard `state!=RUNNING` impede dupla contagem; (2) divisor do envelhecimento 8→4 com teto 128 (> 112+3): boost pleno apos ~512 ticks (~5,1 s) sem rodar; (3) removido o `break` de `score==0`.
- **Provas executadas:** apos o fix, mesmo harness/perfil: `sel10` de 4→**116028** (shell volta a ser despachada), `sel5` congela em 3485 (init para de ganhar toda rodada), `shell: ready` chega e o shell processa comandos. Cmd `js` completo: `host: argv js` → `lang: image read ok` → `lang: chunk0 verify match` → `app: runtime init ok` → `js: NOT IMPLEMENTED (ENOSYS)` (diagnostico honesto, sem stub) → **`shell: command exit js rc=0`** (`/tmp/vg/06_run7`). Gate `make validate-phase6` 6/6 PASS (build/phase6-validation.md). A shell tambem ganhou flush de type-ahead no start (os scancodes resido do menu de boot do stage2 caíam na fila do PS/2 e prefixavam o primeiro comando — o gate viu `netmgrd` chegar como `ssetmgrd`; flush estilo login-shell, so userland, sem tocar ABI), necessario para os gates de sessao chegarem limpos.
- **Escopo honesto:** isto fecha o MODO (i) do item 0.6 (a inanicao que impedia a sessao de nascer). O gate `validate-network-surface` avanca mas fica VERMELHO num modo NOVO e distinto (bug #19: stall do `netmgrd status` no meio do print) — NAO e regressao deste commit: o A/B (scheduler original re-stashado) mostra que antes do fix a shell nem chegava a rodar `netmgrd` (FAIL em `shell: command netmgrd`), ou seja, o gate ja estava vermelho por esta causa-raiz.

### [x] 20. ABI do `sockaddr` divergente entre kernel e userland sob o fallback host-toolchain — o kernel lia o `sa_family` no layout do header DO HOST (Linux: offset 0, 2 bytes) e o userland escreve no layout BSD publicado (sa_len@0, sa_family@1) — todo connect/bind com sockaddr de app era rejeitado — CORRIGIDO (10/11, item 1.4)

- **Comportamento observado:** `dig example.com` falhava com `rc=-4` (connect) embora o cliente
  estivesse correto: o app mandava `sin_family=AF_INET`, `sin_port=htons(53)`, octetos na ordem
  correta. O cliente DNS real (implementado neste mesmo ciclo para o item 1.4) transformou a
  falha silenciosa antiga ("dns lookup not implemented yet") em erro DIAGNOSTICADO — e o
  diagnostico revelou o bug latente de ABI que existia desde a introduction do socket API.
- **Causa raiz (medida, nao presumida):** o kernel compila com `-idirafter headers`, ou seja os
  headers DO HOST ganham de `headers/sys/socket.h`. Sob o fallback `gcc -m32` (sem i686-elf),
  o `struct sockaddr`/`struct sockaddr_in` que o KERNEL ve e o do Linux host (`sa_family` 16-bit
  em offset 0, SEM sa_len) — enquanto todo o userland compila contra o `headers/sys/socket.h`
  do repo (BSD: `sa_len` 8-bit em 0, `sa_family` 8-bit em 1). Witness pareado no mesmo boot:
  app `dig: conn-try h=1 len=16 fam=2` vs kernel `net-connect: h=1 len=16 fam=528` — o kernel
  lia os bytes `[16,2]` como u16 LE (0x0210=528) e rejeitava `!= AF_INET` em TODOS os
  connect/bind. Os accessors de porto/endereco (`mk_network_sockaddr_port/ipv4`) ja liam RAW
  BYTES nos offsets BSD e por isso DHCP/ping funcionavam — so os derefs de struct campo
  (`->sa_family`) estavam viciados.
- **Correcao:** novo accessor `mk_network_sockaddr_family()` (leitura raw do byte 1, layout
  BSD publicado) substituindo os 3 derefs `->sa_family` (bind, connect x2); o preenchimento do
  `src_addr` no recvfrom agora escreve bytes crus no layout publicado (sa_len=16, family, port,
  addr) em vez de campos do struct host. Nenhuma mudanca de syscall/struct publicada: a ABI
  continua a de `headers/sys/socket.h` + `headers/netinet/in.h`; o que mudou e que o kernel
  agora obedece a ela em vez do header do host.
- **Correcoes-irmao no mesmo ciclo (mesma superfice):** `dig`/`host` criavam o socket com
  `protocol=0` e o kernel classifica UDP por `protocol==IPPROTO_UDP(17)` — socket sem protocolo
  nao entrava em NENHUM branch de send/recv (silencioso; agora o cliente usa IPPROTO_UDP e o
  client retorna codigos distintos -3/-4/-5 para socket/connect/send); o `netctl dhcp` passava
  `argc=2` para `netctl_command_connect` que exige `>=3` para ler `argv[2]` — imprimia usage em
  vez de conectar (witness `app: entry argc=2 1=dhcp` provou que o argv chegava intacto; o bug
  era o array literal mal contado). E o `Build.ported.mk` nao dependia dos headers compartilhados
  (`applications/ported/include/*.h`) — editar header deixava `.app` stale (corrigido no padrao
  de regra e no stamp do Makefile).
- **Provas executadas:** `make` -Werror verde; `make test-host` 43/43 (fdopen 20 + DNS 23, incl.
  parse de resposta com compressao, loop de pointer rejeitado, NXDOMAIN); `make
  validate-phase6-boot-smoke` 6/6; probe focado `tools/probe_dns.py` → **`dig: dns-ok
  query=example.com server=10.0.2.3 addr=104.20.23.154`** (build/dns-probe-clean.md) — A record
  REAL resolvido via o nameserver do lease DHCP, com witness wire `udp-tx: OK dport=53
  sport=15348 dst=0xa000203` + `udp-rx-any: dport=15348 len=69` capturados na fase de
  instrumentacao (witnesses temporarias removidas da arvore apos o fechamento).
- **Escopo honesto:** o `validate-network-surface` completo (24 comandos) segue preso ao wall de
  digitacao TCG do host 2-core (itens 0.15/0.16) — a prova do DNS usa o probe focado; os
  marcadores novos do gate (`query=example.com server=10.0.2.3`) valem quando o gate rodar
  completo. Consumer residual do layout host: nenhum deref de campo sockaddr restante em
  network.c (auditado: 0 ocorrencias de `->sa_family`/`->sin_*`).

### [x] 21. IOAPIC: toda linha ISA 8-15 (e 3-7 sob flags SMP) caia em RTEs descasados — `ioapic_isa_entry` usava offset fixo +2 em vez do mapeamento PC-AT — CORRIGIDO (10/11, exposto ao ligar o RX-IRQ do virtio-net, linha 70)

- **Comportamento observado:** ao armeçar o INTx do virtio-net (linha 11) com TODA a cadeia correta — handler registrado, PIRQRC do PIIX roteado (pirq3 -> irq11), ELCR em level, IMR/RTE unmasked — o vetor 0x2B nunca chegava (`rx-irq` ausente, "spurious" ausente: dispatch nunca chamado), enquanto a rota estava objetivamente programada (dump `irq 11 -> entry 13`).
- **Causa raiz:** `ioapic_isa_entry()` = `irq + 2` para TODO IRQ. O wiring PC-AT (e o modelo QEMU pc) é: ISA IRQ0 → entrada 2, cascata IRQ2 → entrada 0, e TODO o resto identidade (1→1, 8-15→8-15). Com o offset +2, apenas o timer (0→2) caía certo por sorte; IRQ1 teclado ia para a entrada 3 (=ISA IRQ3), IRQ11 para 13, IRQ12 para 14 — entradas que NENHUM fio aciona. Consequências silenciosas históricas: em boots IOAPIC (flags SMP) teclado/mouse só sobreviviam porque o servico de input POLA a 8042; a afinidade "IRQ1/12 no AP" (adendo 47, linha 115) reescrevia entradas ERRADAS — o PASS observado vinha do polling, não da rota; qualquer device PCI em linha 8-15 (virtio-net, USB UHCI em irq11, AHCI em irq10 no T400!) estaria morto. No T61/T400 AHCI é justamente linha 10 → este bug teria sabotado a campanha de storage fisico.
- **Correcao aplicada:** `ioapic_isa_entry()` implementa o mapeamento PC-AT (0↔2, resto identidade), com comentario registrando o porque e a evidencia. Um unico chokepoint — todos os chamadores (`kernel_irq_setup`, `ioapic_mask/unmask`, `set_irq_redirect` da afinidade smp.c) passam por ele; nenhum codigo usava numero cru de RTE alem do dump de diagnostico.
- **Provas executadas (mesma imagem, mesma janela):** serial do boot com flag SMP agora imprime `ioapic: irq 1 -> entry 1 ... vector 33`, `irq 11 -> entry 11 ... vector 43`; `network: virtio-net rx-irq #1 (RX delivered by interrupt)` chega ANTES do OFFER do DHCP e a cadeia OFFER→REQUEST→ACK→bound completa (antes, sem EOI, o DHCP congelava no OFFER — segundo bug, corrigido junto: handler agora da EOI na linha em toda entrega, convengao do stub generico que nao ack); `make validate-phase6-boot-smoke` 6/6; `make validate-network-datapath` PASS; `make validate-smp`: fallback PASS, smp2/smp4 FAIL apenas no marcador `userland.app: shell start` (baseline #5 inalterada — todos os marcadores de bring-up seguem passando, logo o novo roteamento nao introduziu regressao).
- **Licao de metodo:** "medir a rota antes de confiar no armamento" — cada camada (handler, roteador PCI, ELCR, IMR/RTE) foi verificada individualmente; o bug estava numa camada que NUNCA tinha sido exercitada porque nenhum device funcionava por interrupgao sob IOAPIC.


### [x] 22. Audio: selecao de backend + launch do host ficaram INALCANCIVEIS no boot (regressao do merge externo "Som e net") — CORRIGIDO (11/11)

- **Comportamento observado:** TODOS os gates `validate-audio-*` (hda-smoke/output/playback/startup, audio-stack AC97) vermelhos com `audio: soft` permanente e a lista de marcadores faltando incluindo `desktop: open-new` — e o A/B (stash completo das mudancas da rodada) provou IDENTICO no HEAD limpo: regressao pre-existente, nao da rodada. Na serial, o progresso parava em `audio: soft / uaudio-driver / usb-snap`; a linha `audio: azalia?`/`audio: compat ok` (selecao de hardware) NUNCA aparecia.
- **Causa raiz:** o merge externo moveu a selecao de backend + `mk_service_launch_task(AUDIO)` para um *deferred launcher* registrado via `mk_service_register_deferred_launcher`, e o trigger pretendido era o `sys_service_restart(MK_SERVICE_AUDIO)` do init (`init: deferred audiosvc launch ok`). Mas o mesmo `mk_audio_service_init` REGISTRA o record local-handler ANTES do restart; `mk_service_restart_worker_record` da early-return `0` para record sem worker (`process==0 && pid==0 && launch_flags==0`) — o launcher so era invocado quando o record NAO existia (`mk_service_ensure`/`restart` branch `service==0`). Record existia sempre → launcher virou codigo morto → nenhum audio host, nenhum backend de hardware, "launch ok" era uma mentira de retorno-zero. No era anterior (a5cdffd71) a selecao+launch eram inline em `mk_audio_service_init` no phase [17] — e assim estavam desde sempre verdes.
- **Correcao aplicada:** `mk_audio_service_init` voltou a executar o launch na fase [17] (chama `mk_audio_service_launch_deferred()` inline, com comentario registrando o bypass do record e a evidencia), mantendo o launcher registrado como caminho REAL de restart (o record passa a carregar launch_flags apos o launch_task). Zero mudanca de ABI; userland intacto.
- **Provas executadas:** serial do gate hda agora mostra a cadeia completa: `audio: azalia select begin pci=8086:2668 irq=11 base=febd0000` → `azalia reset ok` → `ring init ok` → `codec=0 vendor=1af40022` → `widget probe ok` → `playback path usable` → `input streams=4 capture path usable` → `azalia select done config=00:04.0 irq11 flags=1e` → `audio: deferred launch backend=compat-azalia` → `audiosvc: task added/online` (host vive); `make validate-audio-hda-capture` (novo gate deterministico) PASS: build/audio-hda-capture-validation.md; `validate-phase6-boot-smoke` 6/6.

### [x] 23. sys_fork de task ring3 copiava ~84MB dentro do contexto do kernel (aritmética de "stack used" contra o TOPO da stack) — freeze total do guest no primeiro ctrl-x do desktop — CORRIGIDO (11/11)

- **Comportamento observado:** no boot com audio host vivo (#22 corrigido), o `desktop: key 24` era traco, e NENHUM witness de janela vinha depois (`open-new`/`open-fail`/`alloc-*` ausentes) — e o SERIAL INTEIRO CALAVA: os `sched-census` periodicos (a cada ~1025 ticks) pararam; medido em sonda dedicada: 0 bytes em 30s apos poke, enquanto antes do fork o cenario rodava. No probe anterior (AC97, mesmo fork) o wedge era identico. Ou seja: nao era "shell lenta" nem wall de digitacao — o KERNEL parava.
- **Causa raiz:** o merge externo duplicou o tratamento de fork user-mode em `sys_fork` e o bloco extra calculava `parent_used = user_stack_top - user_stack_top / 4u` — diferenca de um ENDERECO com 1/4 de si mesmo (~453MB para 0x24xxxxxx, clampado a arena/4) — e fazia `memcpy` desse volume a partir de `top - parent_used` (memoria provavel-mapeada-nao/lixo) DENTRO do syscall, antes do bloco honesto (copia top-8KB). 84MB de memcpy em contexto de kernel sob TCG = main-loop morto por minutos; e era REDUNDANTE: o bloco de baixo ja fazia a copia limitada + frame do filho.
- **Correcao aplicada:** bloco removido (comentario registra a aritmetica errada e a sonda que mediu o wedge); mantido apenas o choice de `context_size`. O caminho real do fork nao mudou de contrato (abi-additive: nenhuma struct/formato alterado; apenas o overcopy ilegal removido).
- **Provas executadas:** mesma sonda, mesma imagem-pos-fix: `fork: child_pid=15 eip=14093b35 cs=1b user_esp=... runs_user=1` na serial; o guest segue produzindo witnesses depois do ctrl-x (`growth-after-poke: 10011` bytes vs `0` antes); o comando digitado chega ao ksh (filho executa `audiosvc`: `init: task-event launched pid=16 prio=desktop class=audio` + terminate). A falha REMANESCENTE dos gates interativos e outra, catalogada como #24.

### [~] 24. Desktop terminal: comando DIGITADO passou a ir ao ksh (fork filho do desktop), e o contrato de completion-witness dos harnesses interativos quebrou — PARCIAL (11/11; roteamento+witnesses construidos, e2e bloqueado pelo #27)

- **Comportamento observado:** apos #22+#23 corrigidos, `make validate-audio-hda-smoke` falha EXCLUSIVAMENTE no canal de digitacao: teclas chegam ao desktop (`desktop: key 97/117/...` com eco), ksh roda o comando (pid 16 launched/terminated), mas `shell: command audiosvc` / `audiosvc: export ok` nunca caem na serial. Os 8 gates interativos antigos (`audiosvc export-state`, `soundctl status/record/play`) dependem desses marcadores.
- **Hipotese/Causa provavel:** (a) os witnesses `shell: command ` (userland/modules/shell.c:408) e `terminal: command start/done` (userland/applications/terminal.c) existem apenas no caminho LEGADO do terminal (busybox in-process, `terminal_execute_command`); com ksh vivo (`ksh_pid>=0`) o input digitado e roteado ao stdin do filho (`desktop.c` ~10653-10665) e NENHUM witness de dispatch sai; (b) o app executado via execve do ksh (pid 16) terminou sem emitir `audiosvc: export ok` via write_debug — argv/exit do exec do ksh ainda nao auditado (a app roda mas pode estar perdendo argv). As duas faces userland (witness de roteamento + exec-argv) precisam de um dono.
- **Proxima sonda nomeada:** imprimir no roteamento de input do desktop (`terminal_send_input` quando `ksh_pid>=0`) um witness curto `terminal: typed ksh rc-pid=<n>` por linha; e auditar o execve do ksh para o app `audiosvc` (`host: argv` ausente para pid16 sugere exec direto sem o bootstrap host). Acao: campanha P8 (userland/ksh) — enquanto isso, as provas de audio/capture usam o gate deterministico de boot `--require-capture-smoke` (nao digita nada), que e o canalo honesto atual.
- **Impacto:** nenhum no kernel; os gates `validate-audio-hda-*`/`validate-audio-stack*` interativos seguem vermelhos APENOS neste canal (baseline ja vermelha antes de hoje por #22/#23 — hoje restou so esta causa, um adiantamento real).
- **ATUALIZADO 11/11 (tarde, mesma rodada):** face (a) FECHADA — witnesses proprios da era ksh criados e medidos: `ksh: exec <cmd>`/`ksh: exit <cmd> rc` no wrapper `compat/src/posix/process.c::execve` (que agora ROTEIA pelo caminho provado do busybox: subscribe TERMINATED(class via argv[0] como busybox faz) + `sys_launch_app_argv` + wait; execve direto NUNCA entregou a ABI `vibe_app_entry(ctx,argc,argv)` e matava apps em silencia), gate `search()` do pdksh com passthrough de nome puro (+witness `ksh: search-fallback`), harness de audio com pacing POR `desktop: key` (crescimento de serial provou ser falso-ack: fundo mantem serial crescendo enquanto PS/2 dropa) e caps 120 s/90 s. Face (b) foi rebatizada #26 (fork resume) e CORREGIDA com provas. Restante do #24 = e2e atras do #27 (startup do proprio pdksh). libcompat permaneceu self-contained (ported apps linkam sem syscalls.o — wrappers raw locais).

### [x] 25. Service-host receive: lost-wake prende o host BLOCKED com fila profunda (profundidade >0, pend=0, waiters=1), e o cliente degrada em storm de retry/unexpected-reply que derruba o INPUT do desktop — DEFESA APLICADA, causa fina ABERTA (11/11, exposto quando o host de audio voltou a existir via #22)

- **Comportamento observado:** no gate hda, o desktop (pid14) emite `svc=5 type=67` (audio GET_INFO) por frame; a witness de timeout mostrou o host audiosvc BLOCKED com a fila dele contendo o request: `service: request timeout svc=5 type=67 src=14 dst=8 host-state=2 hostQ=1 hostW{pend=0 wake=305 tmo=0 waiters=1} cliQ=0` — wake consumido sem mensagem entregue (mesma familia do #19, agora no LADO HOST da fila ipc do proprio task). O efeito em cadeia medido: retries do cliente (`service: retry scheduled type=5 attempt=2/10`), respostas tardias viram `service: unexpected reply src=8 dst=14 waiting_src=1` em storm, e o desktop acaba fazendo `desktop: input reset` com `input-diag ... consumed=0` — o canal de digitacao MORRE junto.
- **Defesa aplicada (nao mascara — re-poll limitado, padrao ja usado no repo para datapaths):** `mk_service_worker_step` (kernel/microkernel/service.c:713) e `sys_service_receive` (kernel/syscall.c) passaram de wait infinito para `ipc_receive_wait_timeout(..., MK_SERVICE_HOST_REPOLL_TICKS=50)`; o loop do host trata timeout como "sem mensagem" e re-peeka a fila — uma mensagem encalhada e drenada em <=0.5 s independentemente do sinal perdido. O caminho rapido continua orientado a sinal (medido: sem diferenca nos gates).
- **Provas:** `make validate-audio-hda-capture` PASS com a defesa (build/audio-hda-capture-validation.md); `validate-phase6-boot-smoke` 6/6.
- **Causa fina ABERTA:** o par enqueue/signal da fila ipc ainda pode perder o ultimo wake sob esta carga (janela exata nao capturada — `kernel_waitable_signal` ja carrega as correcoes do #19; o proximo passo nomeado e testemunhar o estado do waiter DENTRO de ipc_send quando a fila sai de 0->1, para distingur signal-perdido de wakeup-consumido-prematuro). Registrado como #27-adjacente na campanha de terminal.

### [x] 26. sys_fork user-mode: o filho retomava com ESP ancorado no TOPO da stack e sem restaurar os 5 empurrados — `ret` saltava para lixo de dados (PF err=5 em enderecos de dados, cr2=0/0x13d) e o ksh do terminal morria antes de ler qualquer comando — CORRIGIDO (11/11)

- **Comportamento observado:** apos #23, o ctrl-x abriu o terminal e o fork do ksh saiu vivo (`fork: child_pid=16 eip=14093be5 user_esp=25ffe07c`), mas o filho crashava imediatamente: `exception: Page Fault user pid=16 err=5 eip=<endereco-de-DADO>` — o terminal nao tinha shell, entao NENHUM comando digitado era executado (raiz fina por tras do #24).
- **Causa raiz (3 camadas, todas medidas):** (1) o bloco duplicado do merge externo ainda calculava o ponto de partida do filho como `top - 8KB + 20` — a pilha user cresce para BAIXO, o regio viva e `[ESP, top]`; o `ret` do filho popava lixo de frame antigo (medido: retslot=142628c0 = ponteiro de dADO, filho executando-o e lendo cr2=0). (2) A ancora do kernel leu `c`(edx) em vez de `b`(ecx) — o ABI real do int-0x80 e (a=ebx,b=ecx,c=edx,d=esi,e=edi) (`kernel_asm/isr.asm:189-195`), entao o ESP nunca chegou. (3) O wrapper `fork()` usava asm inline NAO-naked — o prologo do proprio gcc mudava o numero de pushes e o `[esp+20]` previsto virava outro campo.
- **Correcao aplicada:** `compat/src/posix/process.c::fork()` virou stub naked com exatamente 5 pushes e ESP do trap entregue em ECX; `fork_child_entry` restaura os 5 registradores e SO entao faz `ret`; `sys_fork` (kernel/syscall.c) espelha o regio viva `[parent_esp, top)` (bounce via heap, teto 64 KB, witness `fork: src=... mirror=... dst=...`) e ancora em `b` com fallback `process_saved_esp`. Fallback preservado para imagens sem arena (kernel ELF) — nenhuma chamada existente mudou de contrato (a ABI syscall e apenas aditiva).
- **Provas executadas (gate hda, mesma imagem):** `fork: esp=37ffe074 retslot=1402901e live=8076` + `fork: src=1402901e mirror=1402901e` + `child_pid=16` e o filho VIVE e executa pdksh de verdade — witness nova `ksh: warning: won't have full job control...` na serial (o ksh roda; o crash remanescente e startup do PROPRIO pdksh, catalogado em #27); `make validate-phase6-boot-smoke` 6/6; `make test-host` ALL PASS.

### [ ] 27. pdksh (terminal desktop) nao completa startup neste ambiente: crash/abort em caminhos que assumem environ/termcap/arquivos do SO — ABERTO (11/11), bloqueia o e2e do #24

- **Comportamento medido (3 pontos de falha apos o #26 consertar o fork, todos `err=5` lendo offset ~0x12c/0x13d de base pequena — ponteiro-int deixo struct/union):**
  1. `set_editmode` recebe `ed=0x13d` (inteiro lido como `char*` — tbl union sem string) e o `strrchr` de `edit.c:227` anda para fora da pagina → guard explicito adicionado com witness `ksh: editmode refused (bad ptr)` (o crash parou de matar o filho, a origem do valor continua errada).
  2. Apos os 10 resets `ksh_getopt_reset(&builtin_opt,0)` adicionados (o port perdeu os resets que o upstream faz por builtin — `builtin_opt` stale entrava no caminho de parsing com `go->p/info` lixo: crash `ksh_getopt+0x1c` lendo `argv[optind-1]+gibbage`), o filho avanca mais e...
  3. ...crasheia no `strrchr`+0x12 (mesma classe, outro chamada — candidates: `is_restricted(argv[0])`/`str_val(global("SHELL"))` em `main.c:429`, com `environ = 0` fixo em `compat/src/posix/process.c:314` e sem `/etc/passwd|termcap`).
- **Hipotese unificadora:** o pdksh linkada no desktop espera um ambiente POSIX completo (environ com SHELL/TERM, /etc/*, get/setpwuid, termcap); no VibeOS `environ` e NULL-hardcoded e nenhum desses arquivos existe — a inicializacao usa ponteiros pequenos de defaults nao-preenchidos. E uma campanha de port (P8), nao um one-liner.
- **ATUALIZADO 12/11 (rodada #27 parte 1) — 3 das causas raiz CORRIGIDAS e o pdksh agora COMPLETA startup e executa comandos:**
  1. **`argv` do pai** — o ksh_main recebia `char *argv[]` do FRAME DO PAI (regiao que o pai reusa imediatamente): medido `argv[0]==1` (dado novo do pai, nao "ksh"). FIX: child branch de `terminal_init_ksh` (userland/applications/terminal.c) agora toca SOMENTE objetos estaticos/globais — fds via `g_ksh_child_fd_in/out` estaticos (setados pre-fork, consumidos nas primeiras instrucoes do filho) e `g_ksh_argv` estatico. Prova: `ksh: child enter` / `ksh: child fds wired` / `sh: No controlling tty (open /dev/tty: No such file or directory)` / `ksh: warning: won't have full job control` — STARTUP COMPLETO do pdksh, primeira vez na historia do repo.
  2. **`ksh_getopt` resets** — 10 sites `ksh_getopt(wp,&builtin_opt)` sem o `ksh_getopt_reset` upstream (stale `go->p/info` entre builtins) — corrigidos (crash `ksh_getopt+0x1c` com `argv[optind-1]+lixo`).
  3. **janela do NETO** — o ksh bifurca de novo para executar cada comando (j_newjob); o neto herdava `user_stack_top = janela arena/2` que nao casa com nenhuma constante STACK_TOP e o `sys_fork` o retomava no FRAME KERNEL ESPELHADO (`cs=25ffddc4`, eip=lixo) — o fantasma ring0 `#UD kernel-mode eip=25ffdf29` que HALTAVA o guest. FIX: `fork_child_user_window()` em kernel/syscall.c mapeia tier1 (arena top -> arena/2) e tier2 (arena/2 -> arena/4) por arena (desktop/main/boot/compat 20260325); janela DESCONHECIDA = fork falha com -1 e witness `fork: no child window for top=...` — NUNCA mais retomar user task em frame kernel espelhado. Prova: neto `fork: child_pid=21 ... user_esp=1cffddc4` (tier2) e a cadeia inteira executa: `ksh: search-fallback <cmd>` → neto → **`ksh: exec <cmd>`** (wrapper execve roteando por LAUNCH_APP) — E2E DO #24 VISIVEL na serial pela primeira vez.
  4. **EBP chain hack RETIRADO** — o desktop compila com `-Os` SEM frame pointers (trap-ebp = 0xffffffff lixo); a realocacao de cadeia EBP era morte e a assinatura restante nao dependia dela.
- **12/11 parte 3 (instrumentos instalados e resultado):** probes de ESP-DRIFT e FRAME-DRIFT (ss/esp/eflags/cs/eip snapshot antes/depois do handler, offsets +60..+76) rodaram em TODA syscall nas reproducoes e dispararam ZERO vezes — a teoria de iretd deslocado/handler desbalanceado esta REFUTADA por medicao; probes removidos do tree apos o resultado (negativos documentados aqui). witness `fork: child_pid stack_size=16384` permanente no tree. GP reproduz DETERMINISTICAMENTE 6/6 quando o LAUNCH falha (`ksh: exec <cmd>` -> GP) e nunca foi visto no caminho feliz. Assinatura final: o salto ring0->janela-do-neto vem de um `call *ptr` de kernel com valor user — proxima sonda nomeada: capturar o `kud-bt`/`ud-bt` completo DO GP (o mesmo caminho que ja capturou o #UD) para nomear o caller exato do salto.
- **RESTANTE (medido 12/11 parte 2, sondas renomeadas):** 3 assinaturas em 3 corridas com ` true` (espaco sacrificial): (1) `General Protection err=ff50 eip=1cffdd66 cs=8` apos `ksh: exec <cmd>` quando o LAUNCH falha — eip EXATAMENTE user_esp-0x5e nas 3 ocorrencias (frame DESLOCADO, nao aleatorio); (2) `#UD kernel-mode eip=147082fd cs=8` (salto para HEAP) sem neto na corrida; (3) corrida LIMPA sem ksh (`input-diag scancodes=16 produced=7 consumed=7 drops=0` — o ctrl-x nem abriu a janela). MEDICAO NOVA (commit 865a29eb0): witness `fork: child_pid stack_size=` — filho E neto tem kernel stack 16384 → TEORIA DE ESTOURO DE STACK NO HANDLER DE LAUNCH REFUTADA. A variancia entre corridas e do canal de digitacao (1a tecla pos-open perdida recorrentemente; `drops=0` no kernel — a perda e no caminho QEMU/ps2 sob carga, familia 0.15). Proximas sondas nomeadas: (a) imprimir `ud-bt` COMPLETO (8 words) em cada #UD/GP do neto para nomear o chamador do salto-deslocado; (b) LAUNCH de app REAL via caminho INJETADO (sem teclado — ex. `restart-smoke`/queue de comando do desktop) para separar GP-na-falha-de-launch de GP-no-caminho-feliz; (c) medir a janela da 1a tecla perdida (sendkey isolado com witness `desktop: key` antes/depois do open-new). Alem disso: `str_val(global("SHELL"))`/environ sintetico continua pendente (os ptrs 0x1/0x3 do EFAULT-str).
- **12/11 parte 4 (instrumento panic-bt INSTALADO e medições):** o scan de backtrace foi instalado em `kernel/panic.c::kernel_panic` (toda morte de kernel passa la) apos uma tentativa em idt.c produzir objecto sem as strings — que se revelou ARTEFATO STALE do build incremental (break-test: idt.c ESTA no grafo; apagar kernel.elf/bin/boot.img e reconstruir restaurou ud-bt/panic-bt nas strings do kernel.bin). Reproducao com cadeia reconstruida: `ksh: exec <cmd>` (write_debug do neto OK, 1 syscall completo) → `GP err=ff50 eip=1cffdd66 cs=8` → `panic-bt: sp=399df9cc` — ESP DENTRO da stack kernel do neto (4.4KB abaixo do esp0 399e2038) mas ZERO palavras de .text nas 96 primeiras abaixo de ESP — e o `save-ledger` do panic mostra os frames gravados da task vitima pid=25 em `f=399e30e0/399e31fc`, ACIMA do esp0 — context/trap-frame FORA dos limites da stack alocada: a assinatura do GP aponta agora para a familia classe #5 (contexto/esp0), NAO para um call *ptr de LAUNCH. err=ff50 decodificado: selector LDT index 0x1FE2 (fora da GDT) = frame corrompido lido como selector de iret.
- **12/11 parte 4b (sinal colateral, tambem em #27):** `syscall: EFAULT-str ptr=0x3 pid=<neto>` aparece 3/3 reproducoes antes do `ksh: exec` — algum call de string no startup do neto com ptr=3 (candidato: scriptexec/ENV open com ponteiro invalido herdado do espelho); a sintese de environ (PATH/HOME/SHELL) ainda pendente.
- **12/11 parte 5 — CADEIA CAUSAL FECHADA E CORRIGIDA (kernel-hardening + testemunhos):** a sonda `thread-created` nomeou o emissor real do GP: `thread-created: pid=17 by=16 entry=25ffdf14 stack=4096` — o filho ksh (pid16) chamava SYSCALL_THREAD_CREATE com ENTRY = endereco dentro da sua propria janela de stack, e `sys_thread_create` inicializava SEMPRE a thread via `process_setup_initial_context_arg` (RING0) — mint de privilegio: toda thread criada de ring3 executava dados de stack de usuario em CPL0. Isso fecha os dois lados do GP perseguido: (a) a vitima `size=4096 kind=0 class=2 user=0` do panic-task = mint de thread lixo; (b) o iret-exec de janela = a propria thread executando o window. A guarda por-arena NAO bastou (entry dentro da arena passava); a regra correta e a da classe #4 do `sys_task_create` — **ring3 jamais munta ring0** — aplicada incondicional: `THREAD-CREATE-PRIV-REJECT entry=... caller_pid=...` (7 rejeicoes medidas). O chamador corrompido (ksh fazendo thread_create com lixo) tem causa primaria PROPRIA: **fork de espaco de enderecos COMPARTILHADO** — filho ksh e desktop-pai escrevem nos mesmos globals (heap/stdio/g_terms) ao mesmo tempo; a corrupcao produz os ptrs lixo (EFAULT-str 0x1/0x3, thread_create de stack) e a guarda apenas impede que virem privilegio. Pos-guarda medido: `PANIC? False | GP? False`, eventos fluem apos rejeicoes, phase6 6/6, test-host ALL PASS, hda-capture PASS, make 0 warnings.
- **ENDURECIMENTOS SECUNDARIOS (parte 5, mesma familia):** `MK_LAUNCH_STACK_SIZE_DEFAULT` 4096→16384 (uma pagina de kernel stack nao hospeda o caminho launch+printfs; witness orcado `task-created` adicionado no local) e worker de retry de servico 4096→16384 (roda a cadeia deferred-de-audio completa numa pagina). ABI-inertes: apenas tamanho de alocacao interna.
- **ABERTOS COM NOME (parte 6):** (i) CAUSA PRIMARIA do terminal: lancar o ksh como TASK PROPRIA via host-launch (estilo audiosvc/netmgrd, que ja funciona) OU dar COW ao fork — decisao de DESIGN da mantenedora (host-launch de app `terminal-ksh` vs `vfork()`+exec obrigatorios); enquanto isso o ksh digitado permanece inutilizavel, mas o kernel nao morre mais por isso. (ii) threads ring3 legitimas exigiriam pilha user por-task (craft usa sys_task_create — inafetado). (iii) environ sintetico (ptrs EFAULT 0x1/0x3) — provavel sintoma de (i), nao causa independente.

- **Impacto:** e2e do typed-command VISIVEL (falta o GP do neto) (falta o GP do neto); nada no kernel-path dos outros gates: `validate-audio-hda-capture` PASS, `validate-phase6-boot-smoke` 6/6, `test-host` ALL PASS apos os 3 fixes.

### [x] 28. `/runtime` nunca era criada pelo `fs_init` — todo `write` de estado de runtime (netmgrd status, taskmgr, applets do desktop, wget) falhava silenciosamente com rc=-1 — CORRIGIDO (13/11)

- **Comportamento observado:** `netmgrd: failed to write /runtime/netmgrd-status.txt` em traces antigos do gate e, medido nesta rodada, `wget: write-fail out=/runtime/wget-proof.txt rc=-1 bytes=205` (build/wget-probe.md, 13/11) — o HTTP baixou 205 bytes reais pela stack propria (`network: tcp connected`, `status=200`), mas o arquivo nunca aparecia.
- **Causa raiz:** `fs_init()` (userland/modules/fs.c) cria `/bin /usr/bin /compat /config /tmp /dev ...` mas NAO cria `/runtime`; `fs_create(path,0)` dentro de `fs_write_bytes` exige o pai existente, e a resolucao de `/runtime/<arquivo>` falhava ANTES de qualquer alocao de node — ou seja, a convencao inteira de "estado de runtime em /runtime" era escrita contra um diretorio inexistente em TODAS as copias do userland, e o `-1` do caller era tratado como falha opaca.
- **Correcao aplicada:** `(void)fs_create("/runtime", 1);` em `fs_init`, no mesmo padrao dos demais diretorios fixos (fs.c:2030). Uma linha de dados, ABI-integra, zero risco para quem ja escrevia em /tmp ou /config (mesma semantica de pai).
- **Provas executadas:** apos rebuild, mesmo probe: `wget: saved url=http://10.0.2.2:8099/ out=/runtime/wget-proof.txt bytes=205 status=200` + `shell: command exit wget rc=0` (serial `/tmp/vibe-wget-probe-zy7i4536`, 13/11) e o gate final `tools/probe_wget.py` em `build/wget-probe-runtime.md`: `wget: readback out=/runtime/wget-proof.txt rc=0 size=205 match=1` + http.server `"GET / HTTP/1.0" 200`. Efeito colateral corrigido de graca: o espelho `/runtime/resolv.conf` do netmgrd e o status-txt agora tambem passam a existir no pool do host.

### [!] 29. Pools de filesystem separados por binario userland: a arvore onde o app/host escreve NAO e a mesma que o shell interativo le — arquivos novos criados por apps sao invisiveis ao `cat` do shell na mesma sessao — ABERTO (13/11), decisao de arquitetura da mantenedora

- **Comportamento medido (13/11, rescue shell, mesma sessao, mesma imagem):** `wget http://... /runtime/wget-proof.txt` → `wget: saved ... bytes=205 status=200` (write rc=0 no pool do host) e imediatamente `cat /runtime/wget-proof.txt` → `error: file not found` (serial `/tmp/vibe-wget-probe-zy7i4536`). NAO e o #28 (o diretorio existe; o node e criado de fato — o readback interno ao host casa byte a byte).
- **Causa estrutural:** `modules/fs.c` (com o pool estatico `g_fs_nodes[FS_MAX_NODES]`) e compilada em MULTIPLOS binarios — `userland.bin` embutido no kernel, o bootstrap/init (`KERNEL_USERLAND_SRCS`), o runtime do desktop e `userland.app` (boot app). Cada binario carregado tem `.data` PROPRIO, logo cada um tem uma arvore de nodes. Apps rodam no pool do processo-host que os lanca (init/svc); o shell interativo do rescue resolve caminhos no pool da PROPRIA copia; o writeback (`fs_flush` → particao raw) persiste para o PROXIMO boot, mas nao existe protocolo de re-import de nodes NOVOS em runtime — visao cross-pool so por heranca de particao.
- **Impacto honesto:** a convencao de arquivos de estado compartilhados (/runtime, /config) so e consistente DENTRO de um pool; consumidores cross-processo (shell `cat`, applets que leem arquivos escritos por outro host) veem arvore antiga. Ferramentas de prova precisam validar no MESMO pool (readback via API do proprio app) — como o probe de wget faz agora.
- **Decisao pendente [!]:** unificacao = ou (a) fs como SERVICO unico com IPC de path-resolve/write entre pools, ou (b) protocolo de re-import sob dirty-generation no leitor, ou (c) aceitar o modelo de particao como contrato (documentar "apps publicam via flush, leitores veem no proximo boot"). Escolha da mantenedora; nada no commit desta rodada depende dela.

### [x] 30. O port vendored do links2 nao compilava desde o landing (mesma familia "codigo de fachada" do microui): config.h em estilo autoconf vs testes `#if defined()` do links2, headers de compat incompletos, fontes core ausentes da source list — CORRIGIDO (13/11, terminal mode)

- **Comportamento observado:** `make ported-links2` morria no PRIMEIRO arquivo (main.c) com `alarm` redeclarado; cada iteracao expunha um buraco novo. O alvo nunca esteve na imagem nem no catalogo (linha 219 do plano) — a serial de docs dizia "links2 HTTP engine ja existe como base"; era base que NUNCA compilou neste repo.
- **Causas raiz medidas (todas corrigidas no commit):** (1) `config.h` vendored usava `#define HAVE_X 0` (estilo autoconf) mas o links2 testa `#if defined(HAVE_X)` — macro com valor 0 conta como "defined": `HAVE_PNG_H 0` forçava `#include <png.h>` (nao existe). Corrigido por sonda mecanica contra a cadeia de includes (headers existentes viram `1`, features inexistentes viram `#undef`). (2) `HAVE_SYS_WAIT_H` estava AUSENTE do arquivo — `sys/wait.h` nunca era incluido e `wait()` virava implicit-decl; e o proprio header de compat omitia a declaracao POSIX de `wait()`. (3) `compat/unistd.h` declarava `alarm()` duas vezes com tipos conflitantes. (4) `struct winsize` e os flags termios completos faltavam nos headers (kbd.c usa o conjunto manual inteiro porque nao ha `cfmakeraw` no config). (5) `main.c` definia `int main()` em vez da convencao `vibe_app_main` dos apps ring3. (6) `jsint.c` (motor JS, core no upstream) estava FORA da `LINKS2_CORE_SRCS` → 3 referencias indefinidas. (7) `app_entry/app_runtime` compilados sem `-I.` (header SDK por caminho de repo). (8) `config.h` nao tinha `VERSION`/`DEBUGLEVEL`/`HAVE_STRTOD`/`GRDRV_VIBEOS` coerentes com o flavor escolhido.
- **Simbolos libc que o port exigia e NAO existiam em lugar nenhum** (medido por nm no ELF): `htons/htonl/ntohs/ntohl` (declarados em header, definidos em ar nenhum), `strtod` (a falta fazia o fallback vendor do links2 chamar `sscanf`, que tambem nao existia), `atof`, `execl/execv` (declarados no unistd.h da lib desde o import, nunca implementados), `execvp`, `wait()` (só waitpid existia), `system()`, `lstat` (decl faltando), `h_addr` (macro BSD), `gethostbyname`. Implementados em `compat/src` sobre as primitivas reais do kernel (execve-despacho-por-registro, fork+waitpid, parse numerico).
- **Limitacoes honestas do flavor atual (terminal, `links2 -source`):** (a) `-DG` graphics segue INVIABEL — o pipeline de glifos do links2 passa por libpng e a arvore nao vende o backend; decisao documentada na receita (Build.links2.mk). (b) `fsync`/`rename`/`getsockname` retornam ENOSYS com diagnostico serial (kernel nao tem flush por-app, unlink, nem feedback de porta local — KNOWN_BUGS #29-adjacente; links2 degrada com aviso, nunca finge estado). (c) resolucao SIMBOLICA via libc `gethostbyname` e ENOSYS por design ate a unificacao de pools (#29); nomes IP-numerico funcionam de verdade, que e o caminho do gate.
- **Provas:** `make ported-links2` RC=0 (ELF 988 KB, header VAPP/ABI1, heap 4 MiB); `make` completo RC=0 com links2 no catalogo (aliases `/usr/bin/links2`,`/bin/links2`; strings do boot.img confirmam); gate `tools/probe_links2.py` → `build/links2-probe.md`: `busybox: try external links2` + `host: argv links2 -source` + `lang: image read ok`/`chunk0 verify match`/`app entry resolved` + http.server `GET / 200` (2x — retry) + `shell: command exit links2 rc=0`. O GATE AINDA FALHA na entrega do corpo (`end_dump state=-1 ce=0`) — causa raiz medida e catalogada separadamente em #34; a testemunha de corpo (`links2: source wrote`) ja esta no codigo e so imprimira quando #34 cair. A prova por VGA dump foi ABANDONADA: QEMU 11.1.2 std-vga nao expoe o bank 0xb8000 ao monitor (xp so ve 0xffffffff) — ver #33.

### [x] 31. Apps cujo `*_APP_BIN` e definido DEPOIS do `-include` do catalog eram silenciosamente EXCLUIDOS da imagem enquanto o header gerado continuava anunciando-os — CORRIGIDO (13/11)

- **Comportamento medido (13/11, links2):** launch do shell → `lang: directory loaded` → `lang: entry missing` → `erro: runtime externo nao instalado: links2`, apesar de `config/app_catalog.tsv` ter a linha, `build/generated/app_catalog.mk` listar `$(LINKS2_APP_BIN)` e o binario `build/links2/links2.app` estar construido. Parse do diretorio AppFS do `build/data-partition.img` mostrou a tabela com 108 entradas terminando em `wget` — links2 simplesmente NUNCA foi empacotado.
- **Causa raiz (duas camadas):** (1) `tools/generate_app_catalog.py` emitia `APP_CATALOG_APP_BINS := $(A_APP_BIN) $(B_APP_BIN) ...` — expansao IMEDIATA no momento do `-include` (~linha 1306 do Makefile). Todo `*_APP_BIN` definido mais tarde no arquivo (`LINKS2_APP_BIN` vivia em ~2552) expandia para VAZIO e o item desaparecia da lista SEM QUALQUER ERRO. (2) `LANG_APP_BINS := $(APP_CATALOG_APP_BINS)` repetia o padrao. O header `app_catalog.h` e gerado por caminho independente (TSV→strings) — por isso o registry do shell CONHECIA o nome mas o AppFS nao tinha a entrada: anuncios de catalogo e conteudo da imagem divergiam por construcao.
- **Correcao aplicada:** gerador agora emite `=` (recursivo, com comentario explicando o trap); `LANG_APP_BINS = $(APP_CATALOG_APP_BINS)`; e `LINKS2_APP_BIN` movido para junto das irmao `*_APP_BIN` (~linha 1260) por consistencia. Nenhum app existente mudou de valor: `make --eval` mede `words $(IMAGE_APP_BINS)` 108→109 e o parse do diretorio AppFS agora contem `links2` (109 entradas).
- **Provas:** `make` RC=0; parse python do `build/data-partition.img`: `count: 109 | links2: True`; gate `tools/probe_links2.py` re-executado contra a imagem corrigida (ver build/links2-probe.md).

### [x] 32. O stamp do port do links2 NAO dependia dos fontes vendor: edits em main.c/headers de compat deixavam um links2.app STALE na imagem sem qualquer rebuild — CORRIGIDO (13/11)

- **Comportamento medido (13/11):** apos adicionar a testemunha `links2: source wrote N bytes` em `userland/applications/network/links2/main.c`, `make ported-links2` reportou RC=0 mas o ELF/app continuou com o timestamp antigo e SEM a string nova no boot.img; `strings build/boot.img | grep 'source wrote'` = False.
- **Causa raiz:** `$(LINKS2_STAMP)` listava como deps apenas Build.links2.mk, lang/sdk e vibeos.{c,h} — os ~60 fontes vendor da arvore (e os headers do compat do port) nao invalidavam nada. E `$(LINKS2_APP_BIN): $(LINKS2_STAMP)` era o UNICO caminho do app; a regra do ELF com `$(LINKS2_OBJS)` so era alcancada pelo sub-make interno, que o stamp nao re-invocava. Mesma familia do gotcha "build incremental pode linkar obj stale" ja registrado para kernel.bin.
- **Correcao aplicada:** `LINKS2_SRCS := $(wildcard .../*.c *.h compat/*.h compat/sys/*.h compat/netinet/*.h compat/arpa/*.h)` e o stamp passou a depender delas (comentario na receita explica o trap). Apos o fix: app 988329→988393 bytes, `witness in app: True`, `in image: True`, `make` RC=0.
- **Prova viva:** gate `tools/probe_links2.py` re-executado com a testemunha na imagem (build/links2-probe.md).

### [!] 33. Console de app ring3 NAO e observavel pela serial nem pelo monitor em QEMU std-vga — a unica testemunha confiavel de output de app e via `host->write_debug`/syscall 11 no proprio caminho que se quer provar
- **Medido (13/11, gate links2):** `xp/4000/1b 0xb8000` pelo HMP retorna TODOS os bytes `0xffffffff` mesmo com o kernel escrevendo texto na tela (SeaBIOS std-vga: o monitor nao ve o window A0000-BFFFF habilitado pelo guest) — o metodo "ler 0xb8000 via HMP" documentado no closure-plan para SESSOES INTERATIVAS funciona para screenshots mas NAO para leitura de conteudo pelo monitor neste QEMU. Simultaneamente, a saida stdout de um app ring3 lancado via host vai para o console (tela), nunca para a serial — so o canal SYSCALL_WRITE_DEBUG chega ao log.
- **Correcao adotada no gate:** witness explicito no caminho vendor (`links2_source_witness` em main.c, emitido SOMENTE apos o loop de fragments escrever bytes reais — nunca um marker incondicional), assert via serial. Reavaliar quando existir leitura de framebuffer no harness (screenshot VGA via QMP screendump e o caminho obvio e nao-testado aqui).

### [~] 34. links2 -source: CINCO causas raiz corrigidas (sockaddr bridge, SO_* constants, select readiness, client write readiness, blocking connect) — o port agora CONECTA TCP de verdade (kernel witness `tcp connected rto=20` do app) e o send retorna (S_SENT); estagio final aberto: segunda `make_connection(port=1)` anomala + request nunca chega ao server — ABERTO (14/11), reprodutor canonico e o proprio gate

- **Causas raiz corrigidas (13-14/11, todas userspace/compat, ABI intocada):**
  1. Sockaddr layout: links2 compila `sa_family` como unsigned short @0 (Linux) contra syscalls que esperam sa_len@0/sa_family@1 (BSD — #20). Family byte 0 → connect -1 → ECONNREFUSED(61) → O_FAILED sem um pacote. Fix: bridge `compat_sockaddr_to_kernel/from_kernel` em connect/bind/sendto/recvfrom (compat/src/posix/socket.c).
  2. SO_* constants: vendored usa SOL_SOCKET 1/SO_ERROR 4; libcompat compila com os publicados (0xffff/0x1007). `connected()` (SO_ERROR pos-connect) caia no default ENOPROTOOPT(42) → stat=-42. Fix: links2/compat/sys/socket.h realinhado aos valores publicados.
  3. Select readiness: select() do compat era dirigido so por eventos (ring de 8; SOCKET_RECV pre-subscribe perdido para sempre). Fix: compat_fd_ready_for_read consulta `recv_ready` AO VIVO via SYSCALL_NETWORK_SOCKET_LIST(232) casando owner_pid+peer gravado no connect (a lista e posicional, sem handle).
  4. Client write readiness: connect()/bind() nao marcavam COMPAT_SOCKET_READY_SEND (so accept() marcava) — o write_select do cliente nunca disparava e o request nunca era escrito. Fix: marcar READY_SEND no sucesso de connect/bind (a limitacao de TX-full continua reportada pelo proximo send()).
  5. Connect nao-blocking: o kernel aceita o connect em SYN_SENT e o caller BSD-style trata rc==0 como ESTABLISHED — o send imediato era rejeitado. Fix: compat connect() agora AGUARDA o established (poll na socket-list 232, teto 20s; UDP/ICMP/UNIX resolvem sincronos).
- **Sequencia medida do gate (run com todos os witnesses):** `sched: load 0` → `load 9000` → objreq 0 (S_WAIT) → `conn: mk 8099` → objreq 1 → **`network: tcp connected rto=20` (o kernel conectou O APP — o blocking connect funcionou)** → `conn: mk 1` (SEGUNDA make_connection, porta 1 — anomala; unico caller e http.c:356 com `get_port(c->url)`) → objreq 6 (S_SENT) → end_dump S_WAIT pendurado. O http.server so ve o wire probe do kernel ("GET /", boot-time) — o request do app (`GET /index.html`) nunca sai do guest.
- **Proximo passo nomeado:** (a) trace da URL em make_connection + dns (por que a segunda conexao aponta porta 1 — suspeita: get_port sobre url alterada no retry/keepalive, ou corrupcao de heap do app); (b) apos mk resolvido, verificar o dispatch do write_select (bitmap READY_SEND marcado — se o GET sair, a perna de leitura herda a query recv_ready ja pronta). AVISO DE METODO permanente: `GET / HTTP/1.0 200` em servers de gates = wire probe do KERNEL (boot-time porta 8099), nunca evidencia do app.
- **Gate flaky:** corridas podem morrer em `serial frozen sending '<tecla>'` (freeze de boot pre-existente familia #35, ANTES do comando) — re-executar.
- **Reprodutor canonico:** `python3 tools/probe_links2.py --image build/boot.img --report build/links2-probe.md` — witnesses cobrem toda a cadeia. Gate VERMELHO de proposito ate o fim da perna de leitura (sem verde falso).

### [!] 35. Validate `abi-compat-surface` (matriz interativa 800x600 bsd-utils/vi/mg) vermelho PRE-EXISTENTE: o desktop nunca sobe (`desktop.app: launch startx` nao aparece) — medido em A/B contra o HEAD limpo; mesma familia do hang do desktop (#19) e NAO-regressao do links2 — ABERTO (13/11), decisao de arquitetura da mantenedora

- **Comportamento medido (13/11):** `make validate-abi-compat-surface` com o WIP do links2 falhou os 3 cenarios com `Timed out waiting for markers: desktop.app: launch startx, desktop: session start, desktop: session ready`. A/B: stash do WIP + mesmo target no HEAD limpo (`898014a3e`) → falha IDENTICA cenario a cenario (mesmos markers faltando). O port nao toca desktop/startx/session: nao-regressao.
- **Estado SMP correlato (mesma rodada, mesmo metodo):** `make validate-smp` no HEAD limpo → `fallback-no-toggle: PASS; smp2/smp4: FAIL (missing userland.app: shell start)`; com WIP → mesmo conjunto vermelho (variacao de qual marker extra falta = timing TCG). Pre-existente no mesmo eixo desktop-incompleto.
- **Assinatura:** os logs serial dos 3 cenarios terminam antes de qualquer `launch startx` — o autoboot do desktop em 800x600 nao executa o launch; sem ctrl-a envolvido, entao e um caminho DIFERENTE do #19 (que exige o primeiro ctrl-a). Mesmo eixo do #29 (pools separados) na pergunta "o que o processo desktop enxerga no boot".
- **Impacto honesto nos gates:** `validate-abi-final` (inclui abi-compat-surface) esta BLOQUEADO por este item — registrado como VERMELHO PRE-EXISTENTE, nao como verde nem "parcial": as rodadas recentes rodam e registram o que NAO depende de desktop (`validate-phase6` 6/6, `test-host` ALL PASS, wget gate PASS, `validate-network-datapath`).
- **Proximo passo nomeado:** pista propria "autoboot do desktop nao executa `launch startx`": nomear em que ponto para (launch_startx? `startx.app` no AppFS do cenario 800x600? divergencia `desktop.app:` vs `desktop:`?) ANTES de mexer em qualquer coisa; requer decisao de arquitetura da mantenedora (mesmo eixo do #29).

### [!] 36. FLAKY-REGRESSAO por A/B do commit 898014a3e (wget/#28): `make validate-storage-persist` fica INTERMITENTEMENTE VERMELHO em main — com `/runtime` criado, os 6+ pools fs passam a ter ARVORES DIFERENTES e o ultimo writeback da boot1 pode chegar rasgado/interleaved ao boot2 (checksum fail); SEM `/runtime` as copias eram byte-identicas e a corrida era encoberta; a moeda decide o resultado (mesma arvore: FAIL e depois PASS na mesma sessao de debug) — ABERTO (13/11), decisao de arquitetura [!] (dono unico do persist = eixo do #29)

- **A/B executado (13/11, mesma maquina/QEMU/validator):** HEAD `898014a3e` com a linha `(void)fs_create("/runtime",1);` → `**Result: FAIL** (NOT durable)` (boot1 `| yes | no | yes |`, boot2 `| yes | no | - |`). Com a MESMA linha comentada, wget/TSV/Makefile intocados → `**Result: PASS**` (boot2 `| no | yes | - |`). Unica variavel entre os dois: /runtime e o conteudo diferenciado que ele torna gravavel. FLAKY confirmado: com /runtime RESTAURADO e instrumentado, o validator PASSou posteriormente na mesma sessao — a corrida e de agendamento (qual processo escreve a janela por ultimo), nao deterministica.
- **Mecanismo (lido + medido):** o boot real sao 6+ instancias de `modules/fs.c` (bootstrap_init, svc hosts, shell, desktop... — KNOWN_BUGS #29 pools separados). Pre-#28, NENHUM estado de runtime era gravavel com sucesso: todas as arvores eram byte-identicas e qualquer writeback serializava os MESMOS bytes na MESMA janela de LBA — a corrida nao existia na pratica. Com /runtime funcionando, apps que escrevem estado de runtime (netmgrd status, taskmgr, applets; medido `wget: saved`) criam nodes com CONTEUDO DIFERENTE por pool; cada instancia que roda `fs_prepare_writeback` serializa um `used_bytes` diferente na mesma regiao. Writebacks concorrentes na mesma janela de setores = imagem final com header/checksum de um corpo parcial.
- **Escopo da correcao (decisao da mantenedora, amarrada ao #29):** escolher DONO UNICO da area de persist (fs-servico com IPC de flush, ou "only the host bootstrap process writebacks" com os demais em modo read-only/dirty-masking no persist). Reverter /runtime NAO e aceitavel (regrediria #28 de novo — os writes silenciosamente mortos voltam).
- **Estado atual dos gates:** `validate-storage-persist` intermitentemente vermelho em main; a rodada links2 NAO alterou persist (apenas diagnosticos `fs: deser fail ...` por branch — witnesses permanentes uteis para o proximo passo).
