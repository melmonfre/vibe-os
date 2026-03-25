# Plano de Reescrita do Backend Grafico

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
- [ ] adicionar benchmark simples de frame:
  - `measure_frame_time()`
  - `measure_fill_time()`
  - `measure_present_time()`
- [ ] registrar em debug:
  - resolucao ativa;
  - pitch;
  - bytes do framebuffer;
  - bytes do backbuffer;
  - heap livre antes e depois da inicializacao do video;
- [ ] revisar todos os pontos que hoje alocam buffers graficos dinamicamente durante boot e `startx`;
- [ ] garantir que falha no backend novo nao impece boot do desktop.

## Fase 2: separar backend legado e backend novo

- [ ] extrair o driver atual para um backend legado explicito;
- [ ] introduzir interface comum de backend;
- [ ] manter `legacy_lfb` como fallback padrao;
- [ ] impedir que chamadas do desktop dependam de detalhes internos do backend atual.

## Fase 3: backbuffer fixo e politica de memoria

- [ ] alocar backbuffer fixo em RAM principal;
- [ ] preferir alocacao antecipada e estavel;
- [ ] impedir realocacoes por frame;
- [ ] redirecionar desenho 2D para o backbuffer;
- [ ] implementar `video_present()` unidirecional;
- [ ] garantir que o kernel nunca leia da VRAM;
- [ ] manter um unico backbuffer por modo ativo;
- [ ] registrar uso de memoria para evitar regressao em resolucao alta.

Observacao:

Se o heap atual continuar sendo bump-only, o backend novo precisa usar:

- arena fixa do video; ou
- paginas reservadas; ou
- alocador de paginas contiguas dedicado.

Nao da para depender de `kernel_free()` como se fosse allocator completo.

## Fase 4: Write-Combining com PAT

- [ ] detectar suporte via `CPUID`;
- [ ] configurar `IA32_PAT (0x277)`;
- [ ] reservar slot para `Write-Combining`;
- [ ] ajustar mapeamento das paginas do framebuffer para usar o tipo correto;
- [ ] manter fallback para cache policy padrao se PAT falhar;
- [ ] documentar claramente quais CPUs foram testadas.

Se PAT nao existir:

- [ ] avaliar MTRR como fallback opcional;
- [ ] manter implementacao isolada e desligavel.

## Fase 5: flush otimizado

- [ ] baseline com copia simples correta;
- [ ] versao com `rep movsd/movsq`;
- [ ] versao SSE2 com `MOVNTDQ`;
- [ ] `sfence` no final do flush nao temporal;
- [ ] selecionar rotina por CPUID;
- [ ] medir ganho real antes de promover uma rotina como default.

## Fase 6: dirty rectangles e apresentacao parcial

- [ ] introduzir dirty rects no backend, nao so na UI;
- [ ] permitir flush parcial;
- [ ] evitar copia da tela inteira quando so janelas pequenas mudarem;
- [ ] manter modo full-frame para jogos e wallpaper;
- [ ] expor API simples:
  - `video_mark_dirty(rect)`
  - `video_present_dirty()`
  - `video_present_full()`

## Fase 7: pipeline para jogos

- [ ] criar caminho dedicado para jogos fullscreen;
- [ ] permitir frame pacing melhor;
- [ ] evitar copias extras entre syscall, microkernel e video;
- [ ] permitir blit/stretch direto no backbuffer do video;
- [ ] revisar input + present para reduzir latencia.

## Fase 8: limpar gargalos do microkernel

- [ ] revisar uso de transferencias para operacoes graficas pequenas;
- [ ] evitar buffers temporarios no heap do kernel para texto/palette/blit;
- [ ] permitir fast path direto para syscalls graficas sensiveis;
- [ ] medir custo de IPC por frame e por chamada.

## Fase 9: backend GPU nativo experimental

### Intel antiga

- [ ] criar detector PCI para GPUs Intel suportadas;
- [ ] mapear MMIO/BARs com seguranca;
- [ ] estudar bring-up minimo usando referencias de `compat/sys/dev/pci/drm/i915`;
- [ ] alvo inicial: framebuffer nativo e mode setting simples;
- [ ] sem aceleracao 3D no primeiro marco.

### AMD antiga

- [ ] levantar familias realisticamente suportaveis;
- [ ] estudar referencias `radeonfb` e material DRM legado;
- [ ] definir se o primeiro passo sera fb nativo ou KMS minimo;
- [ ] evitar prometer suporte a GPUs demais na primeira rodada.

## Protocolo de testes

## Testes funcionais

- [ ] poder selecionar o driver de video nas entries do bootloader;
- [ ] boot em 640x480;
- [ ] boot em 800x600;
- [ ] boot em 1024x768;
- [ ] boot em 1366x768;
- [ ] boot em 1360x768;
- [ ] boot em 1920x1080;
- [ ] troca de resolucao em runtime;
- [ ] `startx` carrega em todos os modos;
- [ ] wallpaper e desktop aparecem sem artefato;
- [ ] DOOM abre e ocupa o modo atual;
- [ ] Craft abre sem corromper frame.

## Testes de stress visual

- [ ] fill rapido com cores solidas;
- [ ] gradiente RGB;
- [ ] abrir/fechar janelas repetidamente;
- [ ] arrastar janelas com desktop cheio;
- [ ] alternar repetidamente entre modos suportados.

## Testes de memoria

- [ ] heap do kernel antes/depois do boot grafico;
- [ ] heap antes/depois de `startx`;
- [ ] heap antes/depois de abrir jogos;
- [ ] validar que o backbuffer e fixo e nao cresce por frame;
- [ ] validar que o backend novo nao depende de leaks "aceitaveis".

## Benchmark esperado

- [ ] medir baseline atual;
- [ ] medir present full-frame com copia simples;
- [ ] medir present com WC;
- [ ] medir present com SSE2 non-temporal;
- [ ] registrar meta realista de ganho:
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

## Decisao de produto

Para o vibeOS fazer sentido como sistema que roda jogos, o caminho certo nao e descartar VBE/LFB agora, e sim:

- transformar LFB em um backend rapido e confiavel;
- manter fallback automatico;
- preparar o kernel para um backend GPU nativo por etapas;
- so depois partir para suporte vendor-specific mais profundo.

Isso reduz risco, melhora muito a experiencia ja no curto prazo e cria uma base honesta para evolucao real.
