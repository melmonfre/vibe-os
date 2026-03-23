# Plano de Integracao de Apps Modulares

Data da auditoria: 2026-03-23

## Objetivo

Integrar o catalogo de apps do VibeOS para que:

- tudo que for launchable em `userland/applications/` entre no projeto como app modular AppFS
- tudo que existir em `applications/ported/` vire app modular ou seja explicitamente bloqueado por build
- a shell externa (`userland.app`) consiga carregar esses apps corretamente
- o fluxo `startx` funcione a partir da shell modular, sem depender do payload monolitico antigo

## Escopo assumido

Para este plano, "app modular" significa binario AppFS com `vibe_app_main(...)`, empacotado no catalogo externo e executavel via shell.

Nao entra como app standalone:

- `userland/modules/*`
- `userland/bootstrap_*`
- `userland/lua/*` e `userland/sectorc/*` como arquivos internos de runtime
- arquivos auxiliares de `doom_port/`, `craft/` e arvores vendorizadas

Esses itens continuam como runtime/shared code para apps launchables.

## Estado auditado hoje

### Ja funcionando

- [x] O boot minimo do microkernel entra no `init` bootstrap e carrega `userland.app` do AppFS.
- [x] O log serial de `make run-headless-debug` em 2026-03-23 chegou a `userland.app: shell start`.
- [x] A shell modular usa `lang_try_run(...)` para carregar apps externos do catalogo AppFS.
- [x] O AppFS atual esta sendo empacotado e lido com sucesso no boot.
- [x] O VFS inicial cria `/bin`, `/usr/bin` e `/compat/bin` para o fluxo de comandos externos.
- [x] Os seguintes apps modulares ja estao no catalogo AppFS atual:
  - `hello`, `js`, `ruby`, `python`, `java`, `javac`
  - `userland`
  - `lua`, `sectorc`, `startx`, `edit`, `nano`
  - `echo`, `cat`, `wc`, `pwd`, `head`, `sleep`, `rmdir`, `mkdir`, `tail`, `grep`, `loadkeys`, `true`, `false`, `printf`
- [x] O build atual empacota 26 entradas no AppFS, todas verificadas na imagem `build/data-partition.img`.
- [x] O shell ja prefere alguns apps externos em vez de stubs internos (`echo`, `cat`, `pwd`, `mkdir`, `true`, `false`, `printf`).
- [x] O diretorio AppFS agora fica cacheado apos a primeira leitura valida, evitando reler o catalogo em toda execucao externa.
- [x] O bootstrap textual agora aponta para `help` e para os atalhos graficos reais (`startx`, `edit`, `nano`), sem anunciar o fluxo antigo `apps/run`.

### Parcialmente pronto

- [x] `applications/ported/` ja tem pipeline de build dedicado em `Build.ported.mk`.
- [x] 14 apps portados ja entram no AppFS e na shell modular.
- [x] `startx.app` agora existe como launcher grafico externo no AppFS.
- [x] `edit.app` e `nano.app` agora existem como launchers externos dedicados no AppFS, reusando o desktop modular.
- [ ] `applications/ported/sed` ainda nao compila no pipeline atual e por isso nao entra no AppFS.

### Ainda faltando

- [ ] `startx` ainda nao funciona a partir da `userland.app` modular.
- [ ] `edit` e `nano` ainda nao estao validados interativamente a partir da `userland.app` modular.
- [ ] `cc` ainda nao esta validado end-to-end como alias externo do `sectorc.app`.
- [ ] Os apps de `userland/applications/` ainda nao sao empacotados como apps AppFS independentes.
- [x] O banner textual agora reflete o fluxo modular atual com precisao suficiente para bootstrap.
- [x] O catalogo de apps agora vem de um manifesto unico (`config/app_catalog.tsv`) gerado para build/shell/stubs.

## Gaps tecnicos identificados

- `userland.app` atual inclui shell/fs/loader, mas nao inclui `desktop.c`, `ui.c`, `lua`, `sectorc` nem os apps graficos launchables.
- `cmd_startx()`/`cmd_edit()`/`cmd_nano()` no shell modular dependem do AppFS externo para validacao end-to-end; o empacotamento agora existe, mas ainda falta smoke test interativo no QEMU.
- `desktop.c` ainda centraliza o ciclo de vida de terminal, editor, jogos e ferramentas em um desktop monolitico.
- `Build.ported.mk` ja consegue achar `applications/ported/sed/config.h`, mas o port ainda falha por dependencias gnulib/dfa incompletas (`xalloc.h` e `dfa.h`).
- O AppFS aceita no maximo `48` entradas e nomes de ate `16` bytes; a modularizacao completa precisa respeitar ou ampliar esse limite.
- Existem arquivos duplicados com espaco no nome em `userland/applications/games/craft/* 2.c`; eles nao devem entrar no manifesto.

## Plano de implementacao

### Fase 1 - Manifesto unico de apps

- [x] Criar um manifesto unico em `docs/` + `Makefile` para descrever cada app modular:
  - nome AppFS
  - comando shell
  - origem
  - heap sugerido
  - tipo (`cli`, `gui`, `session`, `runtime`)
  - aliases/stubs de VFS
- [x] Parar de manter `LANG_APP_BINS` e stubs de VFS como listas soltas espalhadas.
- [x] Gerar `AppFS`, stubs `/bin`/`/usr/bin`/`/compat/bin` e help da shell a partir desse manifesto.

Conclusao da fase:

- [x] Um unico lugar descreve tudo que deve virar app modular.

### Fase 2 - Fechar os gaps de shell modular

- [x] Publicar `lua` como app externo.
- [x] Publicar `sectorc` como app externo e manter `cc` como alias do mesmo app.
- [x] Criar `startx.app` como launcher grafico externo.
- [x] Criar wrappers externos para `edit` e `nano` apontando para o fluxo grafico/editor correto.
- [x] Corrigir help/banner para listar apenas o que existe de fato.

Conclusao da fase:

- [x] A shell modular nao anuncia comando inexistente.
- [ ] `startx`, `lua`, `sectorc`, `cc`, `edit` e `nano` saem da categoria "indisponivel".

### Fase 3 - Modularizar `userland/applications/`

- [ ] Definir ABI interna para apps graficos launchables:
  - init
  - tick/update
  - input
  - draw
  - shutdown
- [ ] Extrair do desktop o suficiente para transformar cada app launchable em modulo reutilizavel, sem duplicar `ui` e sem relinkar tudo em um app gigante.
- [ ] Publicar como apps externos, no minimo:
  - `terminal`
  - `clock`
  - `filemanager`
  - `editor`
  - `taskmgr`
  - `calculator`
  - `sketchpad`
  - `snake`
  - `tetris`
  - `pacman`
  - `space_invaders`
  - `pong`
  - `donkey_kong`
  - `brick_race`
  - `flap_birb`
  - `doom`
  - `craft`
  - `personalize`
- [ ] Decidir se `desktop` sera um app proprio (`desktop.app`) ou apenas backend do `startx.app`.

Conclusao da fase:

- [ ] Tudo que hoje e launchable em `userland/applications/` passa a existir no AppFS com comando claro.

### Fase 4 - Fechar `applications/ported/`

- [ ] Fazer `sed` compilar no pipeline atual.
- [ ] Garantir que todo diretorio launchable em `applications/ported/` gere `.app`.
- [ ] Gerar automaticamente stubs/aliases para todos os ports empacotados.
- [ ] Atualizar a shell para preferir sempre o port real quando ele existir.

Conclusao da fase:

- [ ] `applications/ported/` fica 100% coberto, exceto `include/`.

### Fase 5 - Catalogo, limites e empacotamento

- [ ] Recontar o numero final de entradas AppFS apos modularizar `userland/applications/`.
- [ ] Se necessario, elevar `VIBE_APPFS_ENTRY_MAX` acima de `48`.
- [ ] Revisar nomes para caber no limite atual de `16` bytes por app.
- [ ] Verificar se `VIBE_APPFS_APP_AREA_SECTORS` continua suficiente apos incluir GUI apps maiores.

Conclusao da fase:

- [ ] O catalogo modular completo cabe na imagem sem truncamento ou overflow.

### Fase 6 - Validacao em QEMU

- [x] Validado em 2026-03-23: boot chega ao `userland.app` shell pelo caminho modular.
- [x] Validado em 2026-03-23: `startx.app` esta empacotado no AppFS e o boot modular continuou chegando ao shell apos essa inclusao.
- [x] Validado em 2026-03-23: `edit.app` e `nano.app` estao empacotados no AppFS e o boot modular continuou chegando ao shell apos essa inclusao.
- [ ] Validar `startx` a partir da shell modular.
- [ ] Validar `edit` e `nano` a partir da shell modular.
- [ ] Validar terminal grafico executando apps portados e runtimes externos.
- [ ] Validar paths explicitos como `/bin/java`, `/compat/bin/grep` e aliases shell.
- [ ] Validar que DOOM/Craft continuam enxergando assets apos a modularizacao.
- [ ] Rodar smoke test por app e registrar status no mesmo documento.

Conclusao da fase:

- [ ] O fluxo shell -> app externo -> retorno ao shell esta confiavel para CLI e GUI.

## Ordem recomendada de execucao

1. Fechar manifesto unico.
2. Publicar `lua`, `sectorc`, `cc`, `startx`, `edit`, `nano`.
3. Corrigir `sed`.
4. Modularizar `terminal`, `editor`, `filemanager`, `clock`, `taskmgr`, `calculator`, `sketchpad`.
5. Modularizar jogos leves.
6. Modularizar `doom` e `craft`.
7. Recontar AppFS/espaco e validar no QEMU.

## Definicao de pronto

Este plano so podera ser considerado concluido quando:

- [ ] o boot modular cair sempre em `userland.app`
- [ ] `startx` funcionar a partir da shell modular
- [ ] todo app launchable de `userland/applications/` estiver no AppFS
- [ ] todo app launchable de `applications/ported/` estiver no AppFS ou bloqueado com motivo documentado
- [ ] a shell listar e resolver corretamente os apps reais
- [ ] a validacao em QEMU estiver documentada com evidencias objetivas
