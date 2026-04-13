# USB Boot e Armazenamento

## Estado atual

- O `kernel` tenta storage tradicional primeiro (`ahci`/`ata`) e, se ainda nao houver bloco primario depois do `usb_host`, reprova storage novamente para permitir backend USB em runtime.
- O host USB ja detecta interfaces `mass storage`, guarda `snapshot` configurado e expoe helpers de `control`, `bulk` e selecao do primeiro dispositivo pronto.
- O backend nativo em [kernel/drivers/storage/usb_mass_storage.c](/home/mel/Documentos/vibe-os/kernel/drivers/storage/usb_mass_storage.c) ja faz:
  - `GET_MAX_LUN`
  - `INQUIRY`
  - `TEST UNIT READY`
  - `REQUEST SENSE`
  - `READ CAPACITY (10)`
  - `READ (10)`
  - resolucao da particao de dados via `bootinfo` ou MBR
  - registro como block device primario

## Fechado nesta rodada

- Escrita nativa via USB mass storage agora usa `SCSI WRITE(10)`.
- O cache de leitura do backend USB e invalidado apos escrita.
- Cada escrita tenta confirmacao por leitura de volta do setor.
- Foi adicionado `SYNCHRONIZE CACHE (10)` em modo best-effort antes da verificacao.

## Fluxo de boot esperado

