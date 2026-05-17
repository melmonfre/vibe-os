# Foxxo.ai Async Kernel Hook

## Objetivo

Definir um hook assíncrono para `foxxo.ai` conversar com o kernel, e o kernel responder de volta, sem bloquear bootstrap, input, desktop ou outros caminhos criticos.

Esta integracao segue a regra universal do projeto:

- nao quebra ABI existente
- nao remove syscalls/contratos antigos
- adiciona um caminho novo, opt-in, compatível com o userland atual

## Superficie nova

Foram adicionados dois syscalls novos:

- `SYSCALL_MESSAGE_POST = 112`
- `SYSCALL_MESSAGE_RECV = 113`

E um envelope compartilhado:

```c
struct mk_async_message {
    uint32_t abi_version;
    uint32_t flags;
    uint32_t type;
    uint32_t source_pid;
    uint32_t target_pid;
    uint32_t target_service;
    uint32_t payload_size;
    uint8_t payload[256];
};
```

Flags suportadas:

- `MK_ASYNC_MESSAGE_TO_PID`: roteia diretamente para `target_pid`
- `MK_ASYNC_MESSAGE_TO_SERVICE`: resolve o worker atual de `target_service`
- `MK_ASYNC_MESSAGE_DEFERRED`: marca a mensagem como trabalho de baixa urgencia
- `MK_ASYNC_MESSAGE_REQUIRE_TARGET`: documenta que a entrega nao deve cair em fallback silencioso

Helpers userland:

- `sys_message_post(struct mk_async_message *message)`
- `sys_message_receive(struct mk_async_message *message, uint32_t timeout_ticks)`

## Semantica

`sys_message_post()` e fire-and-forget. O kernel resolve o destino e enfileira a mensagem de forma assíncrona na fila IPC do processo-alvo.

`sys_message_receive()` permite esperar por mensagem com `timeout_ticks`, o que evita loop com `yield()` ou polling agressivo.

`source_pid` e preenchido pelo kernel no envio. Isso permite reply assíncrono sem handshake paralelo: quem recebeu pode responder mirando o `source_pid` original.

## Regra de uso

O hook do `foxxo.ai` nao deve rodar dentro de loop critico de servico como `inputsvc`, `videosvc` ou no supervisor `init`.

O desenho recomendado e:

1. `foxxod` roda como task separada
2. `foxxod` espera mensagens com `sys_message_receive(..., timeout)`
3. `foxxod` traduz para o protocolo da ferramenta
4. respostas voltam via `sys_message_post()`

Se a tarefa for opcional ou pesada, marque a mensagem com `MK_ASYNC_MESSAGE_DEFERRED` e processe isso em worker de baixa prioridade no lado do broker.

## Topologia recomendada

### Foxxo.ai -> kernel

`foxxod` envia uma mensagem para um servico especifico:

```c
struct mk_async_message msg;

memset(&msg, 0, sizeof(msg));
msg.abi_version = MK_ASYNC_MESSAGE_ABI_VERSION;
msg.flags = MK_ASYNC_MESSAGE_TO_SERVICE |
            MK_ASYNC_MESSAGE_DEFERRED |
            MK_ASYNC_MESSAGE_REQUIRE_TARGET;
msg.type = 0x46580001u; /* foxxo request */
msg.target_service = MK_SERVICE_FILESYSTEM; /* exemplo */
msg.payload_size = payload_len;
memcpy(msg.payload, payload, payload_len);
sys_message_post(&msg);
```

### Kernel/service -> foxxo.ai

O destinatario responde para o `source_pid` recebido:

```c
reply.abi_version = MK_ASYNC_MESSAGE_ABI_VERSION;
reply.flags = MK_ASYNC_MESSAGE_TO_PID;
reply.type = 0x46580002u; /* foxxo reply */
reply.target_pid = request.source_pid;
reply.payload_size = reply_len;
memcpy(reply.payload, reply_payload, reply_len);
sys_message_post(&reply);
```

## Contrato de tipos

Para nao conflitar com mensagens internas antigas, reserve um intervalo proprio para o broker:

- `0x46580001`: request
- `0x46580002`: reply
- `0x46580003`: event
- `0x46580004`: error

O kernel nao precisa entender semanticamente esses tipos. Ele so roteia.

## Regras de implementacao

- use esse hook apenas para fluxos assíncronos
- nao use esse canal para substituir ABI antiga de servicos ja estabilizados
- nao misture `sys_service_receive()` e `sys_message_receive()` no mesmo loop consumidor
- para payload maior que 256 bytes, publique um `transfer_id` no payload e mova o corpo pelo contrato de `transfer`
- respostas devem ser idempotentes quando possivel
- erros devem voltar como mensagem explicita, nunca como silencio infinito

## Checklist para subir o broker `foxxod`

- criar app/worker dedicado para o broker
- usar `sys_message_receive(..., timeout)` no loop principal
- separar request de alta prioridade de trabalho `DEFERRED`
- responder pelo `source_pid`
- registrar telemetria basica de fila, timeout e falha de entrega
- manter fallback compativel: se o broker nao existir, o resto do sistema continua bootando e funcionando

## Estado implementado em arvore

Agora existe um app modular `foxxod` no AppFS com comandos basicos:

- `foxxod serve`
- `foxxod status`
- `foxxod ping [texto]`
- `foxxod event <texto>`
- `foxxod request <service-id> <texto>`

O broker:

- exporta `pid` em `/runtime/foxxod.pid`
- exporta telemetria em `/runtime/foxxod-status.txt`
- registra eventos em `/runtime/foxxod-events.log`
- aceita `PING`/`EVENT`
- faz relay assíncrono de `SERVICE_REQUEST` para `target_service`
- encaminha `SERVICE_REPLY` ou `ERROR` de volta ao pid original usando `correlation_id`

O bootstrap continua opcional: nada no boot depende do `foxxod` existir.

## Observacao importante

O kernel continua servindo o userland antigo sem exigir rebuild ou migracao forcada. O hook do `foxxo.ai` e uma extensao assíncrona nova, nao um corte de compatibilidade.
