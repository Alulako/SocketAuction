# Protocolo de aplicação

O SocketAuction usa TCP/IPv4. Como TCP transporta um fluxo de bytes, cada comando e resposta deve terminar com `\n`. Os campos são separados por `|`.

O servidor aceita até 2047 caracteres de conteúdo por linha, sem contar o terminador. Uma linha maior recebe `ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size`, desde que seja encerrada corretamente com `\n`. Uma conexão encerrada antes do terminador não executa o comando incompleto.

## Comandos enviados pelo cliente

| Comando | Descrição | Login exigido |
|---|---|---|
| `LOGIN|nome` | Registra o nome de usuário na conexão. | Não |
| `STATUS` | Consulta o item, maior lance e ofertante. | Não |
| `BID|valor` | Envia um lance inteiro positivo. | Sim |
| `USERS` | Consulta os usuários autenticados. | Sim |
| `HISTORY` | Consulta os últimos 32 lances aceitos. | Sim |
| `PING` | Verifica a comunicação. | Não |
| `QUIT` | Encerra a sessão. | Não |

O nome de usuário deve ter entre 1 e 31 caracteres e não pode conter `|`. Dois clientes não podem usar o mesmo nome simultaneamente. Um cliente já autenticado não pode trocar de nome durante a conexão.

## Respostas e eventos do servidor

```text
OK|LOGIN|Ana
AUCTION|Notebook|1000|NONE
BID_ACCEPTED|1500|Ana
BID_REJECTED|Bid must be greater than 1500
USERS|2|Ana|Joao
HISTORY|2|Ana|1500|Joao|1800
EVENT|NEW_BID|1800|Joao
ERROR|NOT_LOGGED_IN|Login required
PONG
BYE
```

Após confirmar um login, o servidor envia também o estado atual do leilão. Cada lance aceito é confirmado ao próprio ofertante; os demais usuários autenticados recebem um evento assíncrono de novo lance.

Os eventos de lances aceitos são enviados na ordem em que os lances são registrados. O cliente pode receber um evento enquanto aguarda uma resposta a outro comando.

## Regras do leilão

- O valor inicial é 1000 e o item é `Notebook`.
- Um novo lance deve ser inteiro, positivo e estritamente maior que o lance registrado no momento do processamento.
- A comparação, a atualização do valor e o registro no histórico são sincronizados.
- Lances rejeitados não aparecem no histórico.
- O histórico mantém apenas os 32 lances aceitos mais recentes.
- Os valores são inteiros para simplificar a demonstração; o sistema não efetua transações financeiras reais.

## Casos de erro

- Nome inválido: `ERROR|INVALID_LOGIN|Invalid username`.
- Nome já utilizado: `ERROR|USERNAME_IN_USE|Username already connected`.
- Segundo login na mesma conexão: `ERROR|ALREADY_LOGGED_IN|Client already logged in`.
- Operação que exige login: `ERROR|NOT_LOGGED_IN|Login required`.
- Valor de lance malformado: `BID_REJECTED|Invalid bid amount`.
- Comando desconhecido: `ERROR|UNKNOWN_COMMAND|Unknown command`.
- Linha vazia: `ERROR|EMPTY_COMMAND|Empty command`.
- Mensagem longa demais: `ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size`.

Uma linha incompleta seguida de EOF é descartada, sem gerar resposta. Em caso de desconexão, o servidor libera o usuário da tabela de logins.
