# Known Bugs

Este documento lista os problemas conhecidos mais críticos na base atual.
A migração para microkernel foi concluída, mas ainda existem regressões em várias áreas do desktop.

## Status geral

- Migração para microkernel: concluída
- Estabilidade de vídeo: instável em alguns casos de entrada e apresentação
- Áudio: parcialmente restaurado, mas ainda com interferência, artefatos e regressões no player/runtime
- Rede: implementação incompleta / falta de integração completa
- Jogos: parte dos problemas de launch já foi corrigida, mas ainda existem gaps de assets/caminhos

## Bugs atuais

### [x] 1. Teclado estava saindo do modo de vídeo para o shell e causando piscar de tela

- Comportamento observado: no boot para desktop, o sistema lançava `startx`, mas o `userland.app` seguia entrando em `shell_start()`, deixando shell e desktop consumirem teclado ao mesmo tempo.
- Causa raiz corrigida: o conflito de input não era só vídeo; o shell de boot continuava vivo no caminho desktop e competia pelo mesmo stream de teclado, provocando escape de foco e flicker de apresentação.
- Correção aplicada: o `userland.app` agora faz handoff para o desktop e retorna logo após um `sys_launch_app(\"startx\")` bem-sucedido, sem iniciar o shell interativo no mesmo boot path.

### 2. Som não está saindo

- Comportamento: o desktop inicia normalmente, mas não há áudio de sistema ou de apps.
- Sintoma: mixagem de áudio inativa e sem saída de backend.
- Áreas afetadas: reprodução de áudio do desktop, apps de som e notificações.

### 3. Internet / rede está incompleta

- Comportamento: a página de configuração de rede abre, mas a integração de scan/conexão não está completa.
- Sintoma: não é possível conectar de forma confiável a redes Wi-Fi ou aplicar perfis salvos.
- Áreas afetadas: applet de rede, backend de gerenciamento de perfil e reconciliação de status.

### 4. Doom não encontra os arquivos do jogo

- Comportamento: ao iniciar Doom, o app falha na localização dos assets do port.
- Sintoma: erro de "arquivo não encontrado" ou carregamento incompleto do jogo.
- Possível causa: caminhos de dados estáticos não resolvidos ou assets não empacotados corretamente.

### [x] 5. Craft estava alterando as cores do desktop

- Comportamento observado: o jogo Craft modificava a paleta global durante a execução e podia sair sem restaurar o estado anterior.
- Causa raiz corrigida: o lifecycle de encerramento zerava `started/running` no caminho de saída por frame antes de executar o shutdown real, então `craft_upstream_stop()` podia não rodar e a restauração da paleta não acontecia.
- Correção aplicada: o encerramento foi consolidado em um único caminho de cleanup em `craft_finish_run()`, usado tanto no fechamento explícito quanto na saída natural do jogo, garantindo a chamada de `craft_upstream_stop()` antes de limpar o estado.

### [x] 6. Ports BSD de jogos abriam um terminal em vez de iniciar o jogo

- Comportamento observado: os itens do menu Start para ports BSD eram tratados como `APP_TERMINAL` e abriam um terminal com o comando do jogo, em vez de lançar o app modular diretamente.
- Causa raiz corrigida: o caminho de `launch_start_menu_entry()` redirecionava qualquer entrada com `command` para `desktop_request_open_terminal_command()`, inclusive os jogos BSD de uma palavra só.
- Correção aplicada: entradas de jogos BSD do Start Menu agora são detectadas como apps destacados e lançadas via `sys_launch_app_argv()`; se o launch falhar, só aí o desktop cai no fallback de terminal.

### [x] 7. Audio Player tinha sumido da busca/menu do desktop

- Comportamento observado: o player gráfico continuava existindo no código, mas o item do menu/busca deixava de aparecer como `Audio Player`, o que na prática fazia parecer que o app tinha sido removido.
- Causa raiz corrigida: o atalho do menu havia sido renomeado para `Som`, quebrando a encontrabilidade por busca e a expectativa da UI.
- Correção aplicada: o item voltou a se chamar `Audio Player`, com metadata compatível com a busca do menu e preservando o launch do app gráfico.

### [x] 8. Som de boot não tocava no bootloader

- Comportamento observado: o `assets/vibe_os_boot.wav` não tocava no `stage2`; o único "boot sound" real acontecia depois, já no `init` do sistema.
- Causa raiz corrigida: o asset de boot não fazia parte do caminho da partição FAT do loader, e o `stage2` não tinha pipeline próprio para carregar e tocar o áudio antes do countdown.
- Correção aplicada: o bootloader agora gera/carrega `VIBEBOOT.RAW` a partir do WAV de boot e o reproduz no `stage2` logo após renderizar o fundo do loader, antes de iniciar o contador.

## Observações

- Esses bugs devem ser priorizados na ordem em que impactam mais diretamente a experiência do usuário.
- A migração para microkernel está concluída, então o foco agora é estabilizar a camada de apresentação, entrada, áudio e integração de apps.
- Em áudio, tratar como "resolvido" apenas o que já ficou reproduzível e verificável; o problema macro de interferência/artefato no `compat-azalia` continua aberto.
- Documentar cada item com reprodução exata e local de código ajuda no rastreamento e correção.
