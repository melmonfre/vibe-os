# Plano de Rede + Som no Painel

Data da proposta: 2026-03-25

## Objetivo

Adicionar ao desktop do VibeOS dois applets no lado direito do painel:

- um applet de rede, antes do som, para listar redes Wi-Fi, pedir senha, conectar e mostrar estado da interface
- um applet de som, na extrema direita, para listar entradas/saidas de audio e controlar o volume de cada uma
- o port completo de `userland/applications/network/vibe-browser` como aplicacao de desktop e navegador padrao do sistema
- o port completo de `userland/applications/network/vibe-browser` como aplicacao de desktop e navegador padrao do sistema
- deve mostrar na aba desempenho do gerenciador de tarefas o driver de som e o driver de rede detectados assim como ja é feito com a gpu
-deve se criar sprites para os applets de som e network no painel ao inves de usar letras
O plano prioriza reaproveitar ao maximo o que ja existe em `compat/` e no microkernel atual, em vez de criar APIs totalmente novas.
- usar o novo sistema de som para tentar reproduzir assets/vibe_os_boot.wav no bootloader uma unica vez, caso falhe na execução isso não deve impactar o boot.
- o mesmo vale para assets/vibe_os_desktop.wav que deve reproduzir uma unica vez ao carregar o startx. caso falhe tambem não deve travar a area de trabalho
## Resumo executivo

Conseguimos planejar isso de forma realista e com bastante reuso, mas os dois itens nao estao no mesmo estagio:

- Som:
  - ha um caminho claro e incremental
  - a ABI BSD de audio/mixer ja esta importada
  - o servico `audio` ja existe no microkernel, embora ainda seja `query-only`
  - o applet de painel e o painel de volume sao viaveis cedo, mesmo com backend inicial simples

- Rede:
  - o servico `network` tambem existe, mas ainda esta em `query-only`
  - para DHCP e DNS ha muito reuso pronto em `compat`
  - para Wi-Fi real ainda faltam partes de stack e driver/control-plane 802.11
  - o equivalente a "NetworkManager" no VibeOS deve nascer como um daemon proprio e fino, reaproveitando `ifconfig`, `dhcpleased` e `unwind`, nao como um port literal do NetworkManager do Linux

- Navegador:
  - o `vibe-browser` ja esta no repo, mas ainda nao esta integrado ao desktop do VibeOS
  - ele vira uma fase propria do projeto, dependente da stack de rede, DNS, downloads e UX de desktop

## Checklist de progresso

### Painel / Desktop

- [X] existe area de tray/applets no lado direito do painel
- [X] applet de rede foi adicionado antes do som
- [X] applet de som foi adicionado na extrema direita
- [X] sprites/icone dos applets substituíram as letras no painel
- [X] popup de som abre a partir do applet
- [X] popup de rede abre a partir do applet
- [X] popup de rede reutiliza perfis salvos e tenta auto-conexao no desktop
- [X] popup de rede exibe redes salvas na propria UI
- [X] popup de rede permite alternar auto-conexao e esquecer perfis salvos
- [X] popups de rede e som aceitam navegacao por teclado
- [X] desktop delega reconciliacao inicial de rede ao `netmgrd` MVP
- [X] popup de rede usa `netmgrd` MVP para conectar, desconectar e persistir perfis
- [X] a aba de desempenho do task manager mostra driver/backend de audio e rede
- [X] a aba de desempenho do task manager tambem mostra estado real de link/interface/IP/DNS
- [X] boot/startx tentam tocar os WAVs novos em modo best-effort
- [X] fallback de wallpaper foi endurecido para nao sumir em caso de config/decodificacao invalida

### Audio

- [X] servico `audio` responde a `GETINFO`
- [X] servico `audio` responde a `GET_STATUS`
- [X] servico `audio` responde a `GET_PARAMS`
- [X] servico `audio` responde a `MIXER_READ`
- [X] servico `audio` responde a `MIXER_WRITE`
- [X] syscalls/runtime de `START`, `STOP` e `WRITE` estao expostas para userland
- [X] existe playback one-shot de WAV por helper reaproveitavel
- [X] `soundctl list` existe
- [X] `soundctl status` existe
- [X] `soundctl set` existe
- [X] `soundctl mute` e `soundctl unmute` existem
- [X] `soundctl tone` existe para smoke test rapido
- [X] `soundctl play` existe para testar WAVs reais
- [X] `doom_port/i_sound_vibe.c` saiu do stub e usa o backend novo
- [X] backend `compat-auich` detecta hardware PCI suportado em familias AC97 compativeis alem de Intel
- [X] backend `compat-auich` faz bring-up basico de AC97/mixer/sample-rate
- [X] backend `compat-auich` possui caminho MVP de PCM out via DMA
- [X] telemetria de runtime de audio foi exposta para status/task manager
- [ ] port honesto dos drivers necessarios presentes em `compat`
- [X] audio audivel real foi validado em runtime no QEMU com AC97
- [X] backend de audio tem IRQ/underrun handling robusto o suficiente para considerar o driver fechado
- [X] enumeracao de endpoints fisicos reais acima do mixer/driver esta pronta
- [~] captura real de audio esta pronta
- [~] HDA/`azalia` foi portada como segundo backend `compat`
- [~] existe matriz de validacao em hardware real para as ~20 maquinas alvo

