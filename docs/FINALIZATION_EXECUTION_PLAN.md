# Plano Consolidado de Finalizacao do VibeOS
marcar com x o que estiver concluido
Data da consolidacao: 2026-03-29

## Premissas desta consolidacao

- `VibeLoader` deve ser tratado como 100% funcional para o plano principal. O que ficou em `docs/VIBELOADER_PLAN.md` e `docs/VIBELOADER_BIOS_DEBUG_HANDOFF.md` entra como backlog documental/diagnostico, nao como bloqueio de entrega.
- O kernel continua sendo o `VibeKernel`; a meta de compatibilidade 1:1 vale para drivers, servicos, userland e apps herdados de `compat`.
- O objetivo deste documento e juntar o que ainda falta nos outros `.md` em uma fila executavel, sem misturar backlog opcional com bloqueio real.

## O que ja pode sair da frente

- Bootloader / pipeline de boot: tratado como fechado.
- Base AppFS / modularizacao principal: funcional, com pendencias de smoke e aliases.
- GPU em QEMU: suficientemente valida para nao ser o primeiro bloqueio de entrega.
- Port base de apps CLI e jogos de `compat`: ja existe massa critica boa o bastante para mudar o foco para hardware, UX e integracao.

## O que ainda falta de verdade

### Bloco 1: rede e internet real

Origem principal:
- `docs/NETWORK_AUDIO_PANEL_PLAN.md`
- `docs/COMPAT_PLAN.md`
- `docs/COMPAT_PLAN2.md`

Falta fechar:
- driver cabeado real com attach/enum/pacotes de verdade vindos de `compat`
- DHCP + DNS funcionando
- caminho de socket/rede real em vez de control-plane parcial
- Wi-Fi com scan, senha e conexao
- navegador integrado ao desktop usando a rede real
- conjunto minimo de comandos de internet/diagnostico no terminal
- port do `links2` como browser de terminal com modo grafico padrao (`links2 -g`)

Comandos-alvo de terminal para considerar a internet "utilizavel":
- `ifconfig`
- `ping`
- `route`
- `netstat`
- `host`
- `dig`
- `ftp`
- `curl`

Origem atual no tree:
- `compat/sbin/ifconfig/ifconfig.c`
- `compat/sbin/ping/ping.c`
- `compat/sbin/route/route.c`
- `compat/usr.bin/netstat/netstat.h`
- `compat/usr.bin/netstat/route.c`
- `compat/usr.bin/dig/dig.c`
- `compat/usr.bin/dig/host.c`
- `compat/usr.bin/ftp/ftp.c`

Observacao importante:
- `curl` nao esta pronto no `compat` local como os demais comandos acima; ele entra como port/import dedicado e nao como simples reaproveitamento igual a `ping`/`ifconfig`.
- `links2` ja existe no tree em `userland/applications/network/links2`; falta transformar isso em port/build/runtime integrado.

### Bloco 2: audio real em hardware

Origem principal:
- `docs/NETWORK_AUDIO_PANEL_PLAN.md`

Falta fechar:
- `compat-azalia` robusto em notebook real
- captura real validada fora do fallback de QEMU
- `compat-uaudio` sair de substrate/MVP e virar backend confiavel
- matriz real de hardware com backend certo por maquina

### Bloco 3: video nativo e validacao em hardware

Origem principal:
- `docs/VIDEO_BACKEND_REWRITE_PLAN.md`

Falta fechar:
- primeiro backend real de hardware promovido com seguranca
- validacao widescreen real
- mais testes fora do QEMU
- decidir escopo minimo real de i915/radeon/nouveau sem inflar promessa

### Bloco 4: fechamento de integracao modular

Origem principal:
- `docs/MODULAR_APP_INTEGRATION_PLAN.md`

Falta fechar:
- smoke test por app
- aliases e paths explicitos (`/bin/...`, `/compat/bin/...`)
- assets de DOOM/Craft na matriz completa

### Bloco 5: compat / runtime / POSIX que ainda falta

Origem principal:
- `docs/COMPAT_PLAN.md`
- `docs/COMPAT_PLAN2.md`
- `docs/java.md`

Falta fechar:
- semantica POSIX faltante em pontos do VFS/runtime
- execucao nativa de binarios a partir do VFS quando isso deixar de depender do caminho atual de AppFS
- dependencias de plataforma para JVM

### Bloco 6: SMP e robustez multiprocessada

Origem principal:
- `docs/smp.md`

Falta fechar:
- validacao confiavel de `2+ CPUs` em QEMU/hardware
- decidir se o proximo degrau e suporte a topologia ACPI/MP mais amplo ou outra forma de detectar SMP nas maquinas alvo
- endurecer o que ja entrou para nao ficar dependente de um unico ambiente de QEMU

