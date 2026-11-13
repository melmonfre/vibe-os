# Plano de Reescrita do Backend Grafico

Este e um plano ativo de backend grafico. Para a documentacao tecnica do estado atual, veja [overview.md](overview.md) e [drivers.md](drivers.md).

## Objetivo

Reescrever o backend grafico do vibeOS para sair do modelo atual de `VBE/LFB + copia simples + heap fragil` e chegar a um pipeline mais apropriado para desktop e jogos, mantendo o driver atual como fallback seguro.

O foco imediato nao e um driver 3D completo por GPU. O foco imediato e:

- manter boot e desktop estaveis em qualquer resolucao suportada;
- acelerar desenho 2D e apresentacao de frame;
- reduzir dependencia de copias lentas e buffers alocados no pior momento do boot;
- preparar a base para suporte real a GPUs Intel antigas e AMD antigas no futuro.

## Direcao Tecnica

### Fase 0: realidade do projeto

Hoje o vibeOS usa um caminho generico baseado em VBE/LFB. Isso ainda e o melhor fallback universal para BIOS e maquinas antigas, mas ele nao pode continuar sendo o unico backend.

O plano novo sera:

- `backend_legacy_lfb`
  - driver atual, mantido como fallback.
- `backend_fast_lfb`
  - novo backend principal para curto prazo.
  - ainda usa framebuffer linear, mas com backbuffer fixo, politica de memoria correta, write-combining e flush otimizado.
- `backend_gpu_native`
  - camada futura para GPUs reais.
  - comecar por inicializacao e mode setting simples, nao por aceleracao 3D completa.

## Metas

- boot grafico estavel em resolucoes altas;
- `startx` e apps externos carregando sem regressao;
- backbuffer fixo sem leak de memoria;
- apresentacao de frame previsivel;
- aceleracao via PAT e rotinas de copia melhores;
- caminho de fallback automatico se o novo backend falhar.

## Nao objetivos imediatos

- nao prometer um driver "generico" unico que acelere Intel e AMD antigas de uma vez;
- nao portar o stack DRM inteiro do BSD/Linux de primeira;
- nao iniciar com OpenGL/Gallium/Mesa;
- nao depender de leitura de VRAM;
- nao trocar confiabilidade de boot por performance.

## Arquitetura proposta

### 1. Selecao de backend

Adicionar uma camada central de selecao:

- detectar ambiente de boot e capacidades;
- tentar `backend_fast_lfb`;
- se falhar, cair para `backend_legacy_lfb`;
- manter logs curtos e claros do motivo do fallback.

API sugerida:

- `video_backend_init()`
- `video_backend_present()`
- `video_backend_set_mode()`
- `video_backend_get_caps()`
- `video_backend_shutdown()`

### 2. Backend rapido baseado em LFB

Esse backend continua universal, mas com implementacao correta para jogos e desktop:

- backbuffer fixo em RAM;
- desenho sempre no backbuffer;
- flush unidirecional para o LFB;
- nunca ler da VRAM;
- tentar Write-Combining via PAT;
- rotinas de copia otimizadas:
  - `rep movsd/movsq` como baseline;
  - SSE2 `MOVNTDQ` quando disponivel;
  - AVX apenas depois, e so se houver save/restore correto de contexto.

### 3. Backend GPU nativo futuro

Separar por familias, nao por "driver generico":

- Intel antiga:
  - investigar `compat/sys/dev/pci/drm/i915`
  - alvo inicial: detecao PCI, BARs, MMIO seguro, mode set minimo, framebuffer nativo.
- AMD antiga:
  - avaliar caminho legado tipo Radeon KMS/fb antes de qualquer coisa moderna.
  - o material em `compat/sys/arch/*/radeonfb.c` pode servir mais como referencia do que como codigo plug-and-play.

Conclusao importante:

O material em `compat/` e valioso como referencia de:

- sequencia de bring-up;
- detecao de hardware;
- registradores e estruturas;
- organizacao de subsistemas DRM/fb.

Mas ele nao entra "como esta" no kernel do vibeOS sem uma camada grande de adaptacao.

## Checklist de Implementacao

## Fase 1: estabilizar a base

- [X] criar branch `feature/fast-video`; // branch limpa criada com o nome gpu-support. voce ja esta trabalhando nela
- [X] adicionar benchmark simples de frame:
  - `measure_frame_time()`
  - `measure_fill_time()`
  - `measure_present_time()`
- [X] registrar em debug:
  - resolucao ativa;
  - pitch;
  - bytes do framebuffer;
  - bytes do backbuffer;
  - heap livre antes e depois da inicializacao do video;
- [~] revisar todos os pontos que hoje alocam buffers graficos dinamicamente durante boot e `startx`; // backbuffer do video ja esta controlado; ainda falta seguir o rastro em apps/servicos
- [X] garantir que falha no backend novo nao impece boot do desktop.

## Fase 2: separar backend legado e backend novo

- [X] extrair o driver atual para um backend legado explicito;
- [X] introduzir interface comum de backend;
- [X] manter `legacy_lfb` como fallback padrao;
- [X] impedir que chamadas do desktop dependam de detalhes internos do backend atual.

## Fase 3: backbuffer fixo e politica de memoria

- [X] alocar backbuffer fixo em RAM principal;
- [~] preferir alocacao antecipada e estavel; // ja promove apos heap subir; ainda pode melhorar com arena fixa
- [X] impedir realocacoes por frame;
- [X] redirecionar desenho 2D para o backbuffer;
- [X] implementar `video_present()` unidirecional;
- [~] garantir que o kernel nunca leia da VRAM; // desenho e present novos ja sao unidirecionais; copia inicial do shadow ainda espelha LFB uma vez
- [X] manter um unico backbuffer por modo ativo;
- [X] registrar uso de memoria para evitar regressao em resolucao alta.

Observacao:

Se o heap atual continuar sendo bump-only, o backend novo precisa usar:

- arena fixa do video; ou
- paginas reservadas; ou
- alocador de paginas contiguas dedicado.

Nao da para depender de `kernel_free()` como se fosse allocator completo.

## Fase 4: Write-Combining com PAT