### Rede

- [X] servico `network` responde a `GETINFO`
- [X] servico `network` responde a `GET_STATUS`
- [X] servico `network` expoe scan basico e operacoes de conectar/desconectar
- [X] `netctl` existe com `status`, `scan`, `connect` e `disconnect`
- [X] `netctl` tambem gerencia perfis salvos e auto-conexao
- [X] popup de rede mostra status/interface/IP/gateway/DNS
- [X] popup de rede tambem lista perfis salvos
- [X] task manager mostra backend/estado de rede e lease exportado pelo `netmgrd`
- [X] task manager tambem consome o snapshot exportado por `netmgrd`
- [X] o snapshot/status de rede agora tambem explicita a origem do lease aplicado em `em0`
- [X] socket/connect/send/recv MVP deixou de ser puro stub com loopback local
- [X] socket ABI MVP agora cobre `listen`/`accept` no loopback local
- [X] existe `netmgrd` MVP como app dedicado para reconciliacao/estado/perfis
- [X] ponte para aplicar lease IPv4 de `compat` em `em0` via `netmgrd`/runtime foi preparada
- [X] `netmgrd import-lease` já entende o texto do `dhcpleased` e normaliza isso para o runtime
- [X] `netmgrd reconcile` e `connect em0` já procuram automaticamente `/var/db/dhcpleased/em0`
- [ ] port honesto dos drivers necessarios presentes em `compat`
- [X] existe `netmgrd` dedicado
- [ ] ethernet com DHCP funcional via reaproveitamento de `compat`
- [ ] DNS real via base `unwind` esta funcional
- [ ] Wi-Fi real com scan/associacao/WPA esta funcional
- [ ] socket ABI precisa evoluir para cobrir casos alem do loopback MVP
- [ ] comandos ipconfig, ip addr, curl, wget, ping devem funcionar (reaproveitar o que for util em compat)
- [ ] usar dns padrão 1.1.1.1 com fallback pra 8.8.8.8 e resolver nomes de dominio

### Navegador

- [ ] `vibe-browser` esta integrado ao desktop como navegador padrao
- [ ] runtime/rede/downloads necessarios para o navegador estao prontos

## O que ja existe e pode ser reaproveitado

### Audio

- `headers/sys/audioio.h`
  - ABI BSD de audio e mixer
- `headers/kernel/microkernel/audio.h`
  - formato Vibe-native para o servico de audio
- `kernel/microkernel/audio.c`
  - servico `audio` ja registrado
- `userland/applications/games/doom_port/i_sound_vibe.c`
  - exemplo de consumidor de audio em userland
- `userland/applications/games/DOOM/sndserv/*`
  - referencia util para um servidor de audio simples baseado em mensagens/fila
- `compat/usr.bin/mixerctl/*`
  - referencia direta para modelo de mixer/class/devinfo
- `compat/usr.bin/sndioctl/*`
  - referencia direta para volume, mute e nomes de controles
- `compat/usr.bin/sndiod/*`
  - base para um servidor de audio mais completo depois do MVP
- drivers e infraestrutura de audio em `compat/sys/dev/*`
  - fonte de extracao para drivers reais

Estado atual:

- [X] o servico `audio` responde a `GETINFO`, `GET_STATUS`, `GET_PARAMS`, `MIXER_READ` e `MIXER_WRITE`
- [X] ja existe enumeracao compacta de controles no estilo BSD para `output/input` e `volume/mute`
- [X] o applet de som do painel ja le/escreve o mixer real
- [X] `soundctl` ja existe como CLI de diagnostico e controle
- [X] `START`, `STOP` e `WRITE` de audio estao implementados para playback
- [X] existe helper de WAV best-effort para boot/startx
- [X] existe backend `compat-auich` MVP com AC97 + DMA PCM out, agora com deteccao ampliada para AMD/ATI/NVIDIA/SiS/ALI/VIA compativeis
- [X] validacao final de audio audivel real no QEMU foi concluida
- [X] o driver `compat-auich` ja usa IRQ real, contabiliza progresso DMA por descritor e roda `soundctl tone` sem stubs
- [X] a deteccao PCI do backend AC97 compat agora cobre Intel ICH/ESB/440MX, AMD 768/8111, ATI SB200/SB300/SB400/SB600, NVIDIA nForce/MCP, SiS 7012, ALI M5455 e VIA VT82C686/VT8233
- [X] o backend `compat-auich` agora tambem puxa quirks reais de `compat/auich` para BAR nativo em memoria nos Intel ICH4+ e unmute especifico do SiS 7012
- [X] o backend `compat-auich` agora tambem tolera o quirk de Intel ICH4/5/6/7 que nao levanta `PCR`, seguindo o caminho `ignore_codecready` inspirado no `compat/auich`
- [X] o seletor `input default` agora programa `AC97_REG_RECORD_SELECT` para `mic`/`line`
- [X] `READ` de audio agora existe na ABI (`syscall`, runtime e `soundctl record`)
- [X] existe `audiosvc` MVP para exportar endpoints logicos, backend e defaults acima do mixer atual
- [X] `audiosvc` e `soundctl` agora exportam/listam endpoints conforme a topologia realmente detectada, em vez de anunciar sempre `main/headphones` e `mic/line`
- [X] a enumeracao de saidas agora considera tambem DACs AC97 extras (`surround` e `center-lfe`) quando o codec anuncia esses caminhos, em vez de assumir no maximo 2 saidas
- [X] o backend AC97 agora tambem ativa/programa os DACs extras (`surround` e `center-lfe`) via `EXT_AUDIO_CTRL` e registra os volumes/mutes correspondentes quando esses caminhos existem
- [X] o popup de som do desktop agora respeita a topologia detectada e nao desenha entradas inexistentes
- [X] a telemetria exportada pelo backend agora inclui flags de IRQ/captura e contagem real de endpoints para diagnostico
- [X] a telemetria de runtime agora diferencia IRQ observada, backend sem IRQ valida, starvation e underrun no status/export
- [X] a telemetria de diagnostico agora tambem expõe transporte `io/mmio`, quirk `ignore_codecready` e presenca de caminhos multicanal para `soundctl`/`audiosvc`/task manager
- [X] a telemetria de captura agora tambem expõe `capture-dma`, `capture-data` e `capture-xrun`, deixando a prontidao/atividade da captura menos implicita
- [X] a validacao automatizada em QEMU (`validate-audio-stack`) agora cobre `audiosvc status` + `soundctl status` com a telemetria nova
- [X] a validacao automatizada em QEMU agora tambem exige os marcadores de `transport`, `codecready-quirk` e `multichannel` exportados por `audiosvc`/`soundctl`
- [X] a validacao automatizada em QEMU agora tambem exige os marcadores de `capture-dma` exportados por `audiosvc`/`soundctl` quando a captura esta em teste
- [X] o roundtrip automatizado de captura+playback em QEMU (`validate-audio-stack-roundtrip`) passou em execucao sequencial; a falha vista antes era colisao de artefato ao rodar `make` concorrente no mesmo `build/`
- [~] `compat-auich` agora captura por DMA real no `PCMI` e ja retorna dados no QEMU com `AC97`; `soundctl record` ja grava `/capture.wav` no runtime em smoke curto e tambem em captura longa de 3s, mas ainda falta validacao em maquina fisica
- [X] existe smoke test separado no QEMU para `intel-hda`, agora confirmando o backend bootstrap `compat-azalia` em vez do fallback antigo para `softmix`
- [X] o endurecimento recente do validador tambem foi revalidado nos cenarios `validate-audio-stack-roundtrip` e `validate-audio-hda-smoke`
- [X] a frente de hardware real virou item explicito do plano: priorizar os chipsets presentes nas ~20 maquinas alvo e validar por PCI ID/codec/controladora, em vez de continuar so no QEMU
- [~] existe agora um backend `compat-azalia` bootstrap: detecta controladora HDA, faz reset MMIO basico, sobe CORB/RIRB, responde a probe minima de codec/AFG, registra IRQ/telemetria e passa a aparecer como backend proprio em vez de cair diretamente em `softmix`; o bootstrap agora tambem aproveita verbos/constantes de conexao do `compat/azalia` para ficar menos "stub disfarçado"
- [~] o `compat-azalia` agora tambem descobre widgets basicos de saida (DAC/pin), classifica pins fisicos por `config default`, tenta casar pin->DAC via `connection list`/`selector`, programa verbs iniciais de caminho de playback e arma stream descriptor + BDL no controlador; os cenarios `validate-audio-hda-smoke`, `validate-audio-hda-playback` e `validate-audio-hda-startup` passam no QEMU `intel-hda`, o codec sobe com `codec-probe=1`, `widget-probe=1` e agora tambem preserva `path-programmed=1` apos playback; o playback explícito ja recicla defensivamente o stream anterior entre chunks e o handler de IRQ HDA agora reconhece/limpa `BCIS`/`FIFOE`/`DESE`; os WAVs automáticos de `boot` e `desktop` agora completam no HDA no fluxo automatizado de startup; ainda falta revalidar isso em hardware real
- [X] o `compat-azalia` agora registra rotas por endpoint fisico (`speaker`, `headphones`, `line-out`, `digital`) em vez de depender de um unico pin "melhor"; o `default output` do mixer passa a escolher o pin/DAC correspondente ao endpoint HDA detectado, e `audiosvc`/`soundctl`/popup exportam esses nomes fisicos quando o backend ativo e `compat-azalia`
- [X] o playback WAV em HDA foi endurecido tambem para os sons automáticos: `soundctl play` fatia o WAV em bursts HDA e aguarda o backend ficar ocioso entre chunks; para autoplay, o `desktop` agora dispara uma unica vez, com atraso, depois do inicio da sessao, e o `boot` deixou de cair em `defer`; o `desktop-session` passou a ser reproduzido em modo cooperativo no loop do desktop, sem monopolizar a UI enquanto o HDA esvazia cada chunk; os alvos `validate-audio-hda-startup` e `validate-audio-hda-playback` voltaram a passar juntos no QEMU `intel-hda`
- [X] o alvo `validate-audio-hda-playback` agora exige `path-programmed=1` apos playback, endurecendo a validacao do `compat-azalia` no proprio fluxo padrao do repo
- [X] a selecao inicial do audio agora tenta multiplos candidatos PCI HDA/AC97 antes de aceitar `softmix`; quando o fallback ainda acontece, `device.config` diferencia `no-pci-audio` de `no-usable-hw-backend`
- [X] a telemetria e o UX de diagnostico do audio ficaram mais acionaveis em hardware real: `audiosvc`, `soundctl`, task manager e o `audioplayer` agora mostram hints para estados como `hda-no-output-stream`, `hda-reset-failed` e `no-usable-hw-backend`, em vez de deixar isso implicito
- [X] a telemetria de hardware do audio ficou mais util para a matriz de maquinas reais: `GETINFO` agora carrega PCI ID/localizacao da controladora, codec HDA detectado e a rota de saida `pin/dac`; `audiosvc`, `soundctl` e o task manager passaram a exibir isso diretamente
- [X] a validacao HDA ficou endurecida tambem no diagnostico de hardware: `validate-audio-hda-playback` agora exige no serial os markers `pci=`, `codec=` e `route=` emitidos por `audiosvc`/`soundctl`, e o fluxo voltou a passar no QEMU `intel-hda`
- [X] o diagnostico do `audioplayer` ficou mais acionavel em maquina real: o detalhe da ultima falha agora tambem inclui PCI ID da controladora, codec HDA e rota `pin/dac`, e a linha de status do app foi ampliada para caber esse contexto
- [X] o `compat-azalia` agora nao programa so pin/EAPD: durante a selecao da rota de playback ele tambem tenta desmutar e aplicar ganho basico nos amplificadores de saida/entrada ao longo do caminho HDA escolhido (pin/mixer/selector/DAC), ativa defensivamente os demais pins fisicos de saida detectados, sobe o function group e os widgets da rota para power state `D0` e passa a logar a rota `pin/dac` escolhida; `validate-audio-hda-playback` continuou verde depois desse endurecimento
- [~] a compatibilidade HDA em notebook real ainda esta em fechamento: o `compat-azalia` agora escolhe o primeiro stream de saída a partir de `ISS/OSS` do `GCAP`, mas essa frente ainda precisa de revalidacao fora do QEMU para confirmar que o erro de playback no player foi resolvido em hardware Intel HDA real

