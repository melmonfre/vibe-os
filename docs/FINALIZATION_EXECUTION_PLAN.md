# Plano Consolidado de Finalizacao do VibeOS
Data da consolidacao: 2026-03-31

Este arquivo agora lista apenas o que realmente falta fazer.
Tudo que ja foi entregue, validado ou virou historico deve ficar nos documentos especificos, especialmente em `docs/MICROKERNEL_MIGRATION.md`.

## Pendencias Reais

### 1. Rede real utilizavel

Falta fechar:
- datapath real de NIC em vez de control-plane parcial
- DHCP e DNS funcionando no caminho normal
- readiness/eventos de socket completos no datapath real
- Wi-Fi com scan, senha e conexao de verdade
- comandos de rede/internet no terminal com runtime funcional
- navegador do desktop usando a rede real
- `links2 -g` integrado como browser grafico/terminal utilizavel

Comandos minimos que ainda precisam ficar de pe:
- `ifconfig`
- `ping`
- `route`
- `netstat`
- `host`
- `dig`
- `ftp`
- `curl`

### 2. Audio real em hardware

Falta fechar:
- `compat-azalia` robusto em notebook real
- captura real validada fora do fallback de QEMU
- `compat-uaudio` promovido de attach/substrate para backend confiavel
- matriz de hardware definindo backend correto por maquina
- reduzir ainda mais o bridge privilegiado de audio depois da Fase C entregue

### 3. Video real em hardware

Falta fechar:
- promover pelo menos um backend real de hardware com validacao honesta
- ampliar prova de `fast_lfb` e backends nativos fora do QEMU
- validar modos widescreen e troca de modo em hardware real
- endurecer backpressure/overflow de eventos de video
- manter claro o escopo real de `i915` / `radeon` / `nouveau`

### 4. Fechamento final da migracao microkernel

Falta fechar:
- concluir Fase F de rede assincrona real
- concluir os restos de Fase G onde ainda existe dependencia de bridge/fallback hibrido
- tirar `backend-shim` do steady state normal
- garantir que restart/falha de servico nao derruba desktop, input, video ou audio
- continuar apertando ownership de backend ate o kernel ficar so com o minimo privilegiado

### 5. USB de runtime

Falta fechar:
- suporte nativo a USB mass-storage em runtime

Observacao:
- boot por USB BIOS ja nao e o bloqueio; o que falta e backend nativo depois que o kernel assume

### 6. Runtime / compat / POSIX ainda necessario

Falta fechar:
- gaps POSIX que ainda travam ports importantes
- execucao nativa/VFS onde ainda houver dependencia do caminho atual
- dependencias de plataforma para JVM

### 7. Validacao modular final

Falta fechar:
- smoke por app importante
- aliases e paths finais realmente consolidados
- matriz de assets de DOOM / Craft completa

### 8. SMP e robustez em `2+ CPUs`

Falta fechar:
- validacao confiavel com `2+ CPUs` em QEMU e hardware
- decidir o proximo degrau de deteccao/topologia SMP para as maquinas alvo
- garantir desktop responsivo com audio/input/video ativos fora do caso feliz single-core

## Ordem Recomendada

1. fechar rede real
2. fechar audio real em hardware
3. fechar video real em hardware
4. terminar o corte microkernel restante (F / G + ownership tightening)
5. fechar USB runtime
6. fechar runtime/compat/POSIX que ainda bloquear ports
7. fechar smoke modular final
8. retomar SMP com matriz de validacao honesta

## Definicao Pratica de Acabou

O projeto pode ser considerado fechado quando:

- Ethernet sobe, pega lease e resolve DNS
- Wi-Fi lista redes, pede senha e conecta
- terminal usa internet de verdade com `ifconfig`, `ping`, `route`, `netstat`, `host`/`dig`, `ftp` e `curl`
- navegador do desktop usa a rede real
- `links2 -g` funciona como browser utilizavel
- audio toca e captura no backend correto em QEMU e hardware real
- fallback USB audio e realmente util
- video funciona em QEMU e em pelo menos um backend real validado
- restart/falha de servico nao congela desktop
- o steady state normal nao depende de `backend-shim`
- USB mass-storage nativo funciona em runtime
- apps modulares principais passam em smoke
- `make run` com `2+ CPUs` continua responsivo
