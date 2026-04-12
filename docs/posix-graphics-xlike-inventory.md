# POSIX Graphics X-Like Inventory

## Objetivo desta rodada

Abrir a trilha grafica POSIX/X-like com contratos pequenos e versionados, aproveitando o desktop nativo atual como host de janelas em vez de introduzir um segundo compositor completo.

## Candidatos iniciais

| Candidato | Origem atual | Classe | Leitura para a trilha | Prioridade |
| --- | --- | --- | --- | --- |
| `links2` | `userland/applications/network/links2` | misto (`Xlib`, framebuffer, DirectFB, SDL opcional) | melhor referencia interna para descobrir o subconjunto minimo de `Xlib`; nao e o melhor piloto MVP porque ja pede clipboard, atoms, `XImage`, IME/input method e integracao WM | alta para inventario, media para piloto |
| `vibe-browser` | `userland/applications/network/vibe-browser` | nao-X11, toolkit grande | confirma que Qt/toolkits grandes nao entram no MVP e dependem de uma camada muito mais larga | fora do escopo inicial |
| `xclock` | ainda nao importado | `Xlib` puro | bom piloto de janela, expose, timer e texto simples | alta para piloto externo |
| `xeyes` | ainda nao importado | `Xlib` puro | bom piloto para redraw e motion input | alta para piloto externo |
| `xlogo` | ainda nao importado | `Xlib` puro | bom smoke test de desenho 2D e resize | alta para piloto externo |

## Separacao por familia

### `Xlib` puro

- `xclock`
- `xeyes`
- `xlogo`

### `SDL`

- nenhum candidato priorizado nesta fase

### `GLFW`

- nenhum candidato priorizado nesta fase

### Mistos

- `links2`: tem backend `X11/Xlib` explicito e tambem caminhos alternativos

### Ports de terminal usados como regressao paralela

- familia `compat/games/*` publicada no desktop como `APP_TERMINAL`
- esses ports validam launch, input, espera bloqueante e isolamento de processo no desktop
- eles nao sao evidencia de falta de `Xlib`; se abrirem com tela preta, o primeiro suspeito e o launcher/terminal path

## Menor subconjunto de API para Fase 1 e Fase 2

### Display e screen

- `XOpenDisplay`
- `XCloseDisplay`
- `DefaultScreen`
- `RootWindow`
- `ConnectionNumber`

### Janela

- `XCreateSimpleWindow`
- `XDestroyWindow`
- `XMapWindow`
- `XUnmapWindow`
- `XMoveWindow`
- `XResizeWindow`
- `XMoveResizeWindow`
- `XStoreName`

### Eventos e sincronizacao

- `XSelectInput`
- `XPending`
- `XNextEvent`
- `XPeekEvent`
- `XFlush`
- `XSync`

### Desenho 2D

- `XCreateGC`
- `XFreeGC`
- `XSetForeground`
- `XSetBackground`
- `XDrawPoint`
- `XDrawLine`
- `XDrawRectangle`
- `XFillRectangle`
- `XCopyArea`
- `XPutImage`

### WM e atoms minimos

- `XInternAtom`
- `XSetWMProtocols`
- `WM_DELETE_WINDOW`
- `WM_PROTOCOLS`

### Texto e utilitarios minimos

- `XTextProperty`
- `XSizeHints`
- `XClassHint`
- `XStringListToTextProperty`

## Semanticas reais vs shim

### Reais no MVP

- filas de evento por cliente compat
- IDs estaveis de recursos no protocolo wire
- roteamento isolado de janelas compat para o desktop nativo
- eventos basicos de teclado, mouse, foco, expose e close
- desenho 2D previsivel sobre surface/pixmap MVP

### Shim/fallback aceito no MVP

- `RootWindow` virtual unico
- `DefaultScreen == 0`
- um unico display por processo no primeiro corte
- fontes inicialmente bitmap ou texto traduzido para o raster nativo
- clipboard/selecao com semantica minima e sem ICCCM completa
- hints de WM somente no subconjunto necessario para titulo, close e tamanho

### Fora do corte inicial

- `Wayland`
- `XCB`
- `GLX`/OpenGL real
- reparenting classico amplo
- extensoes X11 amplas
- IME complexo e internacionalizacao completa de input method

## Contrato de arquitetura

### Cliente compat

- `libX11` minima roda dentro do processo POSIX portado
- traduz chamadas `Xlib` para um protocolo wire versionado
- nunca fala direto com o compositor principal

### Servidor grafico compat

- processo/servico separado do desktop nativo
- owns tabela de recursos compat: `window`, `pixmap`, `gc`, `cursor`, `atom`
- traduz cada janela compat para uma janela nativa controlada pelo desktop atual
- reinicia ou falha sem derrubar a sessao principal

### Desktop nativo

- continua sendo o dono do compositor, foco e taskbar
- enxerga apps compat como janelas convidadas
- pode negar operacoes perigosas que tentem assumir controle global da sessao

## Regras de ABI do protocolo

- wire format little-endian na v1
- structs wire com campos de 32 bits ou 16 bits explicitamente tipados
- padding reservado ja nasce zerado e precisa ser preservado por clientes e servidor
- todo pacote comeca com `abi_version`, `opcode`, `size_bytes`, `request_id`
- handles wire sao `uint32_t` mesmo que a API publica `Xlib` exponha `unsigned long`
- evolucao futura deve ser aditiva ou por nova `abi_version`

## Primeiros pilotos recomendados

1. `xlogo`
2. `xeyes`
3. `xclock`

`links2` entra depois como validacao de que a camada suporta um app interativo maior, mas nao deve definir sozinho o MVP.

## Uso dos BSD games durante Fases 2 a 5

- usar `fortune`, `snake-bsd` e `tetris-bsd` como regressao de launch via desktop sem piscar/preto
- tratar esses ports como prova de que o caminho `desktop -> terminal -> app compat` continua estavel
- nao usar esses ports como criterio de pronto para `libX11`; para isso os pilotos corretos continuam sendo apps `Xlib` puros