### Rede

- `headers/sys/socket.h`
  - vocabulario BSD de sockets
- `headers/kernel/microkernel/network.h`
  - formato Vibe-native para o servico de rede
- `kernel/microkernel/network.c`
  - servico `network` ja registrado
- `compat/sbin/ifconfig/*`
  - referencia fortissima para configuracao de interfaces, incluindo Wi-Fi
- `compat/sbin/dhcpleased/*`
  - base para cliente DHCP IPv4
- `compat/sbin/dhcp6leased/*`
  - base para DHCPv6 futuro
- `compat/sbin/unwind/*`
  - base para resolucao DNS local
- `compat/sbin/ping/*`
  - bom smoke test posterior
- drivers e stack em `compat/sys/dev/*`, `compat/sys/net*` e afins
  - fonte de extracao para ethernet, Wi-Fi e 802.11

Estado atual:

- [X] o servico `network` responde a `GETINFO`
- [X] o servico `network` responde a `GET_STATUS`
- [X] existe control-plane MVP para scan/conectar/desconectar
- [X] existe camada MVP de socket/bind/connect/send/recv/listen/accept para loopback local
- [X] existe `netmgrd` MVP com `status`, `scan`, `reconcile` e exportacao de estado
- [X] o snapshot exportado por `netmgrd` ja inclui backend, DNS, manager e lease para preparar o encaixe de DHCP real
- [X] `netmgrd`, `netctl status` e task manager agora mostram se o lease veio de `runtime`, `compat-default` ou fallback MVP
- [X] `netmgrd` ja consegue consumir um lease texto de compat e aplicá-lo ao backend de `em0`
- [X] o formato de lease do `compat/dhcpleased` já é aceito diretamente pelo `netmgrd`
- [X] o lease padrão de `compat` já é descoberto automaticamente sem import manual prévio
- [X] o desktop ja usa `netmgrd reconcile` no boot da sessao e exporta estado em eventos de conexao
- [X] o desktop ja usa `netmgrd connect` e `netmgrd disconnect` no fluxo principal do applet
- [X] o task manager ja le `/runtime/netmgrd-status.txt` para mostrar auto-conexao e perfis salvos
- [ ] ainda nao ha um "network manager" do VibeOS

### Painel / Desktop

- `userland/modules/ui.c`
  - ja desenha taskbar/painel
- `userland/applications/desktop.c`
  - ja concentra hit-test, janelas, menu iniciar, personalize e logica de clique

Estado atual:

- [X] o painel ja desenha area de bandeja/applet a direita
- [X] rede e som ja ocupam essa area com popups proprios
- [X] menu iniciar ganhou atalhos diretos para `netctl status` e `soundctl status`

## Arquitetura proposta

## 1. Audio: `audiosvc` + `soundctl` + applet de painel

### 1.1 Servico de audio

Criar um daemon/app de servico de audio em userland, hospedado pelo servico `audio` do microkernel.

Responsabilidades:

- enumerar endpoints logicos:
  - saida principal
  - fones
  - entrada de linha
  - microfone
- manter estado:
  - volume por endpoint
  - mute
  - endpoint padrao
- expor operacoes mixer-like:
  - listar controles
  - ler volume
  - escrever volume
  - trocar endpoint padrao
- no backend inicial:
  - mesmo que ainda sem DMA real completa, manter a API pronta e um mixer funcional sobre o backend existente
- no backend final:
  - plugar drivers reais extraidos de `compat`
  - reaproveitar o maximo possivel da semantica `sndio`

### 1.2 ABI a preservar

Preservar a forma BSD/audioio sempre que possivel:

- `audio_device_t`
- `audio_status`
- `audio_swpar`
- `mixer_devinfo_t`
- `mixer_ctrl_t`

Onde a ABI BSD for ampla demais para o estagio atual, criar um wrapper Vibe pequeno em cima dela, sem quebrar compatibilidade futura.

### 1.3 Ferramentas de userland

Criar:

- `soundctl`
  - CLI para listar dispositivos/controles
  - ajustar volume
  - mutar/desmutar
  - escolher entrada/saida padrao

Exemplos esperados:

- `soundctl list`
- `soundctl set output-main 70`
- `soundctl mute mic`
- `soundctl default output headphones`

Estado atual do MVP:

- `soundctl list`
- `soundctl status`
- `soundctl set output-main 70`
- `soundctl mute mic`
- `soundctl unmute output`

### 1.5 Drivers e `compat`

Depois do MVP do mixer:

- extrair drivers PCI/AC97/HDA mais realistas para o alvo i386/QEMU/hardware real
- aproveitar `sndiod` como referencia para arbitragem e nomes de portas
- usar `mixerctl`/`sndioctl` como base de compatibilidade e diagnostico

### 1.6 Estrategia de implementacao do backend de som

Para cumprir os novos requisitos de reproducao de `assets/vibe_os_boot.wav` e
`assets/vibe_os_desktop.wav` sem travar o sistema, a implementacao deve seguir
em fases, preservando a ABI atual do servico `audio`.

Fase 1: playback one-shot funcional com backend software

- manter o mixer atual e abrir o caminho de playback real no servico `audio`
- expor `START`, `STOP` e `WRITE` por syscall/runtime para userland e apps Lang
- implementar uma fila/ring buffer PCM simples dentro do backend atual
- aceitar primeiro o formato mais facil para o MVP:
  - PCM assinado
  - little-endian
  - 16 bits
  - 44100/48000 Hz
  - mono ou stereo
- se a reproducao falhar:
  - o boot continua normalmente
  - o `startx` e o desktop continuam normalmente

Fase 2: player de WAV pequeno e reaproveitavel

- criar um helper em userland para:
  - abrir arquivo via `fs_read_file_bytes`
  - validar cabecalho RIFF/WAVE
  - localizar o chunk `fmt `
  - localizar o chunk `data`
  - alimentar o backend de audio em blocos pequenos
- usar esse helper em dois pontos:
  - boot: tocar `assets/vibe_os_boot.wav` uma unica vez
  - `startx`/desktop: tocar `assets/vibe_os_desktop.wav` uma unica vez
- a reproducao deve ser best-effort:
  - sem dependencia para o fluxo principal
  - sem busy-loop infinito
  - com timeout/log de falha curto

Fase 3: backend real via `compat`

- manter a ABI do servico `audio` e trocar apenas a implementacao interna
- criar uma camada de backend com dois modos:
  - `softmix`/fallback
  - `compat`/driver real
