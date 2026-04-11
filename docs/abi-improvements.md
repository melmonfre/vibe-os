# ABI Improvements Plan

## Objetivo

Construir uma estrategia realista para aproximar o `vibeOS` das ABIs e vocabularios de userland do OpenBSD usando o material importado em `compat/`, sem transformar o kernel em um port monolitico e sem quebrar o userland existente.

O alvo nao e "copiar o kernel do OpenBSD".
O alvo e:

- reaproveitar formas de ABI que ja sao estaveis e conhecidas
- reduzir atrito para portar programas BSD
- melhorar compatibilidade binaria e semantica no loader, syscalls, headers e servicos
- manter a arquitetura microkernel do `vibeOS`

## Regra universal

Esta iniciativa obedece a regra em [`../universal-rule.md`](../universal-rule.md):

- o kernel nunca pode quebrar o userland

Logo:

- toda evolucao de ABI precisa ser aditiva, versionada ou com fallback
- nenhuma fase deste plano pode exigir rebuild geral de userland para o sistema continuar bootando
- contratos antigos devem continuar aceitos enquanto houver userland dependente

## Premissas

- `compat/sys` e referencia de estruturas, semantica e decomposicao, nao codigo drop-in
- a maior parte do valor imediato esta nas ABIs de userland e nos contratos de driver/control-plane, nao em portar subsistemas completos do kernel BSD
- o `vibeOS` ja tem um nucleo de compatibilidade em:
  - ELF/loader
  - launch ABI
  - message ABI
  - socket vocabulary
  - audio vocabulary
  - servicos `storage/filesystem/video/input/console/network/audio`

## Fontes de referencia principais

- `compat/sys/sys/*`
- `compat/sys/dev/*`
- `compat/sys/net/*`
- `compat/sys/kern/*`
- `compat/sys/arch/i386/*`
- `compat/lib/libc/include/*`
- `compat/include/*`
- `compat/usr.bin/*`

## Estrategia geral

Em vez de tentar "todas as ABIs" em um lote so, dividir por camadas:

1. inventario e classificacao
2. headers e estruturas compartilhadas
3. loader e ABI binaria
4. syscalls e semantica de processo
5. I/O, tty, ioctl e arquivos
6. rede e sockets
7. sinais, tempo e multiplexacao
8. dispositivos e ABIs de driver
9. validacao com programas reais do `compat`

## Fase 0: Inventario de ABI

### Meta

Criar uma matriz de ABIs OpenBSD relevantes para o `vibeOS`, separando:

- ja temos
- temos parcial
- ainda nao temos
- nao faz sentido portar agora

### Entregas

- inventario em `docs/` por dominio
- tabela com:
  - nome da ABI
  - arquivos de referencia no `compat`
  - superficie atual no `vibeOS`
  - lacunas
  - risco de quebra de userland

### Dominios minimos do inventario

- ELF e exec
- tipos basicos de libc e headers de sistema
- syscalls de processo
- arquivos e descritores
- `ioctl`
- `termios`
- sockets
- `poll` e `select`
- sinais
- tempo
- audio
- input
- storage
- rede

## Fase 1: Headers e tipos base

### Meta

Consolidar a linguagem comum entre userland BSD e `vibeOS`.

### Prioridades

- `sys/types.h`
- `sys/param.h`
- `sys/errno.h`
- `fcntl.h`
- `unistd.h`
- `sys/stat.h`
- `sys/ioctl.h`
- `sys/termios.h`
- `signal.h`
- `time.h`
- `sys/time.h`
- `poll.h`
- `sys/select.h`
- `sys/socket.h`
- `net/if.h`
- `netinet/in.h`

### Regras

- preservar layout binario quando a estrutura for compartilhada com userland
- documentar explicitamente qualquer divergencia intencional
- evitar inventar tipos nativos se ja existir forma BSD estavel

## Fase 2: Loader, ELF e ABI binaria

### Meta

Endurecer o caminho de carga binaria e alinhar semantica com o ecossistema BSD onde fizer sentido.

### Trabalho

- validar melhor `ELF32` e preparar caminho para `ELF64` futuro
- mapear melhor `e_osabi`, `e_abiversion`, `e_machine`, `PT_LOAD`, alinhamento e entrada
- definir politica de versionamento ABI para apps `vibeOS`
- decidir como representar:
  - binarios nativos `vibe`
  - binarios BSD-portados
  - apps AppFS

### Referencias

