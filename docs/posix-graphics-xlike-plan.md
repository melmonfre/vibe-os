# POSIX Graphics X-Like Compatibility Plan

## Objetivo

Criar uma camada de compatibilidade grafica estilo `X11` para apps POSIX/BSD/Linux selecionados, sem quebrar o desktop nativo do `vibeOS` e sem competir com a finalizacao do plano de ABI base.

## Regra Principal

- [x] Este plano so comeca de verdade depois que a Fase 10 de `docs/abi-improvements.md` estiver fechada
- [ ] Toda compatibilidade grafica deve ser incremental, opcional e isolada do desktop nativo
- [ ] Nenhum app externo deve poder derrubar a sessao grafica principal
- [ ] A compatibilidade inicial deve priorizar `Xlib` basico e nao `Wayland`
- [ ] O backend grafico compat deve traduzir para o compositor/janela do `vibeOS`, nao substitui-lo

## Escopo Inicial

- [ ] Janela basica
- [ ] Eventos de teclado e mouse
- [ ] Blit/imagem 2D
- [ ] Texto simples
- [ ] Clipboard minimo
- [ ] Timer/event loop
- [ ] Selecao/foco
- [ ] Fontes bitmap ou shim simples

## Fora de Escopo Inicial

- [ ] OpenGL real
- [ ] Aceleracao 3D geral
- [ ] `Wayland`
- [ ] `XCB`
- [ ] extensoes X11 amplas
- [ ] window manager externo completo
- [ ] reparenting total estilo X11 classico
- [ ] compatibilidade universal com qualquer app Linux/BSD grafico

## Fase 0: Inventario e Contratos

- [x] Inventariar apps/ports candidatos que hoje pedem `X11`
- [x] Separar apps `Xlib` puros, apps `SDL`, apps `GLFW` e apps mistos
- [x] Mapear o menor subconjunto de API necessario para os primeiros ports
- [x] Definir o contrato entre cliente compat, servidor grafico compat e desktop nativo
- [x] Documentar quais semanticas serao reais e quais serao shim/fallback

Referencia inicial desta fase:

- `docs/posix-graphics-xlike-inventory.md`
- `headers/include/posix_gfx_compat.h`

## Fase 1: ABI e Headers Graficos

- [x] Publicar headers minimos estilo `X11/Xlib.h`, `X11/Xutil.h`, `X11/Xatom.h`
- [x] Definir tipos, handles, eventos e IDs estaveis
- [x] Versionar structs compartilhadas do protocolo grafico compat
- [x] Definir politica de endian, alinhamento e padding do protocolo
- [x] Reservar espaco para extensoes futuras sem quebrar ABI

Headers publicados nesta rodada:

- `compat/include/X11/X.h`
- `compat/include/X11/Xlib.h`
- `compat/include/X11/Xutil.h`
- `compat/include/X11/Xatom.h`
- `compat/include/X11/Xlocale.h`
- `compat/include/X11/keysymdef.h`
- `compat/include/X11/Xproto.h`

## Fase 2: Runtime Cliente

- [x] Criar `libX11` minima em `compat/` ou camada dedicada equivalente
- [x] Implementar `XOpenDisplay`, `XCloseDisplay`, `DefaultScreen`, `RootWindow`
- [x] Implementar criacao/destruicao/map de janela
- [x] Implementar surface/pixmap minima
- [x] Implementar `XNextEvent`, `XPending`, `XFlush`, `XSync`
- [x] Implementar `XSelectInput` para eventos basicos

Implementacao atual:

- `compat/src/x11/xlib.c`
- nesta rodada o cliente usa shim local sobre os syscalls graficos/input existentes
- isso destrava pilotos `Xlib` simples, mas ainda nao substitui o servidor isolado da fase seguinte

## Fase 3: Servidor Grafico Compat

- [ ] Criar servidor grafico compat isolado do compositor principal
- [ ] Traduzir janelas compat para janelas nativas do desktop
- [ ] Implementar tabela de recursos: window, pixmap, gc, cursor, atom
- [ ] Implementar roteamento de eventos para cliente correto
- [ ] Garantir isolamento de falha por processo cliente

Observacao desta rodada:

- o contrato wire ja existe em `headers/include/posix_gfx_compat.h`
- a implementacao ainda esta em modo cliente+shim local; o processo/servico separado continua pendente

## Fase 4: Desenho 2D

- [ ] Implementar `GC` basico
- [x] Implementar `XCreateGC`, `XFreeGC`, `XSetForeground`, `XSetBackground`
- [x] Implementar `XDrawPoint`, `XDrawLine`, `XDrawRectangle`, `XFillRectangle`
- [x] Implementar `XPutImage` e `XCopyArea` MVP
- [x] Implementar caminho previsivel para redraw/expose

Cobertura adicional MVP:

- `XDrawArc`, `XFillArc`, `XClearWindow`

## Fase 5: Entrada e Loop de Eventos

- [ ] Mapear teclado do `vibeOS` para keycodes/keysym compativeis
- [x] Mapear botoes e movimento de mouse
- [x] Implementar `Expose`, `KeyPress`, `KeyRelease`, `ButtonPress`, `ButtonRelease`, `MotionNotify`
- [ ] Implementar foco, enter/leave e close request basicos
- [ ] Integrar waits graficos com `select`/`poll` sem travar a sessao

Estado atual:

- teclado chega no cliente como keycode bruto do runtime atual
- foco inicial e `FocusIn`/`FocusOut` basicos existem no shim local
- ainda faltam `EnterNotify`, `LeaveNotify`, `WM_DELETE_WINDOW` e FD/wait real integravel a `poll`

## Fase 6: Janela, WM e Integracao

- [ ] Traduzir titulo, tamanho, posicao e hints basicos
- [ ] Implementar `WM_DELETE_WINDOW`
- [ ] Implementar atoms minimos para integracao com o desktop
- [ ] Definir como apps compat aparecem na taskbar e alternancia de foco
- [ ] Impedir que apps compat tomem controle indevido do desktop

## Fase 7: Fontes, Texto e Clipboard

- [ ] Implementar texto bitmap minimo ou shim previsivel
- [ ] Criar fallback estavel para `XDrawString`
- [ ] Implementar selecao/clipboard MVP
- [ ] Definir limites conhecidos para IME, UTF-8 complexo e fontes externas

## Fase 8: Ports Piloto

- [ ] Escolher 2 ou 3 apps pequenos `Xlib`-like como piloto
- [ ] Escolher 1 app interativo com menu/eventos
- [ ] Validar um browser ou viewer leve apenas depois dos pilotos
- [ ] Medir estabilidade no `i7` e na maquina mais lenta antes de expandir

## Fase 9: Robustez

- [ ] Garantir que matar cliente nao mata servidor grafico compat
- [ ] Garantir que matar servidor grafico compat nao mata o desktop nativo
- [ ] Adicionar limites de memoria, handles e fila de eventos
- [ ] Adicionar logging, tracing e diagnostico para deadlock/render

## Fase 10: Validacao

- [ ] Criar suite QEMU dedicada para apps graficos compat
- [ ] Validar abertura, redraw, input e fechamento
- [ ] Validar multiplas janelas
- [ ] Validar regressao entre maquinas rapidas e lentas
- [ ] Validar que apps compat nao derrubam a sessao principal

## Ordem de Execucao

- [x] Primeiro fechar `docs/abi-improvements.md`
- [x] Depois estabilizar `storage/AppFS` e a ultima validacao ABI pendente
- [X] So entao iniciar Fase 0 deste plano

## Definicao de Pronto

- [ ] Um app grafico estilo `Xlib` simples abre e desenha corretamente
- [ ] Eventos basicos de teclado e mouse funcionam
- [ ] Fechar o app nao derruba o desktop
- [ ] Travar o app nao derruba o desktop
- [ ] O caminho funciona nas maquinas lentas e nas rapidas