### Bloco 7: UX de input e desktop

Origem principal:
- observacao do tree atual
- necessidade nova deste pedido

Falta fechar:
- [X] scroll wheel do mouse no kernel
- [X] scroll wheel atravessando syscall/ABI ate o userland
- [X] scroll funcionando de verdade nas interfaces

### Bloco 8: migracao para arquitetura assincrona orientada a eventos

Origem principal:
- `docs/MICROKERNEL_MIGRATION.md`
- problemas reais vistos em `make run` com audio/input/video no desktop

Falta fechar:
- transformar `yield/sleep + poll` em um modelo de eventos/waitables de verdade
- tirar audio, video, input e storage do caminho sincrono visivel para a UI
- fazer cada servico ter fila propria, progresso proprio e falha isolada
- parar de depender de `backend-shim` em estado estavel
- separar de vez:
  - captura de input
  - loop do desktop/compositor
  - apresentacao de video
  - playback/captura de audio
  - I/O de storage/filesystem

Checklist minimo para dizer "agora esta na arquitetura certa":
- [X] existe primeira ABI async para audio (`AUDIO_WRITE_ASYNC`)
- [~] existem primitivas de evento/waitable/cancelamento no kernel
: agora com `waitable`, `signal`, `completion`, timeout/cancelamento, metadata de espera visivel no scheduler, stream de eventos de estado por servico, supervisor consumindo esses eventos no boot e polling nao bloqueante em userland; ainda falta a camada de completion de alto nivel para audio/video/storage/network
- [~] audio publica conclusao/progresso por evento, nao por poll oportunista
: agora existe stream de eventos `queued/idle/underrun` saindo do servico de audio para userland, o task manager observa isso e o helper async de WAV usa o evento `idle` no caminho kernel-async; ainda resta mover ownership/completion steady-state para fora da ponte de compatibilidade do kernel
- [~] input vira publicacao de eventos, nao fallback permanente em leitura direta
- [~] video ganha fila de present/fence
: agora existe stream de eventos de video (`present`/`mode-set`/`leave`) saindo de `videosvc` para userland, o task manager observa esse progresso, e o desktop ja usa um `present submit` com `sequence` de retorno no caminho principal; a fila real com worker dedicado ainda esta pendente
- [ ] filesystem/storage ganham fila de I/O e writeback
- [~] network ganha readiness/eventos de socket reais
: agora existe ABI de eventos de rede com `subscribe/receive`, o servico publica transicoes de link e notificacoes `recv` / `accept` / `send` / `closed`, e o task manager observa esse stream; o datapath de NIC extraido ainda continua pendente
- [ ] queda/restart de um servico nao congela desktop
- [ ] `backend-shim` sai do caminho principal e fica no maximo como rescue/boot bridge

Avanco atual em desktop/startx:
- [~] supervisao de sessao desktop/startx saiu do poll por snapshot e entrou em espera por evento de ciclo de vida de tarefa
: o scheduler agora publica eventos `launched/terminated`, o `desktop-host` reaprende a sessao `startx` bloqueando em `task_event_receive()` em vez de rodar `sys_task_snapshot()` em loop, o shell foreground agora sobe apps modulares em um `app-runtime` dedicado, `shell-host` agora sobe a sessao shell em uma tarefa separada, e o fallback de `startx-host` / `desktop-host` agora tambem sobe a sessao desktop como tarefa separada em vez de chamar `desktop_main()` inline; o `lang_loader` e o bootstrap do desktop agora tambem chegam ate `desktop: session ready` depois de bulk-read no AppFS e startup diferido de wallpaper/catalogo, mas ainda falta fechar o ultimo gate do shortcut/input smoke headless e estender o mesmo modelo para payloads maiores/richer argv e para restart/isolamento de servicos fora do host do desktop
- [~] restart de `inputsvc` ja nao prende o desktop no PID morto nem deixa novos `app-runtime` sem primeiro slice
: o caminho interativo do desktop agora foi puxado de volta para a fila local unificada de input do kernel, preservando os eventos de restart/degradacao de `inputsvc` sem depender do reply atual do transporte; falta transformar isso em gate verde estavel de validacao e depois fechar o ownership steady-state dentro do proprio `inputsvc`
- [~] smoke de restart/launch modular no desktop agora usa atalhos curtos dedicados (`ctrl+k` / `ctrl+u` / `ctrl+v` / `ctrl+w` / `ctrl+l`) e tambem entradas click-driven no menu iniciar
: isso ja codifica restart de `input`, `audio`, `video` e `network` no tree e deixa o gate de continuidade de teclado+mouse mais acessivel para smoke manual/automatizado, mas ainda falta voltar esse caminho a verde na validacao atual sob QEMU headless e depois repetir a prova final no notebook/alvo real

Hierarquia de prioridade obrigatoria:
1. desktop
2. teclado
3. mouse
4. video/compositor/present
5. storage/filesystem do foreground
6. audio
7. rede/daemons
8. tarefas opcionais em background

Regra dura:
- tudo deve rodar separado, mas servico de prioridade menor nunca pode travar um de prioridade maior
- prioridade `5+` nao pode comprometer boot nem `startx`; se um worker/app dessa faixa travar durante a migracao, ele deve ser morto antes de sacrificar as prioridades `1..4`

## Ordem recomendada para finalizar

### Etapa 0: congelar o que ja esta bom

- tratar `VibeLoader` como fechado e parar de gastar ciclo no bootloader
- manter GPU/QEMU/AppFS verdes durante qualquer rodada grande
- toda rodada grande precisa terminar com `boot.img` gerada e pelo menos smoke de boot

### Etapa 1: melhorar UX base imediatamente

- [X] implementar scroll wheel no mouse
- [X] plugar scroll no desktop, start menu, listas e dialogs
- usar isso como melhoria de usabilidade e como exercicio de ABI/input sem risco alto

### Etapa 2: fechar audio de hardware real

- atacar `compat-azalia` ate sair do estado fraco em notebook real
- promover `compat-uaudio` para fallback USB util de verdade
- consolidar a ordem de fallback por backend
- antes de seguir fundo em playback/captura, fechar a base de eventos/waitables e tirar o desktop do caminho quente do playback

### Etapa 3: fechar rede real

- transformar o caminho atual de Ethernet em datapath real
- subir DHCP e DNS
- portar o conjunto base de comandos de terminal de rede (`ifconfig`, `ping`, `route`, `netstat`, `host`, `dig`, `ftp`, `curl`)
- padronizar aliases e local de instalacao para eles no terminal
- depois subir Wi-Fi e UX de senha/conexao
- por fim integrar navegador real ao desktop
- incluir `links2` como browser de terminal e fazer sites abrirem por padrao com `links2 -g`

### Etapa 4: fechar validacao modular e runtime

- smoke per-app
- aliases e paths explicitos
- jogos com assets
- reduzir gaps POSIX mais gritantes

### Etapa 5: fechar hardware/video/SMP

- promover um backend de video real com escopo curto e validado
- fechar a matriz de modos reais
- retomar SMP com deteccao/topologia suficiente para `2+ CPUs` no ambiente de validacao

### Etapa 6: cortar a ponte hibrida e virar microkernel de verdade

- introduzir waitables/eventos/cancelamento no kernel
- transformar `init` em supervisor puro e manter `shell`/`desktop` em hosts separados
- mover `audio`, `video`, `input`, `storage/filesystem` e `network` para datapath assíncrono orientado a eventos
- fazer restart isolado de servico sem fallback destrutivo para UI
- reduzir o kernel privilegiado ao nucleo duro:
  - scheduler
  - memoria/VM
  - IPC
  - interrupcoes
  - supervisao
  - mediacao minima de hardware

## Definicao pratica de "acabou"

O projeto pode ser considerado fechado quando, ao mesmo tempo:

- Ethernet sobe, recebe lease e resolve DNS
- terminal consegue diagnosticar e usar internet com `ifconfig`, `ping`, `route`, `netstat`, `host`/`dig`, `ftp` e `curl`
- Wi-Fi lista redes, pede senha e conecta
- audio toca em QEMU e em hardware real no backend correto
- fallback USB audio e util de verdade
- navegador abre pelo desktop e usa a rede real
- `links2` funciona no terminal e a abertura padrao de sites usa `links2 -g`
- apps modulares principais passam em smoke
- video sobe em QEMU e em pelo menos um backend real de hardware
- mouse scroll funciona no desktop e nas listas/dialogs principais
- desktop, teclado, mouse, video e audio continuam vivos mesmo com falha/restart de servico
- apps comuns rodam abaixo de `network` na ordem de prioridade; shell/desktop continuam sendo classe separada
- o caminho principal deixa de depender de bridge `backend-shim`
- o kernel fica reduzido ao que faz sentido num microkernel de verdade

## Checklist "microkernel de verdade"

