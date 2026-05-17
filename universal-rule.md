# Universal Rule

## O kernel nunca pode quebrar o userland

Esta e a regra universal do `vibeOS`.

Nunca, em hipotese alguma, uma mudanca no kernel pode quebrar o userland existente.

## Consequencias praticas

- Compatibilidade de ABI vem antes de limpeza interna.
- Syscalls, estruturas compartilhadas, formatos de mensagem, contratos de servico e handshakes de boot devem permanecer compativeis.
- Se uma mudanca no kernel exigir comportamento novo, o caminho correto e adicionar compatibilidade, versionamento ou fallback, nunca remover suporte de forma silenciosa.
- O loader, o microkernel e os drivers devem tratar binarios e servicos antigos como carga valida sempre que for tecnicamente possivel.
- Regressao de userland é falha de arquitetura, nao detalhe de implementacao.

## Regra de decisao

Se existir conflito entre:

1. simplificar o kernel
2. preservar o userland

a prioridade e sempre preservar o userland.

## Formas aceitas de evoluir

- adicionar campos novos mantendo compatibilidade binaria
- introduzir novas versoes de ABI sem quebrar as anteriores
- detectar capacidades em tempo de execucao
- manter caminhos legados ativos
- emitir avisos, nunca quebras silenciosas

## Formas proibidas de evoluir

- mudar ABI compartilhada sem camada de compatibilidade
- reinterpretar estruturas antigas de forma incompatível
- remover syscall, mensagem ou contrato usado pelo userland
- exigir rebuild do userland como precondicao para boot do sistema
- aceitar breakage de apps e servicos como custo normal de refactor

## Mandamento

O kernel serve o userland.
Se o kernel quebra o userland, o kernel esta errado.