- [X] detectar suporte via `CPUID`;
- [X] configurar `IA32_PAT (0x277)`;
- [X] reservar slot para `Write-Combining`;
- [X] ajustar mapeamento das paginas do framebuffer para usar o tipo correto;
- [X] manter fallback para cache policy padrao se PAT falhar;
- [~] documentar claramente quais CPUs foram testadas; // `vidbench`, `vidsweep` e `vidmem` agora persistem `cpu_vendor/family/model/stepping` e tambem o PCI da GPU (`vendor/device/revision`) junto dos dados de video; o novo `vidreport` tambem junta essas coletas num pacote unico por maquina, faltando consolidar a campanha real

Se PAT nao existir:

- [ ] avaliar MTRR como fallback opcional;
- [X] manter implementacao isolada e desligavel.

## Fase 5: flush otimizado

- [X] baseline com copia simples correta;
- [X] versao com `rep movsd/movsq`;
- [X] versao SSE2 com `MOVNTDQ`;
- [X] `sfence` no final do flush nao temporal;
- [X] selecionar rotina por CPUID;
- [~] medir ganho real antes de promover uma rotina como default. // benchmarks existem; falta coletar comparativo real em hardware/modos maiores

## Fase 6: dirty rectangles e apresentacao parcial

- [X] introduzir dirty rects no backend, nao so na UI;
- [X] permitir flush parcial;
- [X] evitar copia da tela inteira quando so janelas pequenas mudarem;
- [~] manter modo full-frame para jogos e wallpaper; // politica explicita de present full entrou no backend/syscall; desktop e DOOM ja usam esse caminho, e o compat do Craft deixou de limitar a janela/framebuffer a `640x480`, passando a respeitar o modo grafico ativo para poder ocupar resolucoes maiores; ainda faltam workloads fullscreen adicionais e validacao widescreen/real
- [X] expor API simples:
  - `video_mark_dirty(rect)`
  - `video_present_dirty()`
  - `video_present_full()`

## Fase 7: pipeline para jogos

- [X] criar caminho dedicado para jogos fullscreen;
- [~] permitir frame pacing melhor; // DOOM fullscreen usa pacing basico por timer e agora espera antes do frame para reduzir input lag; Craft respeita `glfwSwapInterval(VSYNC)` no compat e faz a espera em `glfwPollEvents()` antes da coleta de input; ainda falta estrategia geral por app/backend
- [~] evitar copias extras entre syscall, microkernel e video; // syscall direto de fullscreen ja junta blit+present; o servico de video agora tambem tem o atalho equivalente `blit8 + present_full` para o caso 1:1 sem stretch, aceita variantes por `transfer_id` reaproveitado e os wrappers kernel-side mantem upload transfer reutilizavel por PID para evitar churn por frame; ainda falta migrar consumidores reais
- [~] permitir blit/stretch direto no backbuffer do video; // kernel ja tinha `blit8_stretch` direto no backbuffer; agora o servico de video tambem expoe `blit8_stretch` sem `present`, fechando a paridade entre syscall e microkernel; ainda falta ligar isso a consumidores reais
- [~] revisar input + present para reduzir latencia; // pacing de DOOM/Craft foi movido para antes da coleta do frame/eventos, reduzindo atraso artificial de input; ainda faltam medicoes e ajustes por app

## Fase 8: limpar gargalos do microkernel

- [~] revisar uso de transferencias para operacoes graficas pequenas; // texto curto, `blit8` pequeno, `blit8_present` pequeno, `blit8_stretch` pequeno e `blit8_stretch_present` pequeno no servico de video agora seguem inline no payload, sem `transfer`; ainda faltam outras operacoes pequenas
- [~] evitar buffers temporarios no heap do kernel para texto/palette/blit; // texto curto segue inline, texto longo agora tambem reaproveita o upload cacheado por PID em vez de `create/destroy` por chamada, `blit8` pequeno, `blit8_present` pequeno, `blit8_stretch` pequeno e `blit8_stretch_present` pequeno via microkernel nao alocam mais `transfer`; a palette 256x3 tambem deixou de fazer `create/destroy` por chamada e agora reaproveita um transfer cacheado por PID; uploads maiores ainda usam transferencias dedicadas
- [~] permitir fast path direto para syscalls graficas sensiveis; // syscall direta para `blit8 + present_full` agora existe alem do caminho `blit8_stretch_present`, reduzindo uma entrada no kernel em loops fullscreen 1:1 como o compat do Craft; ainda faltam outros casos guiados por medicao
- [~] medir custo de IPC por frame e por chamada; // bench do video agora registra `mk_frame`, `mk_flip`, `mk_blit` e `mk_stretch`, e tambem separa `blit_present` no caminho direto sem stretch; esses numeros ja podem ser consultados, recalculados e comparados em runtime via `vidbench`, inclusive entre politicas `DESKTOP` e `FULLSCREEN`; o comando agora tambem emite recomendacao automatica, tabela desktop-vs-fullscreen, pequenas series locais com media/min/max por politica, persiste o ultimo relatorio em arquivo e tambem anexa historico estruturado em CSV com modo/backend/present por execucao; ainda faltam medicoes reais por hardware
- [X] expor diagnostico basico de backend na UI; // o Task Manager agora mostra o backend de video detectado/ativo (ex.: `Intel i915`, `Radeon`, `nouveau`, `Bochs/BGA` ou fallback BIOS/VBE), o caminho de `present` (`byte loop`/`rep movsd`/`MOVNTDQ`) e metadados de scanout para facilitar validacao em hardware real sem depender so do serial

## Fase 9: backend GPU nativo experimental

Estado atual desta fase:

- [~] base PCI comum pronta para enumeracao de dispositivos de display;
- [~] pasta `kernel/drivers/video/drm/` criada como casa dos backends nativos;
- [~] `drm.c` ja centraliza probe, escolha de backend e dispatch de `modeset`;
- [~] `native_gpu_bga` virou o primeiro backend real dentro de `drm/`, preservando o runtime modeset validado no QEMU;
- [~] `native_gpu_i915` deixou de ser stub puro e ja faz bring-up inicial de probe com leitura de `PCI command`, `GGC`, `BSM/opregion`, geracao/plataforma estimada e aperture `GMADR`;
- [~] `native_gpu_nouveau` hoje TEM `set_mode` REAL: commit PCRTC (NV40-class) com snapshot-before / escrita de `HORIZ/VERT/HSYNC/VSYNC/START/PITCH/CONFIG` / readback-after / rollback no mismatch, handoff do framebuffer de boot via `bootinfo->vesa` e `revert_last_modeset` ligado no lifecycle compartilhado do DRM, modeset habilitado por default (`g_nouveau_modeset_enabled = 1`); QEMU nao emula display NVIDIA, entao esse caminho segue CODED-UNTESTED em silicio (trazido para hardware na campanha fisica P0/P6; witnesses de serial `nouveau: modeset ok` / `nouveau: revert ok`); no lado Radeon, o probe tambem ja classifica `family/asic`, destaca um recorte inicial plausivel de candidatos a `modeset` (`r6xx`/integrados proximos), saiu de offsets crus para um snapshot AVIVO `r6xx` com registradores D1/D2 nomeados (CRTC/GRPH/cursor/viewport/vline), resume esse estado em sinais passivos de "display block responding"/pipes programados, ja separa um preflight conservador de `set_mode` (modo suportado, pitch/framebuffer/aperture, snapshot de display e handoff de framebuffer de boot), executa um `stage` reversivel minimo do pipe D1 com readback, estabilidade de CRTC/blanking/cursor, logs de mismatch mais completos e verificacao explicita de rollback, agora tambem exigindo valores exatos esperados de `D1GRPH_UPDATE`, `D1MODE_MASTER_UPDATE_LOCK`, `D1MODE_MASTER_UPDATE_MODE` e `D1GRPH_ENABLE` em vez de aceitar qualquer valor "compativel", ja extrai esses invariantes esperados para helpers nomeados do D1 em vez de espalhar constantes no `stage/commit/readback`, passou a fazer o mesmo para o bloco preservado (`CRTC`/blanking/cursor) e agora tambem para o bloco programado (`surface`/`pitch`/`extent`), consegue fazer um primeiro `commit` persistente curto do D1 publicando `bootinfo->vesa` com verificacao e integracao em `drm revert/forget`, agora tambem exige um readback final do estado committed antes de aceitar sucesso e, nas rejeicoes de `before/stage/commit/restore`, ja distingue no serial falha de snapshot/readback de mismatch de registrador, resumindo tambem o snapshot `before/staged/committed/restored` do D1 para facilitar bring-up em hardware real; ainda faltam snapshots com semantica mais forte por registrador e endurecer mais o escopo desse primeiro handoff real;
- [ ] ainda falta portar um backend de hardware real em cima dessa estrutura.

### Onde paramos

Ultimo ponto implementado:

- `i915` ja esta quebrado em submodulos de `ids`, `pci`, `mmio`, `display` e `modeset`;
- o probe Intel ja coleta dados de PCI config, snapshot MMIO basico e snapshot passivo do estado de display/pipe A;
- ja existe um `mode plan` inicial separado para `i915`, que calcula timings conservadores de `TRANS_*`/`PIPESRC`, uma etapa de `stage` com escrita controlada + readback + restaure e tambem um helper separado de `commit` persistente para esses registradores e para o plane primario (`DSPCNTR/STRIDE/POS/SIZE/SURF`);
- o caminho de scanout primario do `i915` ja passou a tratar `stride` como parametro proprio alinhado, em vez de assumir `pitch == width`, para nao consolidar um handoff invalido nos registradores `DSPSTRIDE`;
- a camada PCI/DRM agora tambem propaga tamanho conhecido de BAR/aperture quando o BAR MMIO e 32-bit, e o `i915` ja usa isso para recusar modos cujo framebuffer alinhado nao cabe no aperture detectado;
- o `commit` experimental do `i915` tambem deixou de aceitar aperture "desconhecido": para handoff persistente, agora exige tamanho conhecido e suficiente, mantendo o fallback quando essa garantia nao existe;
- quando o `commit` experimental do `i915` consegue persistir o modo, ele agora tambem publica `fb_addr/pitch/width/height/bpp` em `bootinfo->vesa`, alinhando o estado global do video com o scanout nativo e fazendo rollback se essa etapa falhar;
- a chave `INTEL_I915_EXPERIMENTAL_COMMIT` deixou de exigir edicao manual do fonte e passou a poder ser ligada por build (`make INTEL_I915_EXPERIMENTAL_COMMIT=1`), mantendo `0` como default seguro;
- o loader BIOS agora tambem expoe um toggle de boot para esse experimento: no menu do `stage2`, a tecla `I` alterna `BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT`, mostra o estado na linha de debug e permite testar o `commit` nativo sem rebuild completo;
- esse estado tambem ficou visivel no proprio menu de boot com uma linha dedicada `I915 EXP ON/OFF`, para facilitar o teste manual em hardware sem depender da linha de debug;
- o bootstrap/userland e o proprio backend `i915` agora tambem registram explicitamente quando esse experimento foi ligado por boot, para facilitar leitura de log em hardware real sem depender so do estado visual do loader;
- o dispatcher de video tambem passou a logar explicitamente quando uma troca de modo veio por `drm` e quando caiu para `bios` fallback, incluindo o caso em que um backend nativo rejeita o modo pedido;
- o `i915` tambem ganhou log de mismatch por registrador no `readback` de `stage/commit`, para mostrar no serial exatamente quais `TRANS_*`, `PIPESRC` ou `DSP*` divergiram antes do rollback;
- o caminho de `restore` do `i915` agora tambem valida por `readback` se o rollback realmente recolocou `TRANS_*`, `PIPESRC` e `DSP*` no estado anterior, com log do registrador exato quando a restauracao diverge;
- esse `restore` tambem deixou de parar no primeiro erro: quando o rollback diverge, ele agora lista todos os registradores `TRANS_*`, `PIPESRC` e `DSP*` que ficaram fora do snapshot esperado;
- o chamador do `i915` agora tambem preserva e loga o resultado desse rollback, diferenciando no serial quando o plano falha mas a restauracao foi confirmada e quando o proprio rollback tambem falha;
- o dispatcher `drm` e o proprio `i915` agora tambem limpam `mode_out` de forma defensiva em caminhos de falha/rollback, para nao deixar handoff parcial do framebuffer nativo vazar entre tentativas;
- quando o `snapshot_display()` falha no `stage/commit/restore` do `i915`, o serial agora registra explicitamente em que etapa a leitura falhou e com qual contexto de `mmio/fb`, para separar falha de readback de mismatch de registrador;
- o rollback do `i915` tambem deixou de gerar falso positivo: se a restauracao escreve com sucesso mas o snapshot final do estado restaurado falha, isso agora conta como rollback falho, nao como `rollback verified`;
- o `i915` agora tambem guarda contexto do ultimo `commit` persistente bem-sucedido e expoe um caminho de revert explicito; com isso, se o handoff posterior falhar na camada de video, o kernel consegue tentar restaurar display + `bootinfo->vesa` antes de abortar a troca;
- o dispatcher `drm` tambem passou a lembrar qual backend nativo realmente persistiu o ultimo modeset, para que o revert explicito fique backend-aware e nao endureca um atalho fixo de `i915` dentro da camada comum;
- esse contexto de revert tambem passou a ser consumivel: depois que o handoff nativo fecha com sucesso na camada de video, o dispatcher esquece o ultimo modeset persistido para nao carregar snapshot stale entre trocas futuras;
- o proprio uso do revert tambem passou a consumir esse snapshot logo na entrada: se a tentativa de restauracao falhar no meio, o kernel nao reaproveita por engano um contexto parcial ou stale numa segunda tentativa;
- uma nova tentativa de `set_mode()` no `i915` tambem invalida logo no inicio qualquer snapshot antigo de revert, para que o rollback possivel fique sempre preso ao commit persistente que esta sendo tentado agora;
- o `commit` experimental do `i915` agora tambem faz preflight de `mode_out` e `bootinfo` antes de tocar nos registradores, bloqueando handoffs obviamente inviaveis sem precisar programar e reverter o hardware;
- a publicacao em `bootinfo->vesa` tambem deixou de depender do ponteiro `mode_out`: o `i915` agora usa o `mode` prevalidado do preflight como fonte unica do handoff persistente e trata `mode_out` apenas como espelho opcional para o chamador;
- esse preflight do `commit` tambem foi isolado em helper proprio no backend, deixando a fase "validar handoff" separada da fase "programar registradores" e com log mais direto quando o bloqueio acontece antes do hardware;
- o caminho de sucesso do `commit` tambem deixou de depender implicitamente de `mode_out`: ate o log final agora usa o `preflight_mode` validado, evitando desreferencia nula num chamador que aceite `mode_out` opcional;
- o handoff em `bootinfo->vesa` agora tambem ganhou verificacao por readback, tanto no publish do `commit` quanto no revert; assim, o caminho persistente deixa de tratar atualizacao de `bootinfo` como sucesso cego e passa a logar mismatch explicito se essa metade do estado divergir;
- tambem foi corrigido o rollback do caso em que o publish de `bootinfo->vesa` falha: antes o backend podia restaurar o display e deixar `bootinfo` parcialmente alterado; agora essa falha tambem tenta restaurar o snapshot anterior de `bootinfo`, mantendo display e estado global alinhados mesmo no erro;
- o dispatcher `drm` agora tambem esquece de forma completa o ultimo modeset persistido logo no inicio de uma nova tentativa, em vez de limpar so o `backend_kind`; isso evita que snapshots internos do backend anterior fiquem stale mesmo que um chamador futuro reuse a API fora do fluxo atual;
- a camada de video tambem ficou mais resiliente quando o handoff posterior ao modeset nativo falha: se o `drm` persistir o modo mas `vesa_init`/ativacao ainda rejeitarem o framebuffer, o kernel agora tenta reverter o modeset nativo e fazer fallback BIOS para o mesmo modo antes de abortar a troca;
- essa trilha de revert tambem deixou de ficar presa ao `i915`: o backend `native_gpu_bga` agora guarda snapshot do `bootinfo->vesa` anterior e tambem consegue restaurar o modo nativo previo quando o handoff falha depois do modeset, mantendo a recuperacao BIOS consistente tambem no backend virtual principal;
- o `native_gpu_bga` tambem deixou de tratar publish/revert como sucesso cego: agora valida por readback o modo realmente latched no hardware (`XRES/YRES/BPP/VIRT_WIDTH`) e o estado publicado em `bootinfo->vesa`, com log explicito se a programacao ou a restauracao divergirem;
- esse fallback de recuperacao tambem ficou visivel no serial: os logs de sucesso/falha da camada de video agora distinguem `bios` comum de `bios-after-drm-revert`, para nao confundir recuperacao apos `i915` parcial com um fallback BIOS direto desde o inicio;
- a trilha de recuperacao BIOS apos `revert` nativo tambem ganhou desfecho explicito: o serial agora diz quando essa recuperacao succeeded de fato e quando o proprio `set_mode` BIOS de recuperacao falha, evitando esconder esse caso atras do log generico final;
- alem do mismatch por registrador, o backend agora resume no serial o estado `before/after` de `pipesrc/htotal/vtotal/dspcntr/stride/surf` quando um `stage` ou `commit` e rejeitado, facilitando correlacao rapida no teste manual;
- o `stage_result` do `i915` tambem passou a ser zerado de forma defensiva antes do `commit`, evitando logs de rejeicao com estado indefinido quando a falha acontece cedo demais;
- tambem existe agora um alvo dedicado de regressao (`make validate-gpu-backends-i915-exp`) para rebuildar com `INTEL_I915_EXPERIMENTAL_COMMIT=1` e validar em QEMU que o caminho experimental nao regressou os outros backends DRM virtuais; isso ainda nao substitui teste em hardware Intel real, porque o QEMU dessa trilha nao emula um `i915` usavel;
- tambem entrou uma etapa dedicada de validacao em QEMU para subir com GPUs virtuais diferentes e checar pelos logs se o backend detectado confere com o dispositivo emulado;
- a trilha de recovery tambem ganhou regressao automatizada propria em QEMU (`make validate-gpu-backends-recovery`): ela sobe o backend `native_gpu_bga`, força um handoff nativo a falhar no selftest, exige `revert` explicito do modeset e valida que o framebuffer original volta a ficar utilizavel antes do boot seguir;
- o dispatcher do `i915` ja sabe chamar o helper de `commit`, preencher `mode_out` e fazer rollback se o handoff falhar, mas isso continua atras de uma chave explicita de experimento (`INTEL_I915_EXPERIMENTAL_COMMIT`) para nao substituir o fallback seguro antes da validacao;
- o proximo passo recomendado e validar esse caminho com a chave habilitada em ambiente controlado e, se passar, remover a trava gradualmente para uma familia curta de hardware.

### Intel antiga

