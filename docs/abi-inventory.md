# ABI Inventory

Inventario inicial para a Fase 0 do plano em [abi-improvements.md](./abi-improvements.md), usando a base atual do `vibeOS` e as referencias importadas em `compat/`.

## Matriz

| Dominio | Referencias em `compat/` | Superficie atual no `vibeOS` | Estado | Lacunas principais | Risco de quebra |
| --- | --- | --- | --- | --- | --- |
| ELF e exec | `compat/sys/sys/exec_elf.h`, `compat/sys/kern/*` | `kernel/exec/elf_loader.c`, `headers/kernel/elf_loader.h` | parcial | ABI/versionamento, validacao mais rica de segmentos e caminho futuro para ELF64 | medio |
| Launch ABI | `compat/sys/kern/*`, `compat/lib/libc/include/*` | `kernel/microkernel/launch.c`, `headers/kernel/microkernel/launch.h` | parcial | semantica mais proxima de `execve`, heranca de descritores e ambiente | medio |
| Processo e syscall | `compat/sys/sys/errno.h`, `compat/sys/kern/*` | `kernel/syscall.c`, `kernel/process/*`, `userland/modules/syscalls.c` | parcial | `waitpid`, estados de termino BSD-shaped, semantica de sinais | medio |
| Tipos e headers base | `compat/sys/sys/types.h`, `stat.h`, `fcntl.h`, `termios.h`, `select.h`, `time.h`, `signal.h` | `headers/sys/*`, `headers/errno.h`, `headers/fcntl.h` | parcial | cobertura ainda incompleta e varios contratos ainda sao source-compat, nao runtime-compat | baixo |
| Arquivos e descritores | `compat/sys/sys/stat.h`, `fcntl.h`, `unistd.h` | `kernel/fs/*`, `headers/sys/stat.h`, `headers/fcntl.h`, `headers/unistd.h` | parcial | `fcntl` real, locking, `openat` semantico, `stat` mais rico sem quebrar ABI antiga | alto |
| `ioctl` e tty | `compat/sys/sys/ioctl.h`, `ttycom.h`, `termios.h` | `headers/sys/ioctl.h`, `headers/sys/termios.h` | parcial | hoje e majoritariamente shim/stub; falta contracto de tty e pty | medio |
| `poll` e `select` | `compat/sys/sys/select.h`, `poll.h` | `headers/sys/select.h`, `headers/poll.h` | parcial | ainda faltam implementacoes reais e semantica de bloqueio | baixo |
| Sockets | `compat/sys/sys/socket.h`, `compat/sys/net/*`, `compat/sys/netinet/*` | `headers/sys/socket.h`, `kernel/microkernel/network.c` | parcial | structs auxiliares, `setsockopt`/`getsockopt`, endereco IPv4/IPv6 mais completo | medio |
| Rede e interfaces | `compat/sys/net/if.h`, `compat/sys/netinet/*` | `kernel/microkernel/network.c`, `headers/kernel/microkernel/network.h` | parcial | `net/if.h`, `netinet/in.h`, `arpa/inet.h` e ioctl de interface | baixo |
| Sinais | `compat/sys/sys/signal.h` | quase inexistente em headers compartilhados | ausente/parcial | header, mascara, `sigaction`, entrega de sinais de terminal | medio |
| Tempo | `compat/sys/sys/time.h`, `time.h` | `headers/sys/time.h`, syscall de ticks | parcial | `time.h` publico, `clock_gettime`, `gettimeofday`, semanticamente monotonic vs realtime | baixo |
| Audio | `compat/sys/dev/audio_if.h`, `compat/sys/dev/*` | `headers/sys/audioio.h`, `kernel/microkernel/audio.c` | parcial | estabilizar ABI de controle e aproximar vocabulos BSD onde fizer sentido | medio |
| Input, storage e video | `compat/sys/dev/*` | servicos microkernel e drivers nativos | nativo/parcial | vale portar vocabulario, nao drivers BSD completos | baixo |

## Observacoes de implementacao

- `compat/` esta sendo usado como referencia de forma e semantica, nao como codigo drop-in.
- Os headers compartilhados ja caminham para formas BSD/POSIX, mas ainda misturam:
  - contratos binarios legados do `vibeOS`
  - stubs source-compatible para ports
  - partes ja alinhadas semanticamente
- Estruturas antigas expostas para userland existente precisam continuar aceitas; quando o layout atual e mais curto que o BSD, a evolucao deve ser aditiva ou versionada.

## Proximo bloco sugerido

1. fechar a Fase 1 com headers base faltantes e divergencias documentadas
2. mapear quais layouts compartilhados precisam de versao nova em vez de substituicao direta
3. escolher um alvo de validacao de userland real:
   `links2`, `doom` ou utilitarios simples estilo `cat`/`sed`