- `compat/sys/sys/exec_elf.h`
- codigo de exec em `compat/sys/kern/*`
- ecosistema `libc` importado

## Fase 3: Processo, launch e semantica de syscall

### Meta

Aproximar a semantica visivel ao userland para criacao, termino, consulta e erro de processos.

### Trabalho

- formalizar ABI de launch e contexto de processo
- revisar `getpid`, `exit`, `wait`, `yield`, snapshots e terminacao
- introduzir semantica BSD-shaped para:
  - codigos de erro
  - estados de termino
  - retorno consistente de syscall
- preparar terreno para:
  - `fork` ou equivalente
  - `execve` ou equivalente
  - `waitpid`

### Observacao

Nao precisa implementar `fork` imediatamente.
Precisa definir desde cedo como a ABI vai acomodar isso sem quebrar o que existe.

## Fase 4: Arquivos, descritores, `stat`, `fcntl`, `ioctl`

### Meta

Fazer programas BSD simples pararem de depender de shims ad hoc.

### Trabalho

- consolidar contrato de descritor de arquivo
- alinhar `open/read/write/close/lseek/stat/fstat`
- revisar flags de `open`
- alinhar `errno` e semantica de falha
- criar politica de `ioctl`:
  - quais classes serao suportadas
  - quais ficarao traduzidas no userland
  - quais serao nativas do microkernel

### Alvos imediatos

- `cat`
- `sed`
- `grep`
- `vi`/`mg`
- shells portados

## Fase 5: TTY, console e `termios`

### Meta

Parar de tratar console apenas como dispositivo grafico/textual do `vibeOS` e passar a ter uma ABI de terminal utilizavel por software BSD real.

### Trabalho

- `termios`
- canonical/raw mode
- echo
- sinais de terminal
- `ioctl` basico de tty
- pseudo-terminal em fase posterior

### Impacto

Sem isso, muito userland BSD interativo sempre vai precisar de compat layer especial.

## Fase 6: Sockets e rede

### Meta

Sair de um vocabulary BSD-shaped para uma ABI de socket realmente compativel o bastante para software portado.

### Trabalho

- revisar `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`
- completar `setsockopt`/`getsockopt`
- alinhar `sockaddr`, `msghdr`, `iovec`
- planejar `ioctl` de interface de rede
- alinhar semanticamente:
  - blocking vs non-blocking
  - `EWOULDBLOCK`
  - `EINPROGRESS`
  - `SO_ERROR`

### Referencias

- `compat/sys/sys/socket.h`
- `compat/sys/net/*`
- `compat/usr.bin/netstat/*`
- `compat/usr.bin/ftp/*`
- `compat/usr.bin/curl/*` quando aplicavel

## Fase 7: Sinais, tempo, `poll`, `select`, depois `kqueue`

### Meta

Dar base de runtime para software BSD multiprogramado e interativo.

### Ordem sugerida

1. `time` e `gettimeofday`
2. `nanosleep` e afins
3. `select`
4. `poll`
5. sinais basicos
6. `kqueue` apenas depois

### Motivo

`kqueue` e importante, mas `poll/select` desbloqueiam muito mais software mais cedo.

## Fase 8: ABIs de audio, input e dispositivos

### Meta

Levar a serio as ABIs de controle de dispositivo que ja comecaram a ser importadas.

### Audio

- aprofundar `audioio` BSD-shaped
- completar mixer ABI
- compatibilizar melhor `mixerctl`, `sndioctl`, `sndiod` quando for viavel

### Input

- estabilizar eventos de teclado e mouse
- mapear melhor semantica de teclado BSD onde necessario

### Storage

- revisar contratos de bloco, geometria, erros e flush/sync

### Rede

- estabilizar identificacao de NICs e superficie de controle

## Fase 9: ABI de hardware e driver boundary

### Meta

Usar o `compat/sys/dev` como referencia para melhorar as fronteiras entre kernel, servicos e drivers.

### Trabalho

- padronizar enumeracao PCI para expor:
  - vendor/device
  - subsystem ids
  - class/subclass/prog-if
  - command/status
  - BARs IO/MMIO
  - secondary bus para bridges
- definir contrato para:
  - lease de IRQ
  - mapeamento de BAR
  - DMA buffer registration
  - notificacoes de hotplug no futuro

### Resultado esperado

Drivers `compat-*` deixam de ser casos especiais e passam a caber numa ABI de hardware consistente.

## Fase 10: Validacao por software real

