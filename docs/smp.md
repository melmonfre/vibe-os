# SMP Plan

Objetivo: evoluir o VibeOS de kernel single-core cooperativo para kernel SMP real, sem perder compatibilidade com o boot atual e mantendo fallback seguro para hardware sem APIC/SMP.

## Fase 0 - Fundacao

- [X] Detectar topologia de CPU no boot via `CPUID`
- [X] Detectar tabelas Intel MP em low memory quando disponiveis
- [X] Expor API de topologia (`cpu_count`, `boot_cpu_id`, `smp_capable`)
- [X] Logar no boot quando hardware multiprocessado for detectado
- [X] Criar estado per-CPU do kernel
- [X] Criar spinlocks reutilizaveis no kernel

## Fase 1 - Local APIC

- [X] Adicionar helpers `rdmsr/wrmsr` e acesso seguro ao MSR `IA32_APIC_BASE`
- [X] Implementar descoberta do endereco base do Local APIC
- [X] Implementar leitura/escrita MMIO do LAPIC
- [X] Implementar enable do Local APIC no BSP
- [X] Implementar helpers de EOI/IPI do LAPIC
- [ ] Validar fallback limpo para maquinas sem APIC

## Fase 2 - AP Startup

- [X] Reservar area baixa para trampoline dos APs
- [X] Adicionar codigo de trampoline 16/32-bit para AP
- [X] Enviar `INIT` + `SIPI` pelo LAPIC
- [X] Handshake BSP/AP com contador de CPUs iniciadas
- [X] Inicializar GDT/IDT basicas nos APs
- [X] Colocar APs em idle seguro ate scheduler SMP entrar

## Fase 3 - Scheduler SMP

- [X] Mudar `scheduler_current()` para estado per-CPU
- [X] Proteger run queue com spinlock
- [X] Separar contexto atual por CPU
- [X] Fazer `yield/schedule` funcionar corretamente em SMP
- [X] Garantir que interrupcoes e syscalls usam o estado da CPU atual
- [X] Manter fallback single-core como default seguro

Estado atual da ABI:

- `context_switch` cooperativo antigo deixou de ser o caminho principal do scheduler.
- Tasks agora guardam `trap frame` no proprio stack (`EIP/CS/EFLAGS` + `pusha`) e o retorno de `irq0`/`yield` volta pela stack escolhida pelo scheduler.
- `yield()` passou a entrar por trap dedicado (`int 0x81`) usando a mesma ABI de frame do timer, em vez de depender de chamada C pura.
- A preempcao por timer so e armada depois que o `init` bootstrap ja foi criado, evitando perder o bootstrap do kernel antes de haver uma run queue valida.
- `smp_init()` voltou a ser executado no boot quando a plataforma permite, os APs sobem e ficam em `sti; hlt` seguro, e um IPI dedicado acorda esses cores quando o scheduler pode entrar.
- O trampoline dos APs agora fica explicitamente fora/fora-do-caminho do allocator fisico util e o bring-up C do AP reforca a inicializacao basica com `gdt_init()` + `kernel_idt_init()`.
- Syscalls e helpers sensiveis de servicos passaram a consultar o `current_pid` via scheduler per-CPU centralizado, em vez de cada subsistema reler `scheduler_current()` por conta propria.
- O fallback padrao voltou a ser single-core seguro: o bring-up SMP agora so ativa quando o loader entrega `BOOTINFO_FLAG_EXPERIMENTAL_SMP`, alternado no `stage2` pela tecla `M`.
- O bootstrap e o smoke `validate-audio-hda-startup` voltaram a passar em QEMU com essa ABI nova.

## Fase 4 - Robustez

- [ ] Revisar heap para concorrencia
- [ ] Revisar timers/EOI/IRQ para APIC
- [ ] Revisar drivers de entrada/video/storage para reentrancia minima
- [ ] Adicionar testes de boot em hardware/QEMU com 2+ CPUs
- [~] Documentar riscos e knobs de ativacao

## Principios

- Ativar SMP gradualmente.
- Nao trocar o caminho principal de boot ate o bootstrap dos APs estar estavel.
- Manter o sistema buildavel e bootavel ao fim de cada fase.
- Usar PIC/single-core como fallback padrao ate o scheduler SMP estar correto.
