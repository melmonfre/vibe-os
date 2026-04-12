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

- [ ] Inventariar apps/ports candidatos que hoje pedem `X11`
- [ ] Separar apps `Xlib` puros, apps `SDL`, apps `GLFW` e apps mistos
- [ ] Mapear o menor subconjunto de API necessario para os primeiros ports
- [ ] Definir o contrato entre cliente compat, servidor grafico compat e desktop nativo
- [ ] Documentar quais semanticas serao reais e quais serao shim/fallback

## Fase 1: ABI e Headers Graficos

- [ ] Publicar headers minimos estilo `X11/Xlib.h`, `X11/Xutil.h`, `X11/Xatom.h`
- [ ] Definir tipos, handles, eventos e IDs estaveis
- [ ] Versionar structs compartilhadas do protocolo grafico compat
- [ ] Definir politica de endian, alinhamento e padding do protocolo
- [ ] Reservar espaco para extensoes futuras sem quebrar ABI

## Fase 2: Runtime Cliente

- [ ] Criar `libX11` minima em `compat/` ou camada dedicada equivalente
- [ ] Implementar `XOpenDisplay`, `XCloseDisplay`, `DefaultScreen`, `RootWindow`
- [ ] Implementar criacao/destruicao/map de janela
- [ ] Implementar surface/pixmap minima
- [ ] Implementar `XNextEvent`, `XPending`, `XFlush`, `XSync`
- [ ] Implementar `XSelectInput` para eventos basicos

## Fase 3: Servidor Grafico Compat

- [ ] Criar servidor grafico compat isolado do compositor principal
- [ ] Traduzir janelas compat para janelas nativas do desktop
- [ ] Implementar tabela de recursos: window, pixmap, gc, cursor, atom
- [ ] Implementar roteamento de eventos para cliente correto
- [ ] Garantir isolamento de falha por processo cliente

## Fase 4: Desenho 2D

- [ ] Implementar `GC` basico
- [ ] Implementar `XCreateGC`, `XFreeGC`, `XSetForeground`, `XSetBackground`
- [ ] Implementar `XDrawPoint`, `XDrawLine`, `XDrawRectangle`, `XFillRectangle`
- [ ] Implementar `XPutImage` e `XCopyArea` MVP
- [ ] Implementar caminho previsivel para redraw/expose

## Fase 5: Entrada e Loop de Eventos

- [ ] Mapear teclado do `vibeOS` para keycodes/keysym compativeis
- [ ] Mapear botoes e movimento de mouse
- [ ] Implementar `Expose`, `KeyPress`, `KeyRelease`, `ButtonPress`, `ButtonRelease`, `MotionNotify`
- [ ] Implementar foco, enter/leave e close request basicos
- [ ] Integrar waits graficos com `select`/`poll` sem travar a sessao

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
- [ ] Depois estabilizar `storage/AppFS` e a ultima validacao ABI pendente
- [ ] So entao iniciar Fase 0 deste plano

## Definicao de Pronto

- [ ] Um app grafico estilo `Xlib` simples abre e desenha corretamente
- [ ] Eventos basicos de teclado e mouse funcionam
- [ ] Fechar o app nao derruba o desktop
- [ ] Travar o app nao derruba o desktop
- [ ] O caminho funciona nas maquinas lentas e nas rapidas