- [ ] nenhum caminho de UI depende de escrita sincrona em device/backend para continuar rodando
- [ ] audio e captura funcionam em fila assíncrona com conclusao por evento
- [ ] input chega por publicacao de eventos e fila por dispositivo
- [ ] video apresenta frames por fila/fence e nao por trabalho pesado no loop do desktop
- [ ] storage/filesystem usam workers de I/O/writeback
- [~] network tem readiness/eventos reais para sockets
- [ ] apps comuns nao passam na frente de `network`; shell/desktop ficam em classe superior separada
- [~] `init` ja comeca a lancar `shell-host` / `desktop-host` separados; falta estender isso para apps modulares AppFS
- [ ] `backend-shim` saiu do steady state
- [ ] queda de um servico nao derruba os demais
- [ ] `make run` com `2+ CPUs` continua responsivo com som, input e video ativos

## Plano especifico: scroll do mouse

### Meta

Adicionar wheel scroll no caminho completo:

- controlador PS/2 ou outro backend de mouse
- syscall/ABI
- desktop/UI
- apps que dependem de lista/viewport

### Estado atual

- `mouse_state` so carrega `x`, `y`, `dx`, `dy` e `buttons`
- o driver PS/2 do kernel usa pacote de 3 bytes
- o desktop ja possui varios pontos de scroll manual por drag/teclado, mas nao recebe delta de wheel

### Etapas tecnicas

#### Fase A: kernel/input

- [X] detectar e habilitar protocolo de wheel no mouse PS/2 (`IntelliMouse`, pacote de 4 bytes)
- [X] estender `mouse_state` com delta de wheel
- [X] garantir que a fila de eventos preserve esse campo

#### Fase B: ABI/syscall

- [X] propagar o novo campo em `headers/include/userland_api.h`
- [X] atualizar syscall/input service/runtime sem quebrar o polling atual
- [X] manter compatibilidade com mouse sem wheel

#### Fase C: desktop e UI base

- [X] ligar wheel no start menu
- [X] ligar wheel em listas de apps/resultados
- [ ] ligar wheel em file dialogs
- [X] ligar wheel em areas com scroll futuro do file manager/editor/terminal quando houver viewport/lista adequada
: file manager e lixeira agora respondem a wheel; editor/terminal continuam pendentes quando houver viewport/lista adequada

#### Fase D: apps e compat wrappers

- [X] verificar jogos/apps que ja esperam callback de scroll
- [X] avaliar se o compat de GLFW deve expor wheel para apps como Craft

#### Fase E: validacao

- [ ] QEMU com wheel
- [ ] mouse USB/PS2 real
- [ ] teste de regressao de clique/movimento sem wheel

### Criterio de pronto para scroll

- [X] wheel sobe no kernel sem quebrar movimento/clique
- [ ] desktop responde a scroll em pelo menos start menu, listas e dialogs
- [ ] regressao zero para mouse sem wheel

## Primeira sequencia de execucao recomendada

1. [X] Implementar scroll wheel do mouse.
2. Fechar `compat-azalia` no hardware real alvo.
3. Implementar bloqueio real de tarefas e consertar `sleep` para estabilizar multitarefa, desktop e audio em single-core.
4. Fazer Ethernet real com DHCP/DNS.
5. Portar os comandos de rede/internet do terminal e integrar `links2 -g`.
6. Fechar smoke modular e gaps POSIX mais visiveis.
7. Voltar para video real e SMP com matriz de hardware/QEMU mais honesta.

## Plano especifico: terminal de internet

### Meta

Chegar num terminal que seja util para diagnostico e uso real da internet, no estilo BSD/Linux:

- `ifconfig` para interfaces e enderecamento
- `ping` para conectividade
- `route` para tabela/rota default
- `netstat` para sockets/estatisticas
- `host` e `dig` para DNS
- `ftp` para transferencia simples
- `curl` para HTTP/HTTPS e automacao
- `links2` como browser de terminal

### Ordem recomendada

#### Fase A: diagnostico basico

- `ifconfig`
- `ping`
- `route`
- `netstat`

#### Fase B: DNS e resolucao

- `host`
- `dig`

#### Fase C: transferencia e web

- `ftp`
- `curl`
- `links2`

### Plano de integracao do links2

- usar o codigo ja presente em `userland/applications/network/links2`
- criar um port/build controlado em vez de depender do `configure` legado cru
- priorizar primeiro o caminho minimo necessario para abrir sites
- padronizar a invocacao principal como `links2 -g`
- tratar `links2` como app de terminal: o fluxo normal do usuario deve ser chamar pelo shell ou via launch helper textual

### Criterio de pronto para terminal de internet

- `ifconfig` e `ping` funcionam em QEMU e em pelo menos uma maquina real
- `host`/`dig` confirmam DNS real
- `curl` baixa uma URL real e suporta HTTPS no minimo basico necessario
- `links2 -g https://example.org` abre com sucesso no ambiente suportado
