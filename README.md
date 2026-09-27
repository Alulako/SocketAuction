# SocketAuction — Leilão Online em Tempo Real

**Trabalho 1 — Redes de Computadores**

**Instituição:** Universidade de São Paulo (USP) — Instituto de Ciências Matemáticas e de Computação (ICMC)

**Curso:** Engenharia de Computação

**Disciplina:** Redes de Computadores

**Professor:** Prof. Dr. Rodolfo I. Meneguette

**Semestre:** 2º semestre de 2026

## Integrantes

- **Ana Luíza Lasta Kodama** — NUSP 14651204
- **Ana Beatriz** — sobrenome completo e NUSP a confirmar
- **Isabela Lima Silva** — NUSP 15678780

> Os dados completos de Ana Beatriz serão adicionados antes da entrega final.

---

## 1. Sobre o trabalho

O **SocketAuction** é uma aplicação cliente-servidor de **leilão online em tempo real**, desenvolvida em linguagem C para o Trabalho 1 da disciplina de Redes de Computadores.

A proposta do sistema é permitir que vários usuários se conectem simultaneamente a um servidor por meio de **sockets TCP**, façam login, consultem o estado atual do leilão e enviem lances. Quando um novo lance é aceito, os demais clientes conectados recebem a atualização automaticamente, sem precisar consultar o servidor.

O projeto foi desenvolvido para exercitar, na prática:

- programação com sockets;
- arquitetura cliente-servidor;
- protocolo de aplicação;
- comunicação TCP;
- múltiplas conexões simultâneas;
- threads POSIX (`pthread`);
- sincronização com mutexes;
- estado compartilhado;
- comunicação assíncrona servidor → cliente;
- tratamento de erros e desconexões;
- testes de concorrência e robustez.

A implementação utiliza apenas recursos da linguagem C e APIs POSIX disponíveis no ambiente Linux, sem bibliotecas externas de terceiros.

---

## 2. Funcionalidades

O sistema atualmente oferece:

- conexão de vários clientes ao mesmo servidor;
- uma thread no servidor para cada cliente conectado;
- login com nome de usuário;
- rejeição de nomes duplicados enquanto o usuário já estiver conectado;
- consulta dos usuários conectados;
- consulta do estado atual do leilão;
- envio e validação de lances;
- rejeição de lances inválidos ou menores/iguais ao lance atual;
- atualização sincronizada do maior lance;
- identificação do maior ofertante;
- broadcast em tempo real de novos lances;
- histórico dos últimos 32 lances aceitos;
- envio automático do estado atual do leilão após o login;
- remoção automática de usuários desconectados;
- tratamento de comandos desconhecidos;
- tratamento de mensagens maiores que o limite do protocolo;
- encerramento controlado do servidor com `SIGINT` ou `SIGTERM`;
- testes automatizados de concorrência, broadcast, histórico e robustez.

O item inicial configurado para demonstração é:

```text
Item: Notebook
Lance inicial: 1000
Maior ofertante: NONE
```

---

## 3. Arquitetura

A aplicação utiliza o modelo cliente-servidor sobre TCP.

```text
                         +----------------------+
                         |       server.c       |
                         |                      |
                         | Socket TCP principal |
                         +----------+-----------+
                                    |
                         accept() de conexões
                                    |
              +---------------------+---------------------+
              |                     |                     |
              v                     v                     v
        Thread cliente 1      Thread cliente 2      Thread cliente 3
              |                     |                     |
              |                     |                     |
           Cliente A             Cliente B             Cliente C
              \                     |                    /
               \                    |                   /
                +-------------------+------------------+
                                    |
                          Estado compartilhado
                         +----------------------+
                         | item                 |
                         | maior lance          |
                         | maior ofertante      |
                         | histórico de lances  |
                         +----------------------+
                                    |
                                  mutex
```

### Servidor

O `server.c` é responsável por:

1. criar o socket TCP;
2. associá-lo a uma porta com `bind()`;
3. colocar o socket em modo de escuta com `listen()`;
4. aceitar conexões usando `accept()`;
5. criar uma thread para cada cliente;
6. interpretar os comandos recebidos;
7. manter o estado global do leilão;
8. sincronizar o acesso ao estado compartilhado;
9. enviar respostas;
10. transmitir eventos para os demais clientes conectados;
11. remover clientes desconectados;
12. encerrar o socket principal de forma controlada.

### Cliente

O `client.c` possui duas linhas principais de execução:

- **thread principal:** lê comandos digitados pelo usuário e envia ao servidor;
- **thread receptora:** permanece aguardando respostas e eventos do servidor.

Essa separação permite receber um novo lance em tempo real mesmo quando o usuário não está digitando nenhum comando.

---

## 4. Por que TCP?

Foi utilizado **TCP (Transmission Control Protocol)** porque o sistema precisa de uma comunicação:

- orientada à conexão;
- confiável;
- ordenada;
- sem perda silenciosa de mensagens da aplicação.

Em um leilão, a ordem dos lances é importante. O servidor precisa receber os dados corretamente e manter uma visão consistente do maior lance registrado.

---

## 5. Concorrência e sincronização

Como vários clientes podem enviar comandos ao mesmo tempo, o servidor é multithread.

Cada conexão aceita gera uma nova thread:

```text
Cliente Ana   -> thread A
Cliente João  -> thread B
Cliente Bia   -> thread C
```

O maior lance é um estado compartilhado entre todas essas threads. Por isso, a operação de verificar e atualizar um lance é protegida por um `pthread_mutex_t`.

Conceitualmente:

```text
lock(mutex)

ler maior lance
comparar novo lance
atualizar valor
atualizar maior ofertante
registrar no histórico

unlock(mutex)
```

Sem essa proteção, dois clientes poderiam tentar atualizar o lance simultaneamente e provocar uma **race condition**.

O registro de clientes também possui seu próprio mutex, pois várias threads podem inserir, remover ou consultar usuários ao mesmo tempo.

---

## 6. Protocolo da aplicação

TCP fornece um fluxo de bytes, portanto o SocketAuction define seu próprio protocolo de aplicação.

Cada mensagem é textual e termina com uma quebra de linha (`\n`). Campos são separados por `|`.

### Cliente → servidor

| Comando | Função |
|---|---|
| `LOGIN|Ana` | Realiza login |
| `BID|1500` | Envia um novo lance |
| `STATUS` | Consulta o estado atual do leilão |
| `USERS` | Lista usuários conectados |
| `HISTORY` | Consulta o histórico de lances |
| `PING` | Verifica se a conexão está ativa |
| `QUIT` | Encerra a sessão |

### Servidor → cliente

Exemplos:

```text
OK|LOGIN|Ana
AUCTION|Notebook|1500|Ana
BID_ACCEPTED|1500|Ana
BID_REJECTED|Bid must be greater than 1500
USERS|2|Ana|Joao
HISTORY|2|Ana|1500|Joao|1800
EVENT|NEW_BID|1800|Joao
PONG
BYE
```

A especificação completa está em [docs/PROTOCOL.md](docs/PROTOCOL.md).

---

## 7. Exemplo de funcionamento

Suponha dois clientes conectados.

### Terminal da Ana

```text
$ ./client 127.0.0.1 8080

[CLIENT] Connected to 127.0.0.1:8080.

> LOGIN|Ana
[SERVER] OK|LOGIN|Ana
[SERVER] AUCTION|Notebook|1000|NONE

> BID|1500
[SERVER] BID_ACCEPTED|1500|Ana
```

### Terminal do João

```text
$ ./client 127.0.0.1 8080

> LOGIN|Joao
[SERVER] OK|LOGIN|Joao
[SERVER] AUCTION|Notebook|1000|NONE

[SERVER] EVENT|NEW_BID|1500|Ana
```

João recebe o novo lance automaticamente por meio do broadcast do servidor.

Se João oferecer um valor maior:

```text
> BID|1800
[SERVER] BID_ACCEPTED|1800|Joao
```

Ana receberá:

```text
[SERVER] EVENT|NEW_BID|1800|Joao
```

---

## 8. Estrutura do projeto

```text
SocketAuction/
├── server.c
├── client.c
├── Makefile
├── README.md
├── .gitignore
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT.md
│   ├── PROTOCOL.md
│   └── TEST_PLAN.md
│
└── tests/
    ├── test_m5_concurrency.sh
    ├── test_m6_broadcast.sh
    ├── test_m7_history.sh
    └── test_m8_robustness.sh
```

---

## 9. Ambiente utilizado

O projeto foi desenvolvido e testado no seguinte ambiente:

- **Sistema operacional:** Ubuntu 22.04.3 LTS via WSL2
- **Compilador:** GCC 11.4.0
- **Linguagem:** C11
- **Threads:** POSIX Threads (`pthread`)
- **Transporte:** TCP/IPv4

---

## 10. Compilação

No Linux/WSL, execute na raiz do projeto:

```bash
make
```

O Makefile compila utilizando:

```text
-std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
```

São gerados dois executáveis:

```text
server
client
```

Para remover os executáveis gerados:

```bash
make clean
```

---

## 11. Como executar localmente

### 1. Iniciar o servidor

Em um terminal:

```bash
./server 8080
```

Saída esperada:

```text
[SERVER] SocketAuction listening on port 8080.
[SERVER] M8: hardened protocol and graceful shutdown enabled.
```

### 2. Abrir um cliente

Em outro terminal:

```bash
./client 127.0.0.1 8080
```

### 3. Abrir outros clientes

Abra novos terminais e execute novamente:

```bash
./client 127.0.0.1 8080
```

Cada processo será uma conexão independente.

---

## 12. Execução em máquinas diferentes

O servidor aceita conexões em todas as interfaces IPv4 disponíveis (`INADDR_ANY`).

Para executar em computadores diferentes na mesma rede:

1. inicie o servidor na máquina que atuará como servidor;
2. descubra o IPv4 dessa máquina;
3. execute o cliente em outra máquina informando o IP do servidor.

Exemplo:

```bash
./server 8080
```

Em outra máquina:

```bash
./client 192.168.0.10 8080
```

As máquinas devem possuir conectividade entre si e a porta escolhida não pode estar bloqueada pelo firewall.

---

## 13. Tratamento de falhas

Foram implementadas verificações para diferentes situações de erro.

### Falha na criação ou configuração do socket

Erros em operações como:

```text
socket()
setsockopt()
bind()
listen()
accept()
connect()
send()
recv()
```

são detectados e tratados pelo programa.

### Cliente desconectado

Quando uma conexão é encerrada, o servidor:

1. detecta o fim da conexão;
2. remove o usuário do registro de clientes;
3. fecha o descritor correspondente;
4. mantém o servidor disponível para os demais usuários.

### SIGPIPE

O servidor ignora `SIGPIPE`. Assim, uma tentativa de envio para um cliente que acabou de se desconectar não encerra todo o processo servidor.

### Mensagens muito grandes

Uma linha maior que o limite aceito pelo protocolo é completamente consumida pelo servidor e recebe:

```text
ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size
```

A próxima mensagem continua sendo interpretada normalmente.

### Comandos inválidos

Comandos desconhecidos recebem:

```text
ERROR|UNKNOWN_COMMAND|Unknown command
```

### Encerramento do servidor

`SIGINT` e `SIGTERM` são tratados para fechar o socket de escuta e encerrar o servidor de maneira controlada.

---

## 14. Testes automatizados

O projeto possui uma suíte de testes executável pelo Makefile.

### Concorrência de lances

```bash
make test-m5
```

Executa 20 clientes concorrentes em cinco rodadas e verifica se o maior lance final permanece correto.

### Broadcast

```bash
make test-m6
```

Verifica se um cliente recebe automaticamente um evento de novo lance realizado por outro usuário.

### Histórico

```bash
make test-m7
```

Verifica:

- histórico compartilhado;
- ordem dos lances;
- exclusão de lances rejeitados;
- envio automático do estado atual após o login.

### Robustez

```bash
make test-m8
```

Verifica:

- mensagens maiores que o limite;
- recuperação do protocolo após mensagem inválida;
- desconexão abrupta;
- comandos malformados;
- disponibilidade do servidor após erros;
- encerramento controlado.

### Executar todos os testes

```bash
make test-all
```

Resultado esperado:

```text
[PASS] M5 concurrency stress test completed
[PASS] M6 real-time broadcast test completed
[PASS] M7 history/refinement test completed
[PASS] M8 robustness test completed
```

---

## 15. Roteiro sugerido para apresentação

Uma demonstração simples pode ser feita com três terminais:

**Terminal 1 — servidor**

```bash
./server 8080
```

**Terminal 2 — Ana**

```text
LOGIN|Ana
BID|1500
HISTORY
```

**Terminal 3 — João**

```text
LOGIN|Joao
USERS
BID|1800
HISTORY
```

Durante a demonstração é possível mostrar:

1. criação e inicialização do socket servidor;
2. conexão dos clientes;
3. criação de threads;
4. login simultâneo;
5. consulta de usuários;
6. envio de lances;
7. rejeição de lance inválido;
8. broadcast em tempo real;
9. histórico compartilhado;
10. desconexão de um cliente;
11. encerramento do servidor.

---

## 16. Documentação adicional

- [Arquitetura](docs/ARCHITECTURE.md)
- [Protocolo](docs/PROTOCOL.md)
- [Plano de testes](docs/TEST_PLAN.md)
- [Fluxo de desenvolvimento](docs/DEVELOPMENT.md)

---

## 17. Resumo técnico

| Item | Implementação |
|---|---|
| Linguagem | C11 |
| Compilador | GCC |
| Arquitetura | Cliente-servidor |
| Transporte | TCP/IPv4 |
| Concorrência | POSIX Threads |
| Modelo do servidor | Uma thread por cliente |
| Sincronização | `pthread_mutex_t` |
| Comunicação | Bidirecional |
| Atualizações | Broadcast assíncrono |
| Estado compartilhado | Leilão + clientes + histórico |
| Histórico | Últimos 32 lances aceitos |
| Testes | Automatizados via Makefile |
| Bibliotecas externas | Nenhuma |

---

## 18. Estado do projeto

- [x] Comunicação TCP cliente-servidor
- [x] Múltiplos clientes simultâneos
- [x] Threads
- [x] Login
- [x] Registro de usuários
- [x] Sistema de lances
- [x] Validação de lances
- [x] Mutexes
- [x] Broadcast em tempo real
- [x] Histórico
- [x] Tratamento de desconexões
- [x] Testes de concorrência
- [x] Testes de robustez
- [x] Makefile
- [x] Documentação

O projeto encontra-se funcional e pronto para demonstração, restando apenas completar o sobrenome e o NUSP de Ana Beatriz antes da entrega final.