### Meta

Medir progresso por programas reais do OpenBSD importados em `compat/`.

### Suites sugeridas

- basicos:
  - `cat`
  - `echo`
  - `grep`
  - `sed`
  - `printf`
  - `uname`
- interativos:
  - `mg`
  - `vi`
  - `tmux` em fase bem posterior
- rede:
  - `ping`
  - `ftp`
  - `netstat`
  - `ifconfig`
  - `route`
- audio:
  - `mixerctl`
  - `sndioctl`

### Criterio de aceite

Nao basta compilar.
Precisa:

- iniciar
- executar o fluxo basico
- retornar erros coerentes
- nao quebrar o userland legado do `vibeOS`

## Ordem de prioridade real

Se precisarmos escolher uma ordem agressivamente pragmatica:

1. inventario ABI
2. headers e tipos base
3. ELF e loader
4. launch/process/syscall error model
5. arquivos, `stat`, `fcntl`, `ioctl`
6. `termios` e tty
7. sockets e rede
8. `poll/select`
9. sinais
10. ABIs de dispositivo mais profundas

## O que nao fazer

- nao portar subsistemas inteiros do OpenBSD sem fronteira ABI definida
- nao inventar ABI nova quando a BSD ja resolve o problema
- nao quebrar structs compartilhadas para "limpar" o kernel
- nao aceitar incompatibilidade silenciosa como etapa temporaria permanente
- nao misturar "referencia semantica" com "copiar codigo cegamente"

## Primeiro lote recomendado

O melhor primeiro lote para gerar progresso concreto e visivel e:

- inventario ABI em tabela
- consolidacao de `errno`, `stat`, `fcntl`, `ioctl`
- endurecimento do ELF/loader
- formalizacao de retorno e erro de syscall
- `termios` MVP
- `poll/select` MVP

Esse lote ja melhora bastante a taxa de sucesso de software BSD simples e prepara o terreno para fases mais caras como sinais, rede completa e pseudo-tty.

## Definicao de pronto

Esta iniciativa pode ser considerada madura quando:

- as ABIs compartilhadas estiverem documentadas e versionadas
- o kernel aceitar caminhos legados e novos sem quebrar userland
- um conjunto representativo de programas BSD simples rodar sem shims especiais por app
- os servicos do microkernel expuserem contratos estaveis e previsiveis
- a evolucao futura do kernel estiver presa a testes de compatibilidade ABI

## Andamento atual da Fase 2

- o loader AppFS agora distingue layout atual de header e layout legado, em vez de tratar qualquer `abi_version = 1` como se fosse identico
- o kernel continua aceitando o layout atual sem rebuild geral
- aliases de `load_address` da geracao `0x02000000/0x04000000/0x06000000` passam a ser reconhecidos como a mesma classe de arena para compatibilidade controlada
- o layout AppFS mais antigo, que nao carregava `load_address` no header, passa a falhar com diagnostico explicito em vez de incompatibilidade silenciosa
- o loader ELF deixa de aceitar so `System V/GNU` e passa a tolerar tambem `FreeBSD/OpenBSD` nos casos simples de `ELF32/i386`


