# Craft Functional Recovery Plan

## Objetivo

O objetivo deste trabalho nao e apenas "deixar o Craft mais leve".

O objetivo e ter um clone funcional de Minecraft no VibeOS:

- mundo voxel 3D visivel e navegavel
- chunks carregando corretamente ao redor do jogador
- camera, colisao e spawn funcionando
- colocar e remover blocos funcionando
- renderer mostrando cubos/blocos de verdade, nao uma tela chapada
- pipeline assincrono para carga e rebuild de chunks
- multiprocessador/SMP ativado como parte do design, nao como detalhe opcional

Se o jogo nao entregar isso, qualquer micro-otimizacao de frame continua sendo secundaria.

## Diretriz Central

O plano anterior estava otimista demais com "performance first".

Para este projeto, a ordem correta e:

1. corrigir a funcionalidade base do clone
2. garantir pipeline assincrono de chunks
3. garantir execucao real em multiprocessador
4. so depois otimizar renderer, blit e custo de frame

## Requisitos Nao Negociaveis

- O primeiro frame util do jogo precisa mostrar terreno voxel ou, no pior caso, chegar nisso logo apos um bootstrap curto e previsivel.
- O jogador nao pode nascer olhando para uma cena vazia se ja existe mundo gerado ao redor.
- O mundo precisa continuar carregando em background sem congelar o loop principal.
- O caminho assincrono precisa continuar funcionando com SMP habilitado.
- O modo SMP nao pode ser apenas um "ifdef bonito": ele precisa distribuir trabalho real entre workers.
- O renderer compat precisa respeitar a semantica visual minima do jogo: blocos, profundidade, culling e visibilidade coerentes.

## O Que Consideramos "Clone Funcional de Minecraft"

- O terreno aparece como blocos 3D.
- O jogador consegue andar pelo mundo.
- A camera aponta para um volume voxel coerente.
- O chunk central e a vizinhanca imediata aparecem corretamente.
- Novos chunks entram sem quebrar o frame ou sumir com o mundo.
- O jogador consegue destruir e colocar blocos.
- O estado persiste sem corromper a geometria.

Nao entra nesta definicao inicial:

- paridade visual total com OpenGL original
- multiplayer completo
- efeitos visuais avancados
- HUD perfeita

Esses itens ficam depois da funcionalidade base.

## Problema Atual

Hoje o sintoma observado e simples: o jogo abre, mas nao se comporta como clone funcional de Minecraft.

Na pratica isso significa que pelo menos um destes contratos esta quebrado:

- o mundo nao esta sendo gerado no momento certo
- os chunks nao estao chegando ao renderer no momento certo
- a geometria chega, mas o renderer compat a descarta ou projeta errado
- o caminho assincrono/SMP esta incompleto, quebrado ou nao confiavel
- o bootstrap inicial depende de combinacoes fragilizadas entre sync e async

## Hipotese de Trabalho

O `Craft` precisa ser tratado como um sistema com tres contratos principais:

1. contrato de mundo
   `create_world`, `load_chunk`, `compute_chunk`, `generate_chunk`

2. contrato de pipeline assincrono
   workers, handoff de dados, conclusao de jobs, rebuild e troca segura de buffers

3. contrato de renderer voxel
   matriz, culling, depth, clipping, draw de chunks e apresentacao final

Enquanto esses tres contratos nao estiverem validados, qualquer ajuste de performance e apenas tentativa cega.

## Fase 0: Contrato de Funcionalidade

- [ ] Escrever no codigo e no documento que o alvo e "clone funcional de Minecraft", nao "demo leve".
- [ ] Definir um checklist minimo de aceitacao por frame inicial:
- [ ] existe `player_count == 1`
- [ ] existe chunk central
- [ ] existe pelo menos uma quantidade minima de faces no mundo visivel
- [ ] `render_chunks()` desenha pelo menos um chunk valido
- [ ] a camera nasce olhando para terreno utilizavel
- [ ] Registrar logs claros para primeira sessao:
- [ ] chunks criados
- [ ] chunks com `faces > 0`
- [ ] buffers gerados
- [ ] chunks efetivamente desenhados

Arquivos-alvo:

- `userland/applications/games/craft/upstream/src/main.c`
- `userland/applications/games/craft/craft_upstream_runner.c`
- `userland/applications/games/craft/craft_gl_compat.c`

## Fase 1: Restaurar o Mundo Voxel

- [ ] Garantir que o bootstrap inicial sempre entregue mundo visivel ao redor do jogador.
- [ ] Revisar a logica de `force_chunks()` e o papel dela no primeiro frame util.
- [ ] Validar `highest_block()` e spawn inicial para evitar nascer em posicao ruim ou olhando para vazio.
- [ ] Confirmar que `compute_chunk()` realmente gera faces e que `generate_chunk()` entrega buffers validos.
- [ ] Confirmar que os chunks visiveis nao estao sendo descartados por `chunk_visible()` de forma indevida.
- [ ] Confirmar que o renderer desenha cubos com profundidade correta.

Critico nesta fase:

- sem mundo voxel na tela, o jogo continua nao funcional

Arquivos-alvo:

- `userland/applications/games/craft/upstream/src/main.c`
- `userland/applications/games/craft/upstream/src/world.c`
- `userland/applications/games/craft/craft_gl_compat.c`