- o primeiro alvo real deve ser AC97 Intel ICH usando:
  - `compat/sys/dev/pci/auich.c`
  - `compat/sys/dev/ic/ac97.h`
- motivo da escolha:
  - encaixa melhor no alvo i386/QEMU do projeto
  - menor risco do que tentar HDA/`azalia` de inicio
  - permite validar DMA, mixer e interrupcao com uma base menor

Fase 4: consolidacao para apps e jogos

- fazer `doom_port/i_sound_vibe.c` sair do stub e consumir o backend novo
- depois disso, ampliar suporte para:
  - stereo fixo
  - mixing de multiplas fontes
  - notificacao de underrun
  - captura real
  - HDA/`azalia` como proximo alvo de compat

### 1.4 Applet do painel

Adicionar um applet de som na extrema direita do painel com:

- icone de alto-falante
- estado visual:
  - mute
  - volume baixo/medio/alto
- clique:
  - abre popup de som

Popup de som:

- lista de saidas
- lista de entradas
- slider de volume por endpoint
- botao/toggle de mute
- marcador de dispositivo padrao

## 2. Rede: `netmgrd` + `netctl` + applet de painel

### 2.1 Daemon de gerencia

Criar um daemon proprio do VibeOS, chamado por exemplo `netmgrd`.

Ele sera o equivalente funcional ao "NetworkManager" no projeto, mas fino e orientado a reuso de `compat`.

Responsabilidades:

- enumerar interfaces
- classificar:
  - ethernet
  - loopback
  - wifi
- manter perfis de rede
- acionar DHCP
- publicar DNS local
- expor estado para o painel

### 2.2 Reuso de `compat`

Usar `compat` em camadas:

- `ifconfig`
  - referencia para controle de interface
  - reutilizar parsing e semantica de configuracao onde fizer sentido
- `dhcpleased`
  - base para obter lease IPv4
- `unwind`
  - base para DNS local/caching resolver

Importante:

- nao tentar portar os daemons de OpenBSD integralmente logo de inicio
- extrair primeiro as partes uteis:
  - modelos de estado
  - parsers
  - controle de interface
  - fluxo de lease
  - publicacao de resolvers

### 2.2.1 Drivers e `compat`

Antes de Wi-Fi completo, precisamos puxar de `compat`:

- drivers ethernet que encaixem no alvo atual
- stack 802.11 suficiente para scan e associacao
- controle de autenticacao para WPA/WPA2
- glue minimo para expor isso ao `mk_network`

### 2.3 Wi-Fi

Para Wi-Fi real, o gerenciador precisa destas capacidades:

- listar redes vistas no scan
- expor SSID, seguranca, intensidade
- conectar com senha
- lembrar perfil salvo
- indicar conectado/conectando/falha

Isso exige:

- control-plane 802.11 no driver/kernel
- scan
- associacao
- WPA/WPA2 pelo menos

Conclusao honesta:

- o menu de rede pode ser projetado agora
- DHCP e DNS podem avancar antes
- Wi-Fi completo depende de trabalho real de stack/driver; nao e so colar `ifconfig`

### 2.4 Ferramentas de userland

Criar:

- `netmgrd`
  - reconciliar auto-conexao a partir dos perfis salvos
  - exportar estado sintetizado para arquivos/runtime
  - concentrar a futura transicao do painel para um manager dedicado
- `netctl`
  - listar interfaces
  - status da conexao
  - scan Wi-Fi
  - conectar
  - desconectar
  - mostrar lease e DNS

Exemplos esperados:

- `netctl status`
- `netctl scan wlan0`
- `netctl connect em0`
- `netctl connect wlan0 "MinhaRede"`
- `netctl connect wlan0 "MinhaRede" --psk "senha"`
- `netctl disconnect wlan0`
- `netctl profiles`
- `netctl remember wlan0 "MinhaRede" --psk "senha"`
- `netctl autoconnect "MinhaRede"`

### 2.5 Applet do painel

Adicionar applet de rede no lado direito do painel, antes do som.

Estado visual:

- sem link
- ethernet conectada
- wifi desconectado
- wifi conectando
- wifi conectado com barras de sinal

Clique:

- abre popup de rede

Popup de rede:

- interface ativa
- IP atual
- gateway
- DNS
- lista de redes visiveis
- campo de senha
- botao conectar/desconectar
- lista de redes salvas

## Fases propostas

## Fase 1 - Infra de painel/tray

Objetivo:

- criar uma area de applets no lado direito da taskbar

Entregas:

- reservar area fixa para applets
- hit-test dos applets
- popup ancorado ao applet
- base generica para applets do painel

Critério de pronto:

- [X] painel suporta pelo menos 2 applets sem conflitar com botoes de janela

## Fase 2 - Audio control-plane

Objetivo:

- sair do `query-only` no servico `audio`

Entregas:

- suporte real a `MIXER_READ` e `MIXER_WRITE`
- enumeracao basica de endpoints e controles
- `soundctl`
- popup de som no painel

