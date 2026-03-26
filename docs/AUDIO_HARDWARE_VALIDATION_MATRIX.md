# Matriz de Validacao de Hardware Real de Audio

Data base: 2026-03-26

## Objetivo

Registrar a campanha de validacao real do stack de audio do VibeOS por
controladora, codec e maquina alvo, em vez de depender apenas dos smokes em
QEMU.

## Como preencher

- `familia/controladora`: ex. `Intel ICH7 AC97`, `Intel HDA`, `AMD Hudson HDA`
- `pci-id ctrl`: `vendor:device` da controladora PCI
- `codec`: vendor/modelo do codec quando conhecido
- `transporte`: `io`, `mmio` ou `mmio+hda`
- `backend`: `compat-auich`, `compat-azalia` ou outro
- `playback`: `ok`, `parcial`, `falha`, `nao testado`
- `captura`: `ok`, `parcial`, `falha`, `nao testado`
- `endpoints`: topologia real vista em `audiosvc status` e no popup
- `observacoes`: quirk, sintoma, workaround, logs uteis

## Matriz

| alvo | maquina | familia/controladora | pci-id ctrl | codec | transporte | backend | playback | captura | endpoints | observacoes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| QEMU-AC97-01 | QEMU `-device AC97` | Intel/compat AC97 | emulado | emulado | io/mmio | `compat-auich` | ok | parcial | `main`, `mic`, `line` | roundtrip e smoke ja validados no repo |
| QEMU-HDA-01 | QEMU `-device intel-hda -device hda-duplex` | Intel HDA | emulado | duplex generico do QEMU | mmio+hda | `compat-azalia` | ok | nao testado | `main`, `line-in` sinteticos do codec QEMU | smoke e playback explícito passam; `codec-probe=1` e `widget-probe=1`; o backend recicla defensivamente o stream anterior entre chunks; `path-programmed` ainda fica `0` no status ocioso e o auto-WAV de `boot`/`desktop` segue em modo `defer` para preservar o bootstrap |
| HW-01 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-02 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-03 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-04 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-05 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-06 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-07 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-08 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-09 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-10 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-11 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-12 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-13 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-14 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-15 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-16 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-17 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-18 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-19 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |
| HW-20 | preencher | preencher | preencher | preencher | preencher | preencher | nao testado | nao testado | preencher | |

## Checklist de coleta por maquina

- Registrar `lspci -nn` equivalente ou snapshot do probe PCI do VibeOS
- Rodar `audiosvc status`
- Rodar `soundctl status`
- Testar `soundctl play /assets/vibe_os_desktop.wav`
- Testar `soundctl tone 500 440`
- Testar `soundctl record 1500 /capture.wav` quando captura existir
- Anotar se o popup de som mostra apenas endpoints reais
- Anotar IRQ observada, `transport`, `codecready-quirk`, `widget-probe` e `path-programmed`
- Registrar separadamente se o auto-WAV de `boot`/`desktop` ficou `ok`, `deferido` ou `desligado` para aquele backend

## Prioridade sugerida

1. Fechar primeiro as maquinas AC97 mais proximas do caminho `compat-auich`
2. Depois agrupar HDA por controladora Intel/AMD/NVIDIA e repetir a coleta
3. Separar logo no inicio os casos em que o problema esta no `codec-probe`, no `widget-probe` ou so no `start/playback`
