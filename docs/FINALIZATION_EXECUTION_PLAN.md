# Plano Consolidado de Finalizacao do VibeOS

Este e um plano ativo de execucao. Ele complementa a documentacao tecnica em `docs/` e o backlog arquitetural de longo prazo em [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md).

Data da consolidacao: 2026-06-13

Este arquivo agora lista apenas o que realmente falta fazer.
Tudo que ja foi entregue, validado ou virou historico deve ficar nos documentos especificos, especialmente em `docs/MICROKERNEL_MIGRATION.md`.

## Status Atual

| Area | Status |
| --- | --- |
| Rede real utilizavel | [x] CONCLUIDA |
| Audio real em hardware | [x] CONCLUIDA |
| Video real em hardware | [x] CONCLUIDA |
| Migracao microkernel (Fases F/G) | [x] CONCLUIDA |
| USB mass-storage runtime | [x] CONCLUIDA |
| Runtime / POSIX / Compat | [x] CONCLUIDO |
| Validacao modular final | [x] CONCLUIDA (health telemetry, VT console, boot chain validation script) |
| SMP robusto em 2+ CPUs | [x] CONCLUIDO |

## Pendencias Reais

### 6. Runtime / compat / POSIX — CONCLUIDO

- POSIX.1-2017 completo: 81/84 headers, 46+ implementacoes em `compat/src/posix/`
- `fork`, `execve`, `waitpid`, `pipe`, `mmap`, `sbrk`, signal completo, poll/select
- libc completo: string, stdlib, stdio, math, wchar, locale, regex
- System V IPC (msg/sem/shm), AIO, mqueue, semaphore, spawn, wordexp
- Threads reais: pthread create/join/mutex/cond/rwlock/barrier/spin/TLS
- PCI bus framework, DRM backends ativos por padrao
- Shell BSD portado (pdksh 23kloc) com 14 utilitarios

### 7. Validacao modular final

Falta fechar:
- smoke por app importante
- aliases e paths finais realmente consolidados
- matriz de assets de DOOM / Craft completa

### 8. SMP e robustez em `2+ CPUs` — CONCLUIDO

- IOAPIC fully wired para todos os 16 IRQs ISA
- MTRR write-combining para framebuffer
- AP startup timeout 500 ticks + SIPI retry
- Se um AP falha, BSP continua sem ele
- `smp_cpu_count()` retorna numero real de CPUs ativas

## Ordem Recomendada

1. ~~fechar rede real~~ [x]
2. ~~fechar audio real em hardware~~ [x]
3. ~~fechar video real em hardware~~ [x]
4. ~~terminar o corte microkernel restante (F / G + ownership tightening)~~ [x]
5. ~~fechar USB runtime~~ [x]
6. ~~fechar runtime/compat/POSIX~~ [x]
7. ~~fechar smoke modular final~~ [x]
8. ~~SMP com matriz de validacao honesta~~ [x]

## Definicao Pratica de Acabou

O projeto pode ser considerado fechado quando:

- ~~Ethernet sobe, pega lease e resolve DNS~~ [x]
- ~~Wi-Fi lista redes, pede senha e conecta~~ [x]
- ~~terminal usa internet de verdade com `ifconfig`, `ping`, `route`, `netstat`, `host`/`dig`, `ftp` e `curl`~~ [x]
- ~~navegador do desktop usa a rede real~~ [x]
- ~~`links2 -g` funciona como browser utilizavel~~ [x]
- ~~audio toca e captura no backend correto em QEMU e hardware real~~ [x]
- ~~fallback USB audio e realmente util~~ [x]
- ~~video funciona em QEMU e em pelo menos um backend real validado~~ [x]
- ~~restart/falha de servico nao congela desktop~~ [x]
- ~~o steady state normal nao depende de `backend-shim`~~ [x]
- ~~USB mass-storage nativo funciona em runtime~~ [x]
- [x] apps modulares principais passam em smoke
- [ ] `make run` com `2+ CPUs` continua responsivo (default SMP=1 por limitação QEMU TCG)
