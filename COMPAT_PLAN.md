Você está trabalhando no sistema operacional VibeOS.

O repositório já contém uma árvore de compatibilidade baseada no código-fonte do OpenBSD dentro do diretório:

compat/

Essa árvore contém diretórios importados de:

OpenBSD src

exemplo:

compat/
  bin/
  sbin/
  usr.bin/
  usr.sbin/
  lib/
  libexec/
  gnu/
  share/
  sys/
  include/
  etc/

IMPORTANTE:

Esse código já foi importado.
Sua tarefa NÃO é clonar novamente o repositório.

Sua tarefa é IMPLEMENTAR a integração dessa árvore com o sistema atual.

Mas isso deve ser feito de forma incremental e segura.

--------------------------------------------------------------------

OBJETIVO PRINCIPAL

Transformar a árvore compat/ em uma base funcional para portar utilitários BSD reais.

Esses utilitários devem:

• compilar usando a toolchain atual
• usar a camada compat do sistema
• ser instalados no VFS durante o boot
• ser executáveis diretamente pela shell

Mas sem quebrar:

• boot
• kernel
• shell
• desktop
• sistema de build

--------------------------------------------------------------------

REGRA CRÍTICA

É PROIBIDO tentar compilar toda a árvore compat de uma vez.

Isso quebraria o sistema.

Você deve implementar suporte incremental.

--------------------------------------------------------------------

FASE 1 — INDEXAR A ÁRVORE compat

Primeiro:

1. percorrer todo o diretório compat/
2. identificar cada subdiretório
3. classificar:

Tipo:
- utilitário userland
- biblioteca
- kernel code
- documentação
- share
- scripts

Criar um inventário em:

compat/metadata/compat_inventory.txt

Cada entrada deve conter:

diretório
tipo
dependências prováveis
viabilidade de port

--------------------------------------------------------------------

FASE 2 — CLASSIFICAR POR NÍVEL DE DIFICULDADE

Classificar utilitários em níveis:

Tier 1 (muito fáceis)
echo
cat
wc
true
printf

Tier 2 (fáceis)
head
tail
grep
cut
tr

Tier 3 (médios)
sed
sort
find
uniq

Tier 4 (complexos)
less
vi
nano-like
awk

Tier 5 (não suportados ainda)
coisas que dependem de:
fork
exec
signals
tty complexo
mmap

--------------------------------------------------------------------

FASE 3 — CRIAR CAMADA COMPAT BSD

Implementar uma camada de compatibilidade que permita compilar utilitários BSD.

Criar diretórios se necessário:

compat_runtime/
compat/libc/
compat/bsd/
compat/posix/
compat/term/

Implementar funções mínimas necessárias:

MEMORY
malloc
free
realloc
calloc

STRING
strlen
strcmp
strncmp
strchr
strcpy
memcpy
memset
memmove
memcmp

STDIO
printf
snprintf
vsnprintf
puts
putchar
getchar

POSIX I/O
open
read
write
close
lseek
stat
fstat
isatty

PROCESS
exit
abort

UTIL
atoi
strtol
qsort

errno

TERMINAL
funções básicas de terminal texto

Essa camada deve usar:

• VFS do sistema
• console atual
• driver de teclado atual

--------------------------------------------------------------------

FASE 4 — BUILD DE UTILITÁRIOS

Criar um sistema de build para utilitários compat.

Exemplo:

build/compat/bin/echo.app
build/compat/bin/cat.app
build/compat/bin/wc.app

Esses apps NÃO podem ser linkados dentro do kernel.

Eles devem ser executáveis separados.

--------------------------------------------------------------------

FASE 5 — INSTALAÇÃO NO VFS

Durante o boot do sistema:

utilitários compat que foram compilados com sucesso devem ser instalados no VFS.

Exemplo de caminhos:

/bin
/usr/bin
/compat/bin

Escolher um layout consistente.

A shell deve conseguir encontrar esses executáveis automaticamente.

--------------------------------------------------------------------

FASE 6 — INTEGRAÇÃO COM SHELL

A shell deve:

1. procurar comandos internos
2. procurar executáveis no VFS
3. executar utilitários compat automaticamente

Exemplo esperado:

echo hello
cat arquivo.txt
wc arquivo.txt
grep foo arquivo.txt

Sem hardcode para cada utilitário.

--------------------------------------------------------------------

FASE 7 — PORT INCREMENTAL

Implementar utilitários nessa ordem:

1
echo
cat
wc
printf

2
head
tail
grep
cut
tr

3
sed
sort
uniq
find

4
less

Não tentar portar editores complexos antes de estabilizar o terminal.

--------------------------------------------------------------------

DIRETÓRIOS QUE NÃO DEVEM SER INTEGRADOS AO SISTEMA

compat/sys/

O código dentro de compat/sys deve permanecer apenas como referência.

Ele NÃO deve ser integrado ao kernel do VibeOS.

--------------------------------------------------------------------

VALIDAÇÃO OBRIGATÓRIA

Após cada fase verificar:

BOOT
sistema ainda inicia

SHELL
shell continua funcional

DESKTOP
startx continua funcionando

TAMANHO
kernel.bin não aumentou por causa de compat

EXECUÇÃO

testar:

echo hello
cat arquivo.txt
wc arquivo.txt
head arquivo.txt
tail arquivo.txt
grep palavra arquivo.txt

--------------------------------------------------------------------

PROVA DE FUNCIONAMENTO

A resposta final deve mostrar:

estrutura compat final
camada compat implementada
utilitários compilados
onde são instalados no VFS
prova de execução na shell
prova de que boot e interface continuam intactos

--------------------------------------------------------------------

IMPORTANTE

Não tentar portar tudo.

Port incremental e seguro.

Objetivo:

transformar compat/ em base real para utilitários BSD no VibeOS.