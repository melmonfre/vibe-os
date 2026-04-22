# Icon Theme Implementation Plan

## Objetivo

Integrar o tema de icones existente em `assets/icons` ao desktop do `vibeOS` de forma incremental, com fallback previsivel, baixo custo de memoria e sem depender de formatos que o runtime ainda nao suporta.

## Estado Atual

- [x] O repositorio ja contem um tema completo em `assets/icons`
- [x] O tema ja possui `index.theme`
- [x] A arvore inclui contextos uteis para desktop real: `apps`, `places`, `status`, `notifications`, `panel`, `devices`, `mimes`, `emblems`
- [x] O runtime atual ja decodifica `png` e `bmp`
- [ ] O runtime atual nao resolve icones por nome/contexto/tamanho
- [ ] O runtime atual nao expoe API de tema de icones
- [ ] O desktop atual nao persiste tema de icones em `ui.cfg`
- [ ] O runtime atual nao suporta `svg`
- [ ] O runtime atual nao suporta `xpm`
- [ ] Os assets de `assets/icons` ainda nao entram no caminho de assets registrados em `userland/modules/fs.c`

## Regra Principal

- [ ] O primeiro merge deve entregar um MVP funcional para o desktop nativo
- [ ] O MVP deve usar apenas formatos ja suportados pelo runtime (`png` e, se necessario, `bmp`)
- [ ] `svg` e `xpm` so entram quando houver necessidade real ou pipeline de conversao offline
- [ ] Nenhuma tela do sistema pode depender de um icone especifico para continuar operando
- [ ] Toda busca de icone deve ter fallback deterministicamente reproduzivel

## Escopo do MVP

- [ ] Resolver icones por nome e tamanho
- [ ] Carregar icones `png` em memoria paletizada do desktop
- [ ] Integrar icones nas entradas principais do desktop e do menu iniciar
- [ ] Integrar icones basicos do tray/status para rede e audio
- [ ] Persistir o nome do tema selecionado
- [ ] Garantir fallback para `application-default-icon`, `folder`, `folder_open`, `user-trash`, `audio-volume-*`, `network-*`

## Fora de Escopo Inicial

- [ ] Suporte completo a `svg`
- [ ] Parsing/renderizacao de `xpm`
- [ ] Herdanca complexa entre temas tipo `Inherits=hicolor`
- [ ] Cache em disco estilo `gtk-update-icon-cache`
- [ ] Tema de cursor
- [ ] Troca hot-reload entre muitos temas de terceiros
- [ ] Cobertura imediata de toda a arvore `extras/`

## Inventario Inicial do Tema

- [x] O tema parece derivado de um pacote estilo freedesktop/classico (`BeOS-r5-Icons`)
- [x] A arvore tem diretorios fixos por contexto e tamanho, alinhados ao modelo de `index.theme`
- [x] Ha mistura de `png`, `svg` e `xpm`
- [x] Ha tamanhos uteis para o desktop atual: `16`, `22`, `24`, `32`, `48`, `64`, `128`, `256`
- [ ] Precisamos consolidar quais icones sao realmente exigidos pelo desktop atual antes de tentar cobrir tudo

## Fase 0: Fechar Contrato do MVP

- [ ] Definir o nome canonico do tema embarcado
- [ ] Escolher a raiz logica do tema dentro do FS do sistema, preferencialmente `/assets/icons`
- [ ] Definir a politica de selecao de tamanho: exato primeiro, depois maior mais proximo, depois menor mais proximo
- [ ] Definir se o contexto sera obrigatorio ou apenas sugestao para a busca
- [ ] Definir lista minima de nomes obrigatorios para boot do desktop

Saidas esperadas:

- contrato de lookup `nome + contexto + tamanho -> node/path`
- lista de icones boot-criticos
- estrategia de fallback documentada

## Fase 1: Empacotamento e Registro dos Assets

- [ ] Estender o caminho de registro de assets para incluir `assets/icons`
- [ ] Evitar registrar manualmente centenas de arquivos um a um em C
- [ ] Criar manifest gerado para os icones embarcados, com caminho, bytes e metadados minimos
- [ ] Garantir que a arvore fique visivel via `fs_resolve()` no runtime
- [ ] Definir se arquivos nao usados no MVP entram agora ou em lotes por contexto

Direcao recomendada:

- gerar um manifesto em build para os icones usados no MVP
- embarcar primeiro `apps`, `places`, `status`, `notifications` e `panel`
- adiar `extras/` e formatos nao suportados

## Fase 2: Parser de `index.theme` e Metadados

- [ ] Criar parser minimo para `index.theme`
- [ ] Ler `Directories`, `Size`, `Type`, `Context`, `MinSize`, `MaxSize`
- [ ] Ignorar no MVP qualquer chave nao necessaria
- [ ] Validar o arquivo uma vez no bootstrap da UI, nao a cada lookup
- [ ] Representar em memoria uma tabela compacta de diretorios consultaveis

Observacao importante:

- o `index.theme` atual ja esta proximo do formato freedesktop, entao vale reutilizar isso em vez de inventar uma convencao propria

## Fase 3: API de Tema de Icones no Runtime

- [ ] Criar modulo dedicado, por exemplo `userland/modules/icon_theme.c`
- [ ] Publicar header tipo `headers/userland/modules/include/icon_theme.h`
- [ ] Expor funcoes para inicializar, selecionar tema e resolver caminhos
- [ ] Expor lookup por nome/contexto/tamanho
- [ ] Expor lookup por lista de nomes de fallback
- [ ] Expor helper para obter bitmap paletizado pronto para desenhar

API minima sugerida:

- `icon_theme_init()`
- `icon_theme_set_current(const char *theme_name)`
- `icon_theme_resolve(const char *name, enum icon_context context, int size, char *path, int path_len)`
- `icon_theme_decode(const char *name, enum icon_context context, int size, struct ui_icon_bitmap *out)`

## Fase 4: Politica de Formatos

- [ ] No MVP, aceitar somente arquivos com extensao `png` e opcionalmente `bmp`
- [ ] Ignorar `svg` e `xpm` durante a descoberta ou lookup
- [ ] Se um nome existir apenas como `svg`/`xpm`, cair no fallback raster definido pelo sistema
- [ ] Mapear lacunas onde um icone importante so existe em formato nao suportado

Evolucao recomendada depois do MVP:

- pipeline offline para converter `svg` e `xpm` para `png`
- manifesto gerado apenas com saidas rasterizadas aprovadas

## Fase 5: Cache e Memoria

- [ ] Criar cache LRU pequeno para bitmaps de icones decodificados
- [ ] Chavear cache por `nome + contexto + tamanho`
- [ ] Invalidar o cache ao trocar de tema ou modo de video
- [ ] Evitar redescodificar icones do taskbar e menu a cada frame
- [ ] Limitar tamanho do cache para nao competir com wallpaper e janelas

## Fase 6: Integracao com Desktop e UI

- [ ] Substituir desenhos textuais/hardcoded por icones onde fizer sentido
- [ ] Aplicar icones aos atalhos principais do desktop
- [ ] Aplicar icones ao menu iniciar
- [ ] Aplicar icones ao tray de rede e audio
- [ ] Aplicar icones a janelas e dialogs que tenham identidade clara de app
- [ ] Garantir que ausencia de icone nunca esconda o texto ou a acao

Prioridade de integracao:

- `desktop.c`
- componentes do taskbar/tray
- launcher/menu iniciar
- file manager
- dialogs de personalize

## Fase 7: Mapeamento de Nomes do Sistema

- [ ] Definir tabela de mapeamento entre entidades do `vibeOS` e nomes de icones do tema
- [ ] Mapear apps nativos para nomes existentes do tema ou aliases internos
- [ ] Mapear locais do desktop para `places`
- [ ] Mapear estados do sistema para `status` e `notifications`
- [ ] Criar alias interno quando o nome do app nao bater com o nome do asset

Exemplos que precisam existir no plano de alias:

- terminal
- editor
- file manager
- browser
- calculator
- trash
- folder
- network online/offline
- audio muted/low/medium/high

## Fase 8: Persistencia e Personalizacao

- [ ] Estender `ui.cfg` para incluir `icon_theme=<nome>`
- [ ] Carregar o tema junto com wallpaper e tema de cores
- [ ] Criar fallback para o tema embarcado padrao caso o tema salvo nao exista
- [ ] Permitir que `personalize` mude o tema de icones no futuro
- [ ] Garantir compatibilidade com configs antigas sem `icon_theme`

## Fase 9: Ferramentas de Build e Qualidade

- [ ] Criar script de inventario dos icones usados pelo desktop
- [ ] Criar script de verificacao de gaps: nome sem arquivo, contexto errado, formato nao suportado
- [ ] Criar relatorio de cobertura do MVP
- [ ] Opcionalmente gerar manifesto C/header a partir da arvore real de `assets/icons`
- [ ] Validar nomes duplicados em tamanhos/contextos diferentes

## Fase 10: Validacao

- [ ] Bootar desktop e verificar que o tema carrega sem warnings fatais
- [ ] Validar icones do desktop em `640x480` e resolucoes maiores
- [ ] Validar menu iniciar com cache frio e cache quente
- [ ] Validar tray de rede/audio em mudanca de estado
- [ ] Validar fallback quando um icone obrigatorio estiver ausente
- [ ] Validar troca de modo de video com recarga correta do cache
- [ ] Medir custo de memoria e tempo de decode durante o bootstrap

## Ordem Recomendada

1. Fechar lista de icones boot-criticos e formatos realmente suportados.
2. Colocar os assets do MVP no caminho do FS do sistema.
3. Implementar parser minimo de `index.theme`.
4. Implementar lookup por nome/contexto/tamanho com fallback.
5. Adicionar cache de decode.
6. Integrar primeiro no desktop shell e tray.
7. Persistir configuracao e preparar personalizacao.
8. Expandir cobertura para file manager, dialogs e demais apps.

## Definicao de Pronto do MVP

- [ ] O desktop sobe usando o tema de `assets/icons`
- [ ] Os atalhos principais exibem icones reais do tema
- [ ] O menu iniciar exibe icones reais do tema
- [ ] Rede e audio usam icones dinamicos do tema
- [ ] A busca de icones funciona por nome/contexto/tamanho com fallback
- [ ] O sistema continua funcional mesmo se um icone individual estiver faltando
- [ ] O cache evita decode repetido em render continuo

## Riscos Principais

- [ ] Tentar suportar `svg` e `xpm` cedo demais e travar o merge
- [ ] Inflar demais o bootstrap com assets nao essenciais
- [ ] Lookup sem cache causar decode repetido por frame
- [ ] Nomes do tema nao baterem com os nomes dos apps nativos
- [ ] Falta de manifest/build step tornar o registro de assets manual e fragil

## Recomendacao Final

- [ ] Tratar este trabalho como duas entregas separadas:
- [ ] entrega 1: tema de icones funcional para o desktop nativo com `png`
- [ ] entrega 2: ampliacao de cobertura, conversao offline de vetoriais e suporte a temas adicionais

Isso mantem o plano encaixado no estado atual do `vibeOS`: o tema ja existe, o decoder de `png` ja existe, e o maior valor agora e transformar `assets/icons` em infraestrutura real do desktop em vez de tentar resolver todo o ecossistema freedesktop de uma vez.
