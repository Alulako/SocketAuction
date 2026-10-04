# Arquitetura do SocketAuction

## Visão geral

O SocketAuction usa TCP/IPv4 e arquitetura cliente-servidor. O servidor mantém um leilão único em memória e cria uma thread POSIX para cada conexão. O cliente usa uma thread para ler comandos e outra para receber respostas e eventos enviados pelo servidor.

```text
Cliente Ana ---- TCP ----+
                         |
Cliente João --- TCP ----+---- Servidor
                         |       |-- uma thread por conexão
Cliente Bia ---- TCP ----+       |-- registro de usuários
                                 |-- estado e histórico do leilão
                                 +-- notificações para outros clientes
```

## Conexões e encerramento

O servidor abre um socket de escuta com `socket()`, `bind()` e `listen()`. Um laço com `poll()` verifica periodicamente se foi pedido o encerramento e chama `accept()` quando há novas conexões.

Cada conexão aceita é registrada na lista de sessões ativas antes da criação da thread. Essa lista abrange inclusive clientes que ainda não fizeram login. A thread remove sua sessão e fecha o socket ao terminar.

Ao receber `SIGINT` ou `SIGTERM`, o servidor para de aceitar conexões, fecha o socket de escuta, chama `shutdown()` nas sessões ativas e aguarda que todas sejam removidas antes de encerrar. Os clientes percebem o fechamento da conexão e encerram mesmo sem digitar `QUIT`.

## Protocolo

As mensagens são linhas de texto finalizadas com `\n`. O servidor só processa um comando depois de receber o terminador. Se ocorrer EOF antes disso, a linha parcial é descartada. Mensagens acima do limite são drenadas até o terminador, evitando interpretar fragmentos como novos comandos.

O servidor é a única fonte de verdade para o estado do leilão.

## Concorrência

São usadas travas distintas, com responsabilidades definidas:

| Trava | Responsabilidade |
|---|---|
| `auction_mutex` | Protege o maior lance, o ofertante e o histórico. |
| `client_registry_mutex` | Protege a tabela de usuários autenticados. |
| `client_send_mutexes` | Serializa respostas e eventos enviados a cada cliente, sem um bloqueio global para todos os sockets. |
| `bid_order_mutex` | Mantém a ordem entre aceitar um lance e transmitir sua notificação. |
| `sessions_mutex` | Protege a lista de todas as conexões ativas durante a execução e o encerramento. |

A comparação do novo valor e a atualização do maior lance e do histórico acontecem na mesma seção crítica. Durante o broadcast, o servidor duplica os descritores dos clientes autenticados enquanto consulta a tabela e a libera antes de enviar os dados. Isso evita manter o registro global bloqueado enquanto espera respostas da rede.

O envio por conexão possui tempo máximo configurado de 500 ms por chamada bloqueante de `send()`. Um cliente muito lento ainda pode atrasar temporariamente a sequência de notificações; esse limite evita uma espera indefinida. Em caso de falha no envio de um evento, a conexão problemática é encerrada.

## Fluxo de um lance

1. A thread do cliente recebe `BID|valor`.
2. O valor e o login são validados.
3. O servidor serializa a operação de lance com `bid_order_mutex`.
4. Sob `auction_mutex`, compara o valor, atualiza o estado e registra o histórico.
5. Envia `BID_ACCEPTED` ao ofertante ou `BID_REJECTED` quando necessário.
6. Para lances aceitos, notifica os outros clientes com `EVENT|NEW_BID`.

## Limites e escopo

- Uma instância do servidor mantém um leilão em memória.
- O item inicial é `Notebook`, com lance inicial `1000`.
- O registro comporta até 32 usuários autenticados.
- São mantidos os 32 lances aceitos mais recentes.
- Não há banco de dados, pagamentos ou persistência após reiniciar o servidor.
- O projeto foi pensado para ambiente Linux e utiliza C11, POSIX Threads e sockets do sistema.