- [X] criar detector PCI para GPUs Intel suportadas; // agora o dispatcher so anuncia `native_gpu_i915` para IDs conhecidos com staged modeset viavel (gen5+); Intel conhecido mas fora desse recorte cai como candidato legado/unsupported sem prometer handoff falso
- [~] mapear MMIO/BARs com seguranca; // acesso identity-mapped e helpers internos de MMIO ja existem; ainda falta formalizar estrategia para alias/mapeamento dedicado se sairmos do modelo atual de paginação
- [~] estudar bring-up minimo usando referencias de `compat/sys/dev/pci/drm/i915`; // registradores de timings/transcoder, pipe source, fuse/DFSM e plane primario (`DSP*`) ja entraram no snapshot/stage passivo
- [~] alvo inicial: framebuffer nativo e mode setting simples; // planejamento inicial de timings, `stage` com write/readback/restaure e helper de `commit` persistente ja cobrem `TRANS_*`, `PIPESRC` e `DSP*`; o detector PCI agora tambem filtra IDs Intel suportados e restringe `native_gpu_i915` ao recorte gen5+ que o modeset atual realmente cobre; o readback do plane primario tambem passou a validar enable/pipe/formato 8bpp/stride/surface de forma explicita; dispatcher/handoff de `mode_out` ja existe atras de flag experimental, faltam validacao mais ampla de scanout real em hardware
- [ ] sem aceleracao 3D no primeiro marco.

### AMD antiga

- [~] levantar familias realisticamente suportaveis; // probe Radeon agora ja classifica families/ASICs por `device_id` e marca um recorte inicial plausivel de bring-up (`r6xx`/integrados proximos) como candidato a primeiro `modeset`; ainda falta validar esse recorte contra referencias reais e reduzir a lista a uma familia curta
- [ ] estudar referencias `radeonfb` e material DRM legado 
- [ ] estudar e aplicar driver radeon https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/radeon;
- [ ] estudar e aplicar driver i915 https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/i915;
- [ ] estudar e aplicar driver nouveau https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/nouveau
- [ ] definir se o primeiro passo sera fb nativo ou KMS minimo;
- [ ] evitar prometer suporte a GPUs demais na primeira rodada.

### NVIDIA antiga

- [ ] tratar `nouveau` como referencia de registradores, init de display engine e sequencia de modeset basica;
- [ ] limitar escopo inicial a placas antigas bem documentadas e sem expectativa de aceleracao 3D;
- [ ] manter fallback imediato para `legacy_lfb` se qualquer etapa de bring-up falhar.

## Estrategia concreta para as 3 referencias Linux

Objetivo dessas referencias:

- usar `i915`, `radeon` e `nouveau` como mapa tecnico de bring-up;
- nao importar DRM/KMS inteiro;
- extrair apenas a sequencia minima para:
  - detectar a GPU via PCI;
  - mapear MMIO;
  - descobrir framebuffer/pll/display pipe relevantes;
  - ligar um modo fixo simples;
  - expor um backend `native_gpu_*` que ainda desenha no backbuffer do vibeOS.

### Ordem recomendada

1. `i915`
   - costuma ser o primeiro alvo mais realista para virtualizacao futura e hardware comum legado;
   - documentacao e referencias de registradores sao mais faceis de seguir;
   - melhor candidato para o primeiro backend nativo experimental.
2. `radeon`
   - bom segundo alvo para aprender bring-up de CRTCs/PLL com um stack mais separado;
   - util como referencia para framebuffer nativo em hardware antigo.
3. `nouveau`
   - manter como referencia de estudo e prova de conceito;
   - nao transformar no primeiro alvo, porque tende a abrir mais superficie de risco.

### Escopo minimo por backend nativo

Cada backend experimental deve nascer com escopo pequeno:

- detectar vendor/device no PCI;
- selecionar apenas uma familia curta de device IDs;
- mapear BAR MMIO com helper proprio do vibeOS;
- ler registradores suficientes para identificar display ativo;
- programar um modo fixo simples, preferencialmente `1024x768` ou `800x600`;
- obter ou configurar um framebuffer linear usavel;
- plugar esse framebuffer no pipeline atual:
  - backbuffer em RAM;
  - `present_full`;
  - `present_dirty`;
  - mesma politica de fallback do `fast_lfb`.

Fora do primeiro marco:

- GEM/TTM;
- command submission;
- ring buffers de GPU;
- aceleracao 2D/3D;
- hotplug;
- multi-monitor;
- power management;
- interruptos complexos.

### O que ler em cada arvore

`i915`

- sequencias de init de display pipe/plane;
- tabelas de device IDs;
- helpers de register read/write;
- partes de fb helper / modeset minimo;
- nocao de GTT apenas como contexto, nao como obrigacao inicial.

`radeon`

- bring-up de CRTC, PLL e encoder antigo;
- organizacao entre families e ASICs;
- como o driver separa display, framebuffer e VRAM apertures;
- caminhos de fb simples e sem aceleracao pesada.

`nouveau`

- init basico de display engine e varredura de families;
- estrategia de isolamento por chipset;
- referencias de registradores e sequencias de enable/disable de heads.

### Traducao para arquitetura do vibeOS

Antes de portar qualquer trecho, criar contrato interno:

- `gpu_detect()`
- `gpu_map_mmio()`
- `gpu_read32()/gpu_write32()`
- `gpu_modeset(width, height, bpp)`
- `gpu_get_lfb()`
- `gpu_shutdown_or_fallback()`

Cada backend (`native_gpu_i915`, `native_gpu_radeon`, `native_gpu_nouveau`) deve implementar esse contrato e depois encaixar na mesma interface que hoje ja existe entre:

- `legacy_lfb`
- `fast_lfb`
- futuros backends nativos

### Primeira entrega recomendada de Fase 9

Entrega pequena e valida:

- enumeracao PCI de VGA/GPU;
- escolha por vendor/device;
- backend `native_gpu_i915` experimental;
- modo fixo unico;
- `present_full` funcionando;
- fallback automatico para `fast_lfb` se qualquer passo falhar;
- logs de debug detalhando:
  - vendor/device;
  - BAR escolhido;
  - base MMIO;
  - base framebuffer;
  - modo programado;
  - motivo de fallback.

Se isso funcionar, o segundo passo vira:

- adicionar `present_dirty`;
- adicionar mais um modo;
- testar boot e `startx`;
- so depois pensar em `radeon`.