## Fase 2: Pipeline Assincrono Verdadeiro

- [ ] Tornar explicito o que e sync apenas no bootstrap e o que obrigatoriamente vira async depois.
- [ ] Definir um pipeline simples e seguro:
- [ ] agendar job
- [ ] worker carregar/gerar chunk
- [ ] publicar resultado
- [ ] main thread consumir resultado
- [ ] trocar buffer sem corromper heap
- [ ] Remover qualquer `double free`, reuse invalido de ponteiro ou handoff ambiguo.
- [ ] Garantir que o loop principal continue responsivo enquanto chunks entram em background.
- [ ] Evitar depender de caminhos sync improvisados para mascarar falha do async.

Resultado esperado:

- o jogo pode bootar com ajuda sync minima
- depois disso o mundo continua expandindo de forma assincrona

Arquivos-alvo:

- `userland/applications/games/craft/upstream/src/main.c`
- `userland/applications/games/craft/craft_thread_compat.c`
- `userland/applications/games/craft/craft_upstream_runner.c`

## Fase 3: Multiprocessador / SMP Obrigatorio

- [ ] Validar quantos workers reais o runtime esta subindo.
- [ ] Confirmar que `craft_thread_parallel_workers()` usa informacao real do sistema.
- [ ] Confirmar que ha execucao paralela de jobs de chunk quando `cpu_count > 1`.
- [ ] Registrar por worker:
- [ ] jobs recebidos
- [ ] jobs concluidos
- [ ] tempo medio por job
- [ ] Garantir que o jogo continue funcional com SMP ligado, sem depender de fallback serial silencioso.
- [ ] Se o sistema cair para 1 worker, registrar isso como degradacao explicita e nao como comportamento "normal".

Definicao minima desta fase:

- com multiprocessador ativo, o mundo continua correto
- com multiprocessador ativo, o pipeline continua assincrono
- com multiprocessador ativo, nao ha corrupcao de buffer ou sumico de chunks

Arquivos-alvo:

- `userland/applications/games/craft/craft_thread_compat.c`
- `userland/applications/games/craft/craft_upstream_runner.c`
- `kernel` e `sys_task_snapshot` apenas se a telemetria indicar erro de base

## Fase 4: Renderer Compat para Voxel, Nao para "Parecer Algo"

- [ ] Validar transformacao de vertices com a mesma semantica minima esperada pelo Craft.
- [ ] Validar `backface culling`, depth test, viewport e clipping para geometria voxel.
- [ ] Confirmar que chunks validos nao viram faixas horizontais ou superficies chapadas.
- [ ] Tratar `craft_gl_compat.c` como renderer voxel em software, nao como OpenGL genérico pela metade.
- [ ] Criar asserts/logs temporarios para:
- [ ] triangulos recebidos
- [ ] triangulos descartados por culling
- [ ] triangulos descartados por bounds/clipping
- [ ] chunks cujo buffer existe mas nada foi desenhado

Arquivos-alvo:

- `userland/applications/games/craft/craft_gl_compat.c`
- `userland/applications/games/craft/craft_upstream_runner.c`

## Fase 5: Jogabilidade Basica

- [ ] Andar, olhar, pular e colidir de forma confiavel.
- [ ] Destruir e colocar blocos sem quebrar chunks vizinhos.
- [ ] Persistir estado basico do jogador e do mundo sem reabrir o bug visual.
- [ ] Manter o mundo funcional em janela e fullscreen.

Arquivos-alvo:

- `userland/applications/games/craft/upstream/src/main.c`
- `userland/applications/games/craft/craft_upstream_runner.c`

## Fase 6: Performance Depois da Funcionalidade

- [ ] Medir custo real de `craft_upstream_frame()`.
- [ ] Separar timing de:
- [ ] simulacao/input
- [ ] `craft_thread_pump()`
- [ ] carga e conclusao de chunks
- [ ] `render_sky`, `render_chunks`, `render_signs`, `render_players`
- [ ] HUD/texto
- [ ] `craft_upstream_blit()`
- [ ] Reduzir custo de frame sem sacrificar o contrato funcional.
- [ ] Ajustar resolucao interna, draw distance e features opcionais apenas depois que o clone estiver correto.

Arquivos-alvo:

- `userland/applications/games/craft/craft_app.c`
- `userland/applications/games/craft/craft_upstream_runner.c`
- `userland/applications/games/craft/craft_gl_compat.c`

## Ordem Recomendada

1. Provar no log e na tela o que esta faltando para existir mundo voxel visivel.
2. Fechar o contrato do bootstrap inicial.
3. Fechar o contrato do pipeline assincrono.
4. Fechar o contrato de SMP real.
5. Fechar o contrato do renderer voxel.
6. So entao entrar em tuning de frame time e degradacao visual controlada.

## Definicao de Pronto

- [ ] `Craft` se comporta como clone funcional de Minecraft no VibeOS.
- [ ] O mundo aparece como voxel 3D logo no inicio da sessao.
- [ ] O carregamento de chunks continua de forma assincrona.
- [ ] O multiprocessador/SMP participa de verdade da carga e rebuild de chunks.
- [ ] O renderer compat desenha blocos corretamente.
- [ ] O jogador consegue navegar, destruir e colocar blocos.
- [ ] Otimizacoes posteriores nao dependem de quebrar o contrato funcional acima.