Critério de pronto:

- alterar volume no painel muda o estado do mixer
- `soundctl list` e `soundctl set` funcionam
- o applet deixa de usar estado local fake para volume/mute

Estado atual:

- [X] pronto em modo MVP
- [ ] ainda falta plugar/estabilizar driver real e enumeracao de endpoints fisicos vindos de `compat`

## Fase 3 - Network control-plane minimo

Objetivo:

- construir o `netmgrd` e tirar valor de `compat` antes de Wi-Fi completo

Entregas:

- estado de interfaces
- suporte a ethernet
- DHCPv4 via base `dhcpleased`
- DNS local via base `unwind`
- `netctl status`
- popup de rede com status e detalhes de link

Critério de pronto:

- [ ] interface cabeada sobe, recebe lease e resolve DNS

## Fase 4 - Wi-Fi scan + conexao

Objetivo:

- adicionar controle de Wi-Fi no gerenciador

Entregas:

- scan de redes
- lista SSID/sinal/seguranca
- conexao com senha
- perfis salvos

Critério de pronto:

- [ ] usuario escolhe rede, informa senha e conecta pelo popup

## Fase 6 - `vibe-browser` como app de desktop

Objetivo:

- transformar `userland/applications/network/vibe-browser` no navegador oficial do ambiente grafico

Entregas:

- mapa de dependencias de build e runtime
- integracao com launcher/menu/taskbar
- integracao com sockets, DNS, downloads e URLs
- estrategia de port para Qt/WebEngine ou camada de compatibilidade equivalente
- associacao como navegador padrao

Critério de pronto:

- [ ] o navegador abre pelo desktop, navega com a rede real do sistema e consegue baixar/renderizar paginas no ambiente suportado

## Fase 5 - Refinamento de UX

Objetivo:

- fazer painel e popups ficarem de uso diario

Entregas:

- indicadores de erro
- reconnect automatico
- mute rapido
- scroll/teclas no popup
- persistencia de volume por dispositivo
- persistencia de redes salvas

## Ordem recomendada

1. Criar infraestrutura de tray/applet no painel.
2. Fechar mixer/control-plane de audio.
3. Entregar applet de som.
4. Criar `netmgrd` para ethernet primeiro.
5. Integrar DHCPv4.
6. Integrar DNS local.
7. Entregar applet de rede com status cabeado.
8. Evoluir scan/conexao Wi-Fi.
9. Portar e integrar o `vibe-browser`.

## Riscos e bloqueios reais

### Audio

- o backend atual de audio ainda pode ser mais "estado" do que "driver real"
- pode ser necessario introduzir uma camada de dispositivos/endpoints sinteticos antes de ter hardware completo

### Rede

- `mk_network` ainda nao implementa operacoes reais
- Wi-Fi nao depende so de UI; depende de stack/driver e seguranca
- portar `ifconfig`/`dhcpleased`/`unwind` literalmente pode ser caro demais; a estrategia certa e extrair partes e manter compatibilidade de semantica

## Recomendacao de implementacao

### Reuso forte de `compat`

Usar diretamente como referencia e fonte de extracao:

- `compat/sbin/ifconfig`
- `compat/sbin/dhcpleased`
- `compat/sbin/unwind`

### Evitar

- portar NetworkManager do Linux
- tentar portar o userspace BSD inteiro antes de fechar os contratos Vibe
- misturar UI de painel com a logica de DHCP/Wi-Fi
- tentar encaixar o `vibe-browser` antes de rede, DNS e downloads estarem minimamente funcionais

## Definicao de pronto

O projeto so deve ser considerado concluido quando:

- [X] existir area de applets no lado direito do painel
- [X] o applet de som listar entradas/saidas e controlar volume
- [X] existir `soundctl`
- [X] existir `netmgrd` e `netctl`
- [ ] ethernet com DHCP e DNS estiver funcional
- [X] o applet de rede mostrar estado real da conexao
- [ ] Wi-Fi puder listar redes, pedir senha e conectar
- [ ] o `vibe-browser` estiver integrado ao desktop como navegador de internet

## Meta de MVP

Se quisermos um MVP pragmatico antes do pacote completo:

1. [X] tray/applet no painel
2. [X] applet de som com mixer funcional
3. [ ] applet de rede com ethernet + DHCP + DNS

## Proximos passos imediatos

1. [X] subir o `audiosvc` MVP para mediar endpoints logicos e nomes de portas acima do mixer atual
2. [~] endurecer e validar em runtime o backend de som `compat-auich`
3. [ ] puxar DHCP/DNS reais de `compat` para o caminho de rede
4. [X] subir o esqueleto do `netmgrd`
5. [ ] com rede funcional, iniciar o port completo do `vibe-browser`
6. [ ] Wi-Fi real entra na iteracao seguinte

Esse MVP ja entrega muito valor e deixa o caminho do Wi-Fi isolado para quando a stack 802.11 estiver pronta.