Melhorei a base de compat OpenBSD sem mexer na ABI já consumida pelo userland atual. Entraram `errno`, `ioctl`, `ioccom`, `termios`, `select`, `poll` e `sys/time`, além de uma expansão cuidadosa de `fcntl` em [headers/fcntl.h](/home/mel/Documentos/vibe-os/headers/fcntl.h#L13), [headers/errno.h](/home/mel/Documentos/vibe-os/headers/errno.h#L1), [headers/sys/ioctl.h](/home/mel/Documentos/vibe-os/headers/sys/ioctl.h#L10), [headers/sys/termios.h](/home/mel/Documentos/vibe-os/headers/sys/termios.h#L82), [headers/sys/select.h](/home/mel/Documentos/vibe-os/headers/sys/select.h#L32), [headers/poll.h](/home/mel/Documentos/vibe-os/headers/poll.h#L7) e [headers/sys/time.h](/home/mel/Documentos/vibe-os/headers/sys/time.h#L6).

O ponto mais útil agora é que a camada pública já fala muito mais “BSD”: temos números de `errno` compatíveis, `_IO/_IOR/_IOW`, `winsize`, `TIOCGWINSZ`, `TIOCGETA`, `FIONBIO`, `struct termios`, `cfmakeraw`, `fd_set`, `pollfd`, `timeval` e `timespec`. Também deixei stubs inline conservadores para `ioctl`, `fcntl`, `poll` e `select`: eles suportam os casos seguros e retornam `ENOSYS`/`ENOTTY` quando o microkernel ainda não tem backend real, em vez de mentir semântica. O `TIOCGWINSZ` já tenta inferir tamanho real da tela via `SYSCALL_GFX_INFO` em [headers/sys/ioctl.h](/home/mel/Documentos/vibe-os/headers/sys/ioctl.h#L43).

Verificação: `make -j4` passou inteiro.

Limite atual, de forma bem franca: isso melhora bastante a compat de headers e ABI de superfície, mas ainda não é compat binária completa de OpenBSD. O próximo passo certo é ligar essas interfaces a backends reais no kernel e no runtime: `ioctl`/TTY, `poll`/`select`, `fcntl(F_SETFL)` e depois `stat`/`off_t`/socket ioctls.
Rodei o teste no QEMU com `make QEMU=qemu-system-x86_64 validate-startx-800x600`.

Resultado: falhou antes do desktop subir. O cenário não chegou em `desktop.app: launch startx` nem em `desktop: session ready`. O serial log do run ficou em `/tmp/vmod-ylnip_46/s.log`.

O ponto útil do log é este:
```text
host: argv audiosvc apply-settings
lang: pid ok before reset
lang: pid ok after reset
lang: try_run begin
service: storage request type=16 from pid=12 to pid=1
scheduler: block pid=12 ... deadline=524

host: argv userland
lang: pid ok before reset
lang: pid ok after reset
lang: try_run begin
service: storage request type=16 from pid=13 to pid=1
scheduler: block pid=13 ... deadline=686

scheduler: timeout pid=12 ...
scheduler: timeout pid=13 ...
timer: tick=2000
```

Leitura prática: o boot trava no caminho AppFS/storage antes de `userland.app` ou `startx` realmente arrancarem. Então a melhoria de ABI não quebrou o build, mas ainda não resolveu o gargalo real do desktop neste cenário; o próximo alvo certo é `userland/modules/lang_loader.c` no fluxo de leitura do catálogo/AppFS e a superfície `storage request type=16`.

Se quiser, eu sigo agora direto nessa trilha e fecho o loop: reproduzo esse timeout com logs mais cirúrgicos no loader/storage e tento corrigir a causa real.

Continuei a implementação da trilha de ABI e fechei a parte que faltava no caminho de loader/storage.

Entrou suporte explícito a backend de storage no userland service host: adicionei `sys_storage_backend_*` em [headers/userland/modules/include/syscalls.h](/home/mel/Documentos/vibe-os/headers/userland/modules/include/syscalls.h), implementei as wrappers em [userland/modules/syscalls.c](/home/mel/Documentos/vibe-os/userland/modules/syscalls.c) e troquei o host de storage em [userland/bootstrap_service.c](/home/mel/Documentos/vibe-os/userland/bootstrap_service.c) para usar esse caminho direto ao backend, em vez de depender do syscall público de storage dentro do próprio serviço. Isso deixou o fluxo bem mais robusto para AppFS e persistência durante boot modular.

As mudanças anteriores no loader ficaram preservadas e agora trabalham melhor juntas: [lang/include/vibe_app.h](/home/mel/Documentos/vibe-os/lang/include/vibe_app.h) mantém os layouts legado/atual do header AppFS, [userland/modules/lang_loader.c](/home/mel/Documentos/vibe-os/userland/modules/lang_loader.c) decodifica os headers com fallback/controlado e aceita os aliases antigos de arena, e [kernel/exec/elf_loader.c](/home/mel/Documentos/vibe-os/kernel/exec/elf_loader.c) passou a tolerar `ELFOSABI_FREEBSD` e `ELFOSABI_OPENBSD` para os casos simples de `ELF32/i386`.

Verificação: `make -j4` passou. `make QEMU=qemu-system-x86_64 validate-startx-800x600` ficou muito melhor e passou em 5 de 6 cenários; o único fail foi `input-restart-desktop`, mas o replay isolado desse cenário passou com `python3 tools/validate_modular_apps.py --scenario input-restart-desktop --expect-boot-mode 800x600 --image build/boot.img --report build/input-restart-validation.md --qemu qemu-system-x86_64 --memory-mb 3072`, então no momento isso parece mais intermitência da suíte completa do que regressão funcional direta dessa ABI.