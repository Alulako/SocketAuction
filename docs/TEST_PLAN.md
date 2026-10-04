# Plano de testes

Os testes automatizados são executados em Linux e usam `make`, Bash e, para os testes complementares, Python 3. Os testes escritos em Python utilizam apenas a biblioteca padrão e não são dependências da aplicação.

## Compilação

```bash
make clean
make
```

A compilação utiliza `-std=c11 -Wall -Wextra -Wpedantic -O2 -pthread`.

## Concorrência de lances

```bash
make test-m5
```

Abre 20 clientes na mesma janela de tempo por rodada, envia lances concorrentes e verifica o maior lance e seu ofertante ao final. O cenário é repetido cinco vezes.

## Notificações assíncronas

```bash
make test-m6
```

Mantém um usuário conectado como observador e verifica que ele recebe o evento `EVENT|NEW_BID` quando outro usuário envia um lance aceito.

## Histórico e sincronização inicial

```bash
make test-m7
```

Valida a consulta ao histórico, a ausência de lances rejeitados e o envio do estado atual do leilão após o login.

## Tratamento de falhas

```bash
make test-m8
```

Verifica mensagens acima do limite do protocolo, recuperação após entrada inválida, desconexão abrupta, continuidade do atendimento e encerramento do servidor.

## Testes complementares de conexão

```bash
make test-edges
```

O arquivo `tests/test_connection_edges.py` cobre situações encontradas na revisão técnica:

1. Uma linha TCP sem `\n`, seguida de EOF, não deve ser executada, mesmo quando contém um comando ou lance aparentemente válido. Linhas vazias retornam erro sem fechar a conexão.
2. O cliente deve terminar após o encerramento do servidor, ainda que sua entrada padrão permaneça aberta e o usuário não digite mais nada. O servidor deve fechar também conexões de clientes ainda não autenticados.
3. O servidor deve encerrar conexões ainda não autenticadas ao receber `SIGINT`.
4. Sob lances concorrentes, as notificações recebidas por um observador devem seguir a ordem dos lances aceitos, e o estado final deve conter o maior valor.

## Suíte completa

```bash
make test-all
```

Também é recomendado demonstrar manualmente duas ou mais sessões simultâneas em terminais separados e, quando possível, verificar a comunicação entre máquinas diferentes na mesma rede.