### Implementado nesta rodada

- `headers/kernel/drivers/video/drm/drm.h`
- `kernel/drivers/video/drm/drm.c`
- `kernel/drivers/video/drm/bga.c`
- `kernel/drivers/video/drm/i915.c`
- `kernel/drivers/video/drm/i915_ids.c`
- `kernel/drivers/video/drm/i915_pci.c`
- `kernel/drivers/video/drm/i915_mmio.c`
- `kernel/drivers/video/drm/i915_display.c`
- `kernel/drivers/video/drm/i915_modeset.c`
- `kernel/drivers/video/drm/radeon.c`
- `kernel/drivers/video/drm/nouveau.c`
- `headers/kernel/drivers/video/drm/i915/i915.h`
- `tools/validate_gpu_backends.py`
- `gpu.h/gpu.c` mantidos como camada de compatibilidade para nao espalhar quebra de interface
- `video.c` agora chama a camada `drm` para logar candidatos e tentar `modeset` antes do fallback BIOS
- validacao real em QEMU continua passando no fluxo `vidmodes-shell`
- a camada `drm` agora diferencia melhor alguns dispositivos virtuais suportados/nao suportados no QEMU, como `bochs_vbe`, `qxl_vga`, `vmware_svga2`, `cirrus_5446` e `virtio_vga`
- o probe `i915` passou a usar referencias locais de Linux/BSD para:
  - inferir geracao/plataforma por `device_id`;
  - tratar `BAR0` como MMIO e `BAR2` como `GMADR` no caminho Intel;
  - ler `GMCH/GGC`, `BSM` e `opregion` de PCI config;
  - estimar tamanho de stolen memory em geracoes modernas para debug de bring-up
- o backend `i915` tambem foi quebrado internamente em modulos menores:
  - `i915.c` como ponto de integracao do backend
  - `i915_ids.c` para tabela/plataforma/geracao
  - `i915_pci.c` para leitura de config-space e dados de probe
  - `i915_mmio.c` para helpers de MMIO e snapshot passivo de registradores
  - `i915_display.c` para snapshot passivo de timings/transcoder, `PIPESRC`, `FUSE_STRAP`, `DISP_ARB_CTL2` e `SKL_DFSM`
  - `i915_modeset.c` para gerar um `mode plan` inicial, fazer `stage` de `TRANS_*`/`PIPESRC` com readback e manter a escrita de registradores fora de `i915.c`
  - `i915.h` como contrato interno do backend

## Protocolo de testes

## Testes funcionais

- [~] poder selecionar o driver de video nas entries do bootloader; // o `stage2` agora ja oferece uma entry dedicada `LEGACY VIDEO`, a politica `DRIVER AUTO/LEGACY` no menu e uma tabela declarativa simples de entry->flags dentro do proprio loader; ainda falta levar a mesma escolha para um config externo/entries persistentes em disco em vez de metadata embutida no binario
- [X] boot em 640x480;
- [~] boot em 800x600; // validacao automatizada em QEMU agora existe via `make validate-gpu-boot-800x600`; ainda falta hardware real
- [~] boot em 1024x768; // validacao automatizada em QEMU agora existe via `make validate-gpu-boot-1024x768`; ainda falta hardware real
- [ ] boot em 1366x768; // no QEMU atual da matriz, pedir `1366x768` cai consistentemente para `1024x768`; ainda falta validar em hardware real ou em backend/firmware que exponha esse modo
- [ ] boot em 1360x768; // no QEMU atual da matriz, pedir `1360x768` cai consistentemente para `640x480`; ainda falta validar em hardware real ou em backend/firmware que exponha esse modo
- [ ] boot em 1920x1080; // no QEMU atual da matriz, pedir `1920x1080` cai consistentemente para `640x480`; ainda falta validar em hardware real ou em backend/firmware que exponha esse modo
- [~] troca de resolucao em runtime; // validada em QEMU no caminho `vidmodes` para 640x480, 800x600, 1024x768, 1152x864, 1280x1024 e 1600x1200; ainda falta hardware real e os modos widescreen do catalogo alvo
- [~] `startx` carrega em todos os modos; // validado em QEMU para 640x480, 800x600 e 1024x768 via `make validate-startx-800x600` e `make validate-startx-1024x768`; faltam os modos widescreen e hardware real
- [X] wallpaper e desktop aparecem sem artefato; // a conversao de PNG para a paleta 8-bit do desktop agora usa a mesma busca de cor mais proxima aplicada ao BMP, corrigindo o wallpaper com cores quebradas
- [~] DOOM abre e ocupa o modo atual; // validado em QEMU para 800x600 e 1024x768 via `validate_modular_apps.py`/`make validate-doom-*`; ainda faltam modos widescreen e hardware real
- [~] Craft abre sem corromper frame; // validado em QEMU para 800x600 e 1024x768 via `validate_modular_apps.py`/`make validate-craft-*`; o compat de GLFW tambem deixou de prender o app em `640x480` e agora limita a janela/framebuffer pelo modo grafico ativo, preparando a trilha para ocupar modos maiores; ainda faltam modos widescreen e hardware real

## Testes de stress visual

- [~] fill rapido com cores solidas; // o comando `vidstress` agora executa uma sequencia automatizada de fills fullscreen com espera curta por quadro para inspeção visual em hardware real
- [~] gradiente RGB; // `vidstress` agora gera e apresenta um gradiente RGB332 fullscreen por `blit8_stretch_present`, exercitando paleta + stretch + present no modo atual e nos modos testados
- [~] abrir/fechar janelas repetidamente; // o comando `appcycle` agora agenda no loop do desktop um ciclo automatizado de abrir/esperar/fechar apps graficos suportados (`terminal`, `filemanager`, `taskmgr`, `doom`, `craft`, etc.), reduzindo a parte manual desse stress
- [~] arrastar janelas com desktop cheio; // o comando `dragstress` agora popula o desktop com um conjunto de janelas suportadas e reposiciona automaticamente a janela focada em um padrao de cantos/posicoes intermediarias, exercitando redraw/clip/ordem de janelas sem depender de mouse manual
- [~] alternar repetidamente entre modos suportados; // `vidstress` agora pode ciclar automaticamente pelos modos expostos via `sys_gfx_caps`, renderizando gradiente/checkerboard em cada modo e restaurando o modo original no final

## Testes de memoria

