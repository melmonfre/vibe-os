# Plano de Rede + Som no Painel

Data da proposta: 2026-03-25

## Objetivo

Adicionar ao desktop do VibeOS dois applets no lado direito do painel:

- um applet de rede, antes do som, para listar redes Wi-Fi, pedir senha, conectar e mostrar estado da interface
- um applet de som, na extrema direita, para listar entradas/saidas de audio e controlar o volume de cada uma

O plano prioriza reaproveitar ao maximo o que ja existe em `compat/` e no microkernel atual, em vez de criar APIs totalmente novas.

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

Estado atual:

- o servico `audio` responde a `GETINFO`, `GET_STATUS` e `GET_PARAMS`
- `MIXER_READ`, `MIXER_WRITE`, `READ` e `WRITE` ainda retornam falha/stub

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

Estado atual:

- o servico `network` responde a `GETINFO`
- a camada de operacoes de socket/bind/connect/send/recv ainda e stub
- ainda nao ha um "network manager" do VibeOS

### Painel / Desktop

- `userland/modules/ui.c`
  - ja desenha taskbar/painel
- `userland/applications/desktop.c`
  - ja concentra hit-test, janelas, menu iniciar, personalize e logica de clique

Estado atual:

- o painel so desenha botao iniciar + botoes de janela
- nao existe area de bandeja/applet a direita

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
- `netctl connect wlan0 "MinhaRede"`
- `netctl connect wlan0 "MinhaRede" --psk "senha"`
- `netctl disconnect wlan0`

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

- painel suporta pelo menos 2 applets sem conflitar com botoes de janela

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

- interface cabeada sobe, recebe lease e resolve DNS

## Fase 4 - Wi-Fi scan + conexao

Objetivo:

- adicionar controle de Wi-Fi no gerenciador

Entregas:

- scan de redes
- lista SSID/sinal/seguranca
- conexao com senha
- perfis salvos

Critério de pronto:

- usuario escolhe rede, informa senha e conecta pelo popup

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

## Definicao de pronto

O projeto so deve ser considerado concluido quando:

- existir area de applets no lado direito do painel
- o applet de som listar entradas/saidas e controlar volume
- existir `soundctl`
- existir `netmgrd` e `netctl`
- ethernet com DHCP e DNS estiver funcional
- o applet de rede mostrar estado real da conexao
- Wi-Fi puder listar redes, pedir senha e conectar

## Meta de MVP

Se quisermos um MVP pragmatico antes do pacote completo:

1. tray/applet no painel
2. applet de som com mixer funcional
3. applet de rede com ethernet + DHCP + DNS
4. Wi-Fi entra na iteracao seguinte

Esse MVP ja entrega muito valor e deixa o caminho do Wi-Fi isolado para quando a stack 802.11 estiver pronta.