1. `kernel_storage_init()` falha em `ahci`/`ata` quando nao existe disco legado.
2. `kernel_usb_host_init()` enumera controladores/portas e configura um dispositivo `mass storage`.
3. `kernel_storage_init()` e chamado de novo em [kernel/entry.c](/home/mel/Documentos/vibe-os/kernel/entry.c#L496) e o backend USB registra o block device primario.
4. `fs`/`AppFS`/persistencia passam a usar o mesmo contrato de storage ja usado pelos backends ATA/AHCI.

## O que ainda falta

- Validar em hardware/QEMU um boot completo sem disco ATA/AHCI, apenas com imagem em USB.
- Avaliar suporte a subclasses/protocolos alem de BOT + caminho SCSI atual.
- Melhorar diagnostico de erro com decode mais rico de `REQUEST SENSE`.
- Decidir se vale promover o bridge `usb_compat` ou remove-lo quando o backend nativo estiver suficientemente validado.

## Investigacao em hardware real

- Foi instrumentado o boot entre `vfs_init()` e os `*_service_init()` para localizar travamentos no banner "Initializing VFS...".
- O travamento comum foi reduzido para a janela observada ao redor do launch/bring-up de audio, imediatamente apos `mk_audio_service_init()` anunciar `audio: done`.
- O backend soft de audio estava chamando reconciliacao/topologia cedo demais no bootstrap critico; isso foi adiado para fora da fase critica sem remover funcionalidade.
- Com isso, o boot passou a imprimir `audio: soft`, `audio: usb-snap`, `audio: azalia?`, `audio: azalia ok`, `audio: topo`, `audio: launch`, `audio: done`.
- Mesmo assim, tanto com boot por USB quanto com boot por IDE/compatibilidade, o sistema ainda trava logo depois de `audio: done`.
- Foi adicionada instrumentacao tambem no `audiosvc` userland, nos syscalls iniciais e no scheduler para descobrir se a task de audio chega a ser agendada e em qual ponto para.

## Estado atual por caminho de boot

- IDE/compatibilidade:
  - storage voltou a funcionar.
  - Sequencia observada: `storage: ata?`, `ata: partition`, `ata: register`, `ata: done`, `storage: ata ok`.
  - O travamento continua ocorrendo depois de `audio: done`.
- USB:
  - storage ainda nao sobe.
  - Sequencia observada: `storage: ata?`, `ata: identify fail`, depois fallback ate `storage: none`.
  - O travamento tambem continua ocorrendo depois de `audio: done`.

## Leitura atual

- Existem dois problemas distintos:
  - regressao/falta de suporte no caminho de storage para boot via USB;
  - travamento comum no bootstrap apos o launch do servico de audio.
- A arquitetura definida em `MICROKERNEL_MIGRATION` assume launch/supervisao assincronos e ownership explicito por servico; o problema atual nao deve ser descrito como "o kernel rodando servico de forma sincrona".
- A leitura correta neste ponto e: mesmo com launch assincrono, ainda existe alguma falha na fase inicial de bring-up, dispatch ou primeira execucao da task/servico de audio, e isso consegue parar o progresso observado do boot.

## Politica de retry de servicos

- O supervisor de servicos agora agenda retry em background quando um servico falha no launch/restart, em vez de prender o boot no primeiro erro.
- O backoff usa sequencia de Fibonacci e para apos 10 tentativas.
- A fila de retry roda em task de supervisao dedicada, fora de IRQ, e acorda por `waitable`.
- O comportamento foi implementado em `kernel/microkernel/service.c` e no metadata de `headers/kernel/microkernel/service.h`.
- Os breadcrumbs de boot foram mantidos para continuar localizando em hardware real se o servico chega a ser adicionado ao scheduler e despachado.
- O caminho de request tambem foi endurecido: quando um servico esta offline/degradado, o chamador deixa de forcar recuperacao inline por padrao e passa a agendar recuperacao assincrona no supervisor, falhando rapido quando nao houver fallback valido.

## Implicacao pratica

- O resto do sistema deve conseguir seguir subindo mesmo se um servico individual falhar no primeiro launch.
- Chamadas a servicos opcionais deixam de carregar consigo a responsabilidade de "consertar o servico agora" no meio do fluxo do boot.
- O problema de storage USB continua separado e ainda precisa de correcao propria, porque hoje o caminho USB termina em `storage: none`.
- O problema pos-`audio: done` agora precisa ser revalidado com a nova imagem, porque o supervisor passou a tolerar falha inicial de servico e tentar recuperacao automatica.

## Correcao de bring-up

- O sinal observado em hardware `audiosvc: task added` sem `audiosvc: first dispatch` expunha uma falha real de bring-up: a task era criada e enfileirada, mas o scheduler ainda nao estava liberado para despachar servicos lancados nessa fase.
- A causa era dupla:
  - `scheduler_set_preemption_ready(1)` so era armado em `userland_run()`, tarde demais para servicos criados durante a inicializacao de VFS/servicos;
  - `mk_service_launch_task()` nao estava preenchendo `task_class` do descriptor, o que deixava servicos sem classificacao explicita de despacho.
- A correcao aplicada foi:
  - antecipar `syscall_init()` e a liberacao do scheduler para antes do launch dos servicos de VFS;
  - mapear `service_type -> task_class` no launch generico de servicos.
- O teste seguinte em hardware mostrou que antecipar a liberacao do scheduler foi cedo demais: o boot passou a morrer em `audio: soft`, o que indica interferencia do scheduler/timer no proprio bring-up do kernel.
- Essa parte foi recuada; o foco agora voltou para instrumentar o trecho imediatamente apos `audiosvc: task added`, separando `scheduler_publish_task_event()` de `smp_wake_sleeping_cpus()` com breadcrumbs `before publish`, `after publish` e `wake sent`.
- Como ajuste mais estrutural, o publish de evento de lifecycle do scheduler deixou de fazer limpeza/verificacao de subscribers mortos no caminho quente do `LAUNCHED`. Essa manutencao cruzava estruturas do scheduler durante o launch e e melhor ficar fora do hot path de bring-up.
- Como ajuste arquitetural mais forte, os eventos de lifecycle/task do scheduler agora ficam desarmados durante o bootstrap do kernel e so sao habilitados quando o supervisor userland realmente se inscreve. Isso remove mailbox/stream de eventos do caminho de launch de servicos no bring-up inicial.
- Como desdobramento dessa ideia, `input`, `console` e `network` deixaram de ser lancados dentro do bootstrap do kernel. Agora o kernel apenas prepara seus descriptors/local handlers, e o `init` userland faz o `restart` desses servicos depois que o sistema base ja subiu.
- O conceito de `prepare_task` tambem foi aliviado: servicos "prepared" passam a ser apenas metadata/local handler no bootstrap, sem inicializar subscriptions/mailboxes de evento ate que o servico realmente fique `live`.