## Atualizacao tecnica 2026-03-26

### Audio: estado apos endurecimento do backend

- o backend `compat-auich` agora ja tem:
  - probe PCI real do AC97 Intel ICH
  - acesso por I/O ou MMIO conforme o BAR real exposto pelo chipset
  - codec reset + mixer AC97
  - fila DMA de playback
  - progresso por polling e tambem por IRQ PCI registrada no kernel
  - quirks de compat para ICH4+/BAR nativo e SiS 7012
  - normalizacao explicita dos parametros aceitos pela ABI atual:
    - PCM signed little-endian
    - 16 bits
    - mono ou stereo
    - 11025 / 22050 / 44100 / 48000 Hz
- isso fecha melhor o caminho para:
  - `soundctl tone`
  - `soundctl play`
  - WAVs de boot/startx
  - `doom_port/i_sound_vibe.c`

### Audio: o que ainda falta para considerar fechado de verdade

- validacao audivel real em pelo menos uma maquina fisica alvo
- validacao funcional real em QEMU com `intel-hda` so deve mudar de fallback `softmix` para backend nativo quando o port de `azalia` existir
- telemetria final para diferenciar:
  - IRQ recebida
  - starvation
  - underrun
  - backend sem IRQ valida
- enumeracao/exportacao de endpoints fisicos acima do mixer/driver ja cobre os backends atuais:
  - AC97: `main`, `headphones`, `surround`, `center-lfe`, `mic`, `line`
  - HDA: `speaker`, `headphones`, `line-out`, `digital`, `mic`, `line-in`
- captura real de audio validada em maquina fisica
- segundo backend `compat` para HDA/`azalia`

### Wi-Fi: plano complementar realista para sair dos stubs

Conclusao objetiva:

- hoje `mk_network` ainda e um service/control-plane sintetico
- portanto nao basta plugar `if_iwm`, `if_iwn`, `athn` ou `rtwn`
- antes do Wi-Fi precisamos criar a base minima de rede do kernel

### Etapa A - Base de NIC real no kernel

Implementar uma camada Vibe minima para interface de rede:

- registro de interfaces (`lo0`, `em0`, depois `wlan0`)
- estado de link
- endereco MAC
- TX/RX de frames
- filas RX/TX
- interrupcoes de NIC
- attach/detach de driver PCI
- exportacao de MTU/capabilities

Meta:

- conseguir trazer primeiro `if_em` como backend cabeado real

### Etapa B - Ethernet real antes do Wi-Fi

Trazer primeiro um caminho cabeado completo:

- portar o attach minimo de `compat/sys/dev/pci/if_em.c`
- mapear BAR/IRQ/DMA/descriptor ring para o runtime do kernel
- entregar `link up/down` real
- integrar lease IPv4 real via `dhcpleased`
- publicar DNS real via `unwind` ou arquivo/runtime equivalente

Critério de pronto:

- `netctl connect em0`
- lease IPv4 real
- DNS real
- socket IPv4 deixando de ser loopback-only

### Etapa C - Socket/runtime real

Antes do navegador e antes do Wi-Fi completo:

- evoluir socket ABI para usar a interface real
- suportar:
  - ARP
  - IPv4
  - UDP
  - TCP MVP
- manter o loopback existente como fallback/teste

Critério de pronto:

- `connect/send/recv/listen/accept` funcionando em IPv4 real sobre `em0`

### Etapa D - 802.11 minimo

So depois da Etapa A/B/C:

- escolher um primeiro alvo de Wi-Fi PCI:
  - `if_iwn`
  - ou `athn`
  - ou `rtwn`
- portar:
  - attach PCI
  - firmware loading
  - scan
  - associacao
  - RSSI/link state
- expor isso no `mk_network`

Critério de pronto:

- `netctl scan wlan0`
- listar SSID/sinal/seguranca reais

### Etapa E - WPA/WPA2 PSK

Depois do scan/associacao aberta:

- portar o glue necessario de 802.11/WPA do `compat`
- suportar pelo menos:
  - open
  - WPA2-PSK
- integrar o fluxo de senha/perfil salvo do `netmgrd`

Critério de pronto:

- conectar em rede WPA2 real
- manter perfil salvo
- auto-conectar no boot da sessao

### Etapa F - Fechamento de UX e browser

Com rede real entregue:

- popup de rede deixa de mostrar scan sintetico
- task manager passa a mostrar driver/firmware/link reais
- `vibe-browser` deixa de depender de stubs de rede

## Ordem recomendada revisada

1. endurecer e validar o `compat-auich` ate sair som audivel real
2. fechar `if_em` + DHCP + DNS reais
3. tirar sockets do modo loopback-only
4. integrar o navegador apenas depois da rede cabeada real
5. portar um primeiro driver Wi-Fi PCI
6. fechar scan + associacao aberta
7. fechar WPA2-PSK e perfis salvos