- [~] heap do kernel antes/depois do boot grafico; // o comando `vidmem` agora consegue capturar `task_snapshot` + `sys_gfx_bench` no shell de boot ou em modo grafico, gerando relatorio/CSV para comparar heap antes e depois; ainda faltam coletas reais organizadas
- [~] heap antes/depois de `startx`; // `vidmem` pode ser rodado antes do `startx` e depois no terminal grafico, persistindo historico para comparacao; ainda falta executar essa matriz nas maquinas alvo
- [~] heap antes/depois de abrir jogos; // `vidmem` agora mede heap/physmem + estado do video antes/depois de uma carga grafica repetida e salva historico; ainda falta repetir a coleta ao redor de DOOM/Craft reais
- [~] validar que o backbuffer e fixo e nao cresce por frame; // `vidmem` agora registra `backbuffer_bytes` antes/depois de uma carga repetida de `clear/present/blit8_stretch_present` e marca `stable_backbuffer` no historico
- [~] validar que o backend novo nao depende de leaks "aceitaveis"; // `vidmem` agora deixa registrada a variacao de heap/physmem e do backbuffer por execucao; ainda faltam campanhas reais e criterio final de aceitacao

## Benchmark esperado

- [~] medir baseline atual; // bench de fill/present/frame/fullscreen_direct ja sai em debug e os comandos `vidbench`/`vidsweep` agora geram tabela consolidada por modo/backend/policy em texto + CSV; `vidsweep` tambem ja pode cruzar cada modo com `present_override=auto/byte/rep/movntdq/all`; ainda faltam coletas reais nas maquinas alvo
- [~] medir present full-frame com copia simples; // o caminho baseline existe e `vidbench`/`vidsweep` agora conseguem forcar `present_copy=byte_loop` alem de registrar esse override no historico; ainda falta coletar resultados representativos
- [~] medir present com WC; // present_kind e PAT/WC ja entram na matriz emitida por `vidbench`/`vidsweep`, e ambos tambem permitem fixar a rotina de copia durante a medicao; ainda falta benchmark real em hardware
- [~] medir present com SSE2 non-temporal; // MOVNTDQ + sfence implementados e `vidbench`/`vidsweep` agora conseguem forcar `present_copy=movntdq` (com fallback seguro quando o hardware nao suporta), alem de persistir override pedido e caminho efetivo no historico; falta benchmark real em hardware
- [~] registrar meta realista de ganho: // `vidbench all` agora persiste uma secao automatica `video bench gain target` em `/vidbench-last.txt`, comparando o baseline simples (`byte_loop`, ou o primeiro baseline valido) contra a melhor rotina medida e classificando o resultado em `sub-2x`, `2x-5x` ou `5x+`; o `vidreport` passa a carregar isso junto no bundle, faltando validar e consolidar a campanha em hardware real
  - 2x a 5x no curto prazo ja e excelente;
  - 5x a 10x so deve ser prometido apos benchmark real.

## Reaproveitamento de compat/BSD

## O que vale reaproveitar

- estruturas e organizacao do subsistema DRM;
- rotinas de detecao PCI e enumeracao;
- referencias de registradores para i915;
- ideias de fb helper;
- MTRR/PAT helpers como referencia conceitual.

## O que nao vale copiar cegamente

- dependencias profundas do ecossistema DRM;
- locking e scheduler assumidos por outro kernel;
- memory management/GEM/TTM inteiros;
- partes que pressupoe VM, DMA, interruptos e userspace mais maduros do que o vibeOS tem hoje.

## Estrategia pratica de compat

- usar `compat/` primeiro como biblioteca de referencia tecnica;
- portar trechos pequenos e isolados;
- evitar importar arvores grandes sem adaptar contrato de memoria, lock, IRQ e PCI;
- documentar cada reuse com dono, escopo e risco.

## Avisos de seguranca

- nunca ler da VRAM como fonte de verdade;
- nunca misturar framebuffer fisico com area temporaria nao mapeada corretamente;
- nao ativar SSE/AVX em kernel sem garantir contexto correto;
- nao depender de PAT sem fallback limpo;
- manter caminho de boot e recovery com backend legado sempre disponivel.

## Entregas planejadas

### Marco 1

- backend legado isolado;
- benchmark basico;
- backbuffer fixo;
- present full-frame correto;
- sem regressao de boot.

### Marco 2

- PAT/write-combining;
- copia otimizada por CPU feature;
- present mais rapido e estavel;
- resolucoes altas funcionando com `startx`.

### Marco 3

- dirty rects reais;
- caminho de jogo fullscreen;
- menos overhead nas syscalls graficas.

### Marco 4

- prototipo experimental de backend GPU nativo para uma familia pequena de Intel ou AMD antiga.

## Estado atual do codigo

Em 25/03/2026, o backend de video ja saiu do formato "um driver so" e passou a ter duas implementacoes explicitas:

- `legacy_lfb`
  - fallback direto no framebuffer;
  - continua como caminho seguro quando heap/backbuffer nao sao viaveis.
- `fast_lfb`
  - backbuffer em RAM quando houver memoria suficiente;
  - flush full-frame e parcial;
  - selecao de rotina de present por feature de CPU.

O que ja existe no codigo:

- deteccao de `PAT` e `SSE2` via `CPUID`;
- tentativa de habilitar SSE no kernel para o caminho de present;
- configuracao de `IA32_PAT` e remapeamento do framebuffer com politica WC;
- politica de present com modos `auto`, `dirty` e `full`;
- catalogo de resolucoes separado para `1360x768` e `1366x768`;
- rotina baseline;
- rotina com `rep movsd`;
- rotina SSE2 com `MOVNTDQ` + `sfence`;
- dirty rect acumulado no backend;
- API publica de `present_full`, `present_dirty` e `mark_dirty`;
- politica persistente de `present` para separar desktop (`dirty`) de fullscreen (`full-frame`);
- fast path de syscall para `blit8_stretch + present_full` em uma unica entrada de kernel para fullscreen;
- fast path de syscall para `blit8 + present_full` agora tambem existe para loops 1:1 sem stretch, e o compat do Craft foi migrado para esse caminho;
- benchmark do video agora tambem registra `blit_present`, separando o custo do novo fast path direto `blit8 + present_full` 1:1 do fullscreen stretch e do caminho via microkernel;
- benchmark do video agora tambem pode ser consultado por syscall/userland, e o `busybox` ganhou `vidbench` para mostrar esses numeros sem depender do serial;
- a consulta desses benchmarks agora recalcula o snapshot na hora, para refletir o modo/backend atual em vez de depender apenas do ultimo init ou `set_mode`;
- `vidbench` agora tambem mostra um resumo comparativo simples com overhead relativo entre baseline direto, `blit_present` e `mk_frame`, para acelerar a escolha do proximo fast path;
- `vidbench` agora tambem coleta snapshots separados para politicas `DESKTOP` e `FULLSCREEN`, restaurando `DESKTOP` ao final para facilitar comparacao de comportamento no mesmo backend;
- `vidbench` agora tambem emite uma recomendacao automatica curta baseada no maior overhead observado, para apontar se o proximo foco deveria ser microkernel/IPC, present direto 1:1 ou copy path do backend;
- `vidbench` agora tambem imprime uma tabela compacta `desktop vs fullscreen` com delta percentual das metricas principais, fechando a comparacao lado a lado no proprio comando;
- `vidbench` agora tambem pode coletar multiplas amostras por politica e mostrar estabilidade basica (`avg/min/max`) dos caminhos principais, reduzindo ruido do snapshot unico;
- `vidbench` agora tambem salva o ultimo relatorio textual em `/vidbench-last.txt`, criando uma base simples para comparacao entre boots sem depender apenas do console/serial;
- caminho separado de `blit8_stretch` para desenhar direto no backbuffer e apresentar depois quando o caller quiser;
- mensagem equivalente no servico de video/microkernel para `blit8_stretch + present_full`, pronta para consumidores futuros;
- wrappers do servico de video agora tambem aceitam fast path por `transfer_id` reaproveitado para `blit8` e `blit8_stretch_present`, removendo a copia intermediaria nos callers que ja trabalham com transferencias;
- wrappers do servico de video tambem passaram a reaproveitar um upload transfer por PID para blits repetidos, evitando `create/destroy` e heap churn por frame no caminho microkernel;
- servico de video agora tambem expõe `blit8_stretch` sem `present`, permitindo que callers via microkernel escrevam no backbuffer e decidam o `present` separadamente;
- texto curto no servico de video agora usa payload inline em vez de `transfer`, reduzindo overhead para operacoes pequenas de debug/UI;
- `blit8` pequeno no servico de video agora tambem pode seguir inline no payload, reduzindo overhead para sprites/blocos curtos sem alocacao temporaria;
- `blit8_stretch` pequeno no servico de video agora tambem pode seguir inline no payload, reduzindo overhead para stretchs curtos sem `transfer` separado;
- caminho fullscreen full-frame no `fast_lfb` pode escrever direto no LFB e ressincronizar o shadow backbuffer ao voltar para desktop;
- DOOM fullscreen agora usa pacing basico por `sys_ticks()` para aproximar a cadencia de `TICRATE` e reduzir render sem limite;
- Craft agora aplica pacing basico quando `glfwSwapInterval(VSYNC)` estiver ativo e faz a espera no `glfwPollEvents()` para nao empurrar input para depois do sleep;
- benchmark simples de fill/present/frame em ticks de timer;
- benchmark simples de fullscreen direct-path em ticks de timer para comparar com present full tradicional;
- benchmark simples do caminho via servico de video/microkernel (`mk_frame`, `mk_flip`, `mk_blit`, `mk_stretch`) para começar a expor custo de IPC por frame fullscreen e por chamada;
- catalogo de modos do boot e troca de modo em runtime;
- logs de debug com modo, pitch, bytes de frame/backbuffer, heap e flags de CPU.

Riscos e pendencias que continuam reais:

- o shadow backbuffer ainda nasce a partir de uma copia inicial do LFB, entao o objetivo "nunca ler VRAM" ainda nao esta 100% fechado;
- o limite de `VIDEO_BACKBUFFER_MAX_BYTES` ainda favorece modos menores e precisa ser revisto quando os testes em resolucoes mais altas entrarem;
- o caminho SSE2 foi habilitado, mas ainda precisa de validacao cuidadosa com contexto de kernel e carga real;
- o ganho de WC/non-temporal ainda nao foi provado com uma tabela seria de benchmarks;
- o caminho fullscreen do DOOM ja junta `blit/stretch + present`, e o backend agora pode evitar a copia final no caso full-frame, mas ainda falta migrar consumidores adicionais e medir o ganho real;
- a trilha de alocacoes graficas fora do backend principal ainda nao foi auditada ate o fim em `startx`, desktop e jogos.

## Proxima rodada recomendada

Sequencia mais segura para continuar daqui:

1. Fechar a validacao funcional de modos:
   - boot e `startx` em `800x600`, `1024x768`, `1366x768`, `1360x768` e `1920x1080`;
   - confirmar troca de modo em runtime em mais de um backend;
   - reapontar o smoke test automatizado para widescreen (`1360x768`/`1366x768`/`1920x1080`) quando esses modos estiverem presentes no catalogo do alvo de teste.
2. Coletar benchmark comparativo de verdade:
   - `legacy_lfb` vs `fast_lfb`;
   - `rep movsd` vs `MOVNTDQ`;
   - com e sem WC, quando der para desligar isoladamente.
3. Fechar a politica de memoria:
   - decidir se o backbuffer vai para arena fixa do video ou paginas dedicadas;
   - remover dependencia da copia inicial do LFB quando o modo entra ja com backbuffer disponivel.
4. Auditar o caminho microkernel/userland:
   - medir custo de `startx`;
   - localizar buffers temporarios ainda alocados fora do backend;
   - preparar um fast path para blits sensiveis a latencia.
5. Fechar o pipeline de jogos alem da politica de present:
   - permitir blit/stretch direto no backbuffer do video;
   - reduzir copias entre syscall, microkernel e backend;
   - revisar pacing/input para cargas fullscreen.

## Decisao de produto

Para o vibeOS fazer sentido como sistema que roda jogos, o caminho certo nao e descartar VBE/LFB agora, e sim:

- transformar LFB em um backend rapido e confiavel;
- manter fallback automatico;
- preparar o kernel para um backend GPU nativo por etapas;
- so depois partir para suporte vendor-specific mais profundo.

Isso reduz risco, melhora muito a experiencia ja no curto prazo e cria uma base honesta para evolucao real.

-- decisão aceita. pode fazer
