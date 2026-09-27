#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BACKLOG 8
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 32
#define MAX_USERNAME_LENGTH 31
#define MAX_ITEM_LENGTH 63
#define MAX_HISTORY 32
#define USERS_RESPONSE_SIZE 2048
#define RECEIVE_TOO_LONG (-2)

typedef struct {
    int active;
    int socket_fd;
    char username[MAX_USERNAME_LENGTH + 1];
} ClientEntry;

typedef struct {
    char username[MAX_USERNAME_LENGTH + 1];
    long long amount;
} BidRecord;

typedef struct {
    char item[MAX_ITEM_LENGTH + 1];
    long long current_bid;
    char highest_bidder[MAX_USERNAME_LENGTH + 1];
    BidRecord history[MAX_HISTORY];
    size_t history_count;
} AuctionState;

static ClientEntry client_registry[MAX_CLIENTS];
static pthread_mutex_t client_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

static AuctionState auction_state = {
    .item = "Notebook",
    .current_bid = 1000,
    .highest_bidder = "NONE",
    .history_count = 0
};
static pthread_mutex_t auction_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile sig_atomic_t stop_requested = 0;
static volatile sig_atomic_t listening_socket_fd = -1;

/* Fecha o socket de escuta para liberar o accept() durante o encerramento. */
static void handle_shutdown_signal(int signal_number) {
    (void)signal_number;
    stop_requested = 1;

    if (listening_socket_fd >= 0) {
        close((int)listening_socket_fd);
    }
}

/* Garante o envio completo da mensagem, mesmo quando send() envia apenas parte dos bytes. */
static int send_all(int socket_fd, const char *message) {
    size_t sent_total = 0;
    size_t message_length = strlen(message);
    int result = 0;

    pthread_mutex_lock(&send_mutex);

    while (sent_total < message_length) {
        ssize_t sent = send(
            socket_fd,
            message + sent_total,
            message_length - sent_total,
            0
        );

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = -1;
            break;
        }

        sent_total += (size_t)sent;
    }

    pthread_mutex_unlock(&send_mutex);
    return result;
}

/* Lê uma mensagem do protocolo até '\n' e descarta o restante caso ela ultrapasse o buffer. */
static ssize_t receive_line(int socket_fd, char *buffer, size_t capacity) {
    size_t used = 0;
    int too_long = 0;

    if (capacity == 0) {
        return -1;
    }

    for (;;) {
        char ch;
        ssize_t received = recv(socket_fd, &ch, 1, 0);

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (ch == '\n') {
            break;
        }

        if (ch == '\r') {
            continue;
        }

        if (used + 1 < capacity) {
            buffer[used++] = ch;
        } else {
            too_long = 1;
        }
    }

    buffer[used] = '\0';

    if (too_long) {
        return RECEIVE_TOO_LONG;
    }

    return (ssize_t)used;
}

/* Registra um usuário conectado. O mutex evita alterações simultâneas na tabela de clientes. */
static int register_client(int socket_fd, const char *username) {
    int free_index = -1;

    pthread_mutex_lock(&client_registry_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_registry[i].active) {
            if (strcmp(client_registry[i].username, username) == 0) {
                pthread_mutex_unlock(&client_registry_mutex);
                return -1;
            }
        } else if (free_index == -1) {
            free_index = i;
        }
    }

    if (free_index == -1) {
        pthread_mutex_unlock(&client_registry_mutex);
        return -2;
    }

    client_registry[free_index].active = 1;
    client_registry[free_index].socket_fd = socket_fd;
    snprintf(
        client_registry[free_index].username,
        sizeof(client_registry[free_index].username),
        "%s",
        username
    );

    pthread_mutex_unlock(&client_registry_mutex);
    return 0;
}

/* Remove o cliente da tabela quando a conexão é encerrada. */
static void unregister_client(int socket_fd) {
    pthread_mutex_lock(&client_registry_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_registry[i].active &&
            client_registry[i].socket_fd == socket_fd) {
            client_registry[i].active = 0;
            client_registry[i].socket_fd = -1;
            client_registry[i].username[0] = '\0';
            break;
        }
    }

    pthread_mutex_unlock(&client_registry_mutex);
}

/* Monta a lista de usuários enquanto o mutex impede alterações durante a leitura. */
static int send_users(int socket_fd) {
    char response[USERS_RESPONSE_SIZE];
    size_t used = 0;
    int count = 0;

    pthread_mutex_lock(&client_registry_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_registry[i].active) {
            count++;
        }
    }

    int written = snprintf(response, sizeof(response), "USERS|%d", count);
    if (written < 0 || (size_t)written >= sizeof(response)) {
        pthread_mutex_unlock(&client_registry_mutex);
        return -1;
    }
    used = (size_t)written;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!client_registry[i].active) {
            continue;
        }

        written = snprintf(
            response + used,
            sizeof(response) - used,
            "|%s",
            client_registry[i].username
        );

        if (written < 0 || (size_t)written >= sizeof(response) - used) {
            pthread_mutex_unlock(&client_registry_mutex);
            return -1;
        }

        used += (size_t)written;
    }

    pthread_mutex_unlock(&client_registry_mutex);

    if (used + 2 > sizeof(response)) {
        return -1;
    }

    response[used++] = '\n';
    response[used] = '\0';

    return send_all(socket_fd, response);
}

/* Converte o valor textual do lance e rejeita formatos inválidos ou valores não positivos. */
static int parse_bid_amount(const char *text, long long *amount) {
    if (text == NULL || *text == '\0') {
        return -1;
    }

    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);

    if (errno == ERANGE || end == text || *end != '\0' || parsed <= 0) {
        return -1;
    }

    *amount = parsed;
    return 0;
}

/* Deve ser chamada com auction_mutex já adquirido para manter estado e histórico coerentes. */
static void append_bid_history_locked(const char *username, long long amount) {
    if (auction_state.history_count == MAX_HISTORY) {
        memmove(
            &auction_state.history[0],
            &auction_state.history[1],
            (MAX_HISTORY - 1) * sizeof(auction_state.history[0])
        );
        auction_state.history_count--;
    }

    BidRecord *record = &auction_state.history[auction_state.history_count++];
    snprintf(record->username, sizeof(record->username), "%s", username);
    record->amount = amount;
}

/* Copia o estado atual do leilão sob mutex antes de enviá-lo ao cliente. */
static int send_auction_status(int socket_fd) {
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&auction_mutex);
    int written = snprintf(
        response,
        sizeof(response),
        "AUCTION|%s|%lld|%s\n",
        auction_state.item,
        auction_state.current_bid,
        auction_state.highest_bidder
    );
    pthread_mutex_unlock(&auction_mutex);

    if (written < 0 || (size_t)written >= sizeof(response)) {
        return -1;
    }

    return send_all(socket_fd, response);
}

/* Serializa o histórico de lances aceitos no formato definido pelo protocolo. */
static int send_bid_history(int socket_fd) {
    char response[BUFFER_SIZE];
    size_t used = 0;

    pthread_mutex_lock(&auction_mutex);

    int written = snprintf(
        response,
        sizeof(response),
        "HISTORY|%zu",
        auction_state.history_count
    );

    if (written < 0 || (size_t)written >= sizeof(response)) {
        pthread_mutex_unlock(&auction_mutex);
        return -1;
    }
    used = (size_t)written;

    for (size_t i = 0; i < auction_state.history_count; i++) {
        written = snprintf(
            response + used,
            sizeof(response) - used,
            "|%s|%lld",
            auction_state.history[i].username,
            auction_state.history[i].amount
        );

        if (written < 0 || (size_t)written >= sizeof(response) - used) {
            pthread_mutex_unlock(&auction_mutex);
            return -1;
        }

        used += (size_t)written;
    }

    pthread_mutex_unlock(&auction_mutex);

    if (used + 2 > sizeof(response)) {
        return -1;
    }

    response[used++] = '\n';
    response[used] = '\0';

    return send_all(socket_fd, response);
}

/* Notifica todos os clientes logados, exceto quem acabou de fazer o lance. */
static void broadcast_new_bid(
    int source_socket_fd,
    long long amount,
    const char *username
) {
    char event[BUFFER_SIZE];
    int written = snprintf(
        event,
        sizeof(event),
        "EVENT|NEW_BID|%lld|%s\n",
        amount,
        username
    );

    if (written < 0 || (size_t)written >= sizeof(event)) {
        return;
    }

    pthread_mutex_lock(&client_registry_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!client_registry[i].active ||
            client_registry[i].socket_fd == source_socket_fd) {
            continue;
        }

        if (send_all(client_registry[i].socket_fd, event) < 0) {
            fprintf(
                stderr,
                "[BROADCAST] Failed to notify %s.\n",
                client_registry[i].username
            );
        }
    }

    pthread_mutex_unlock(&client_registry_mutex);
}

static int process_bid(
    int socket_fd,
    const char *username,
    const char *amount_text
) {
    long long amount = 0;

    if (parse_bid_amount(amount_text, &amount) < 0) {
        return send_all(
            socket_fd,
            "BID_REJECTED|Invalid bid amount\n"
        );
    }

    long long previous_bid;

    /* A comparação e a atualização precisam ser atômicas para evitar race condition. */
    pthread_mutex_lock(&auction_mutex);
    previous_bid = auction_state.current_bid;

    if (amount <= auction_state.current_bid) {
        pthread_mutex_unlock(&auction_mutex);

        char response[BUFFER_SIZE];
        int written = snprintf(
            response,
            sizeof(response),
            "BID_REJECTED|Bid must be greater than %lld\n",
            previous_bid
        );

        if (written < 0 || (size_t)written >= sizeof(response)) {
            return -1;
        }

        return send_all(socket_fd, response);
    }

    auction_state.current_bid = amount;
    snprintf(
        auction_state.highest_bidder,
        sizeof(auction_state.highest_bidder),
        "%s",
        username
    );
    append_bid_history_locked(username, amount);
    pthread_mutex_unlock(&auction_mutex);

    printf("[BID] %s -> %lld\n", username, amount);

    char response[BUFFER_SIZE];
    int written = snprintf(
        response,
        sizeof(response),
        "BID_ACCEPTED|%lld|%s\n",
        amount,
        username
    );

    if (written < 0 || (size_t)written >= sizeof(response)) {
        return -1;
    }

    int send_result = send_all(socket_fd, response);
    broadcast_new_bid(socket_fd, amount, username);

    return send_result;
}

/* Exibe o endereço IPv4 e a porta de origem de uma nova conexão. */
static void print_client_address(const struct sockaddr_in *address) {
    char ip[INET_ADDRSTRLEN] = {0};

    if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) == NULL) {
        strcpy(ip, "unknown");
    }

    printf(
        "[SERVER] Client connected: %s:%u\n",
        ip,
        (unsigned int)ntohs(address->sin_port)
    );
}

/* Processa os comandos de uma conexão até QUIT, erro ou desconexão. */
static void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    char username[MAX_USERNAME_LENGTH + 1] = "";
    int logged_in = 0;

    for (;;) {
        ssize_t received = receive_line(client_fd, buffer, sizeof(buffer));

        if (received == RECEIVE_TOO_LONG) {
            if (send_all(
                    client_fd,
                    "ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size\n"
                ) < 0) {
                break;
            }
            continue;
        }

        if (received < 0) {
            perror("[SERVER] recv");
            break;
        }

        if (received == 0) {
            printf("[SERVER] Client disconnected.\n");
            break;
        }

        printf("[RECV] %s\n", buffer);

        if (strcmp(buffer, "PING") == 0) {
            if (send_all(client_fd, "PONG\n") < 0) {
                perror("[SERVER] send");
                break;
            }
            continue;
        }

        if (strcmp(buffer, "STATUS") == 0) {
            if (send_auction_status(client_fd) < 0) {
                perror("[SERVER] send status");
                break;
            }
            continue;
        }

        if (strncmp(buffer, "LOGIN|", 6) == 0) {
            const char *name = buffer + 6;
            size_t name_length = strlen(name);

            if (logged_in) {
                if (send_all(
                        client_fd,
                        "ERROR|ALREADY_LOGGED_IN|Client already logged in\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            if (name_length == 0 ||
                name_length > MAX_USERNAME_LENGTH ||
                strchr(name, '|') != NULL) {
                if (send_all(
                        client_fd,
                        "ERROR|INVALID_LOGIN|Invalid username\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            int register_result = register_client(client_fd, name);
            if (register_result == -1) {
                if (send_all(
                        client_fd,
                        "ERROR|USERNAME_IN_USE|Username already connected\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            if (register_result == -2) {
                if (send_all(
                        client_fd,
                        "ERROR|SERVER_FULL|Too many logged-in users\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            snprintf(username, sizeof(username), "%s", name);
            logged_in = 1;

            char response[BUFFER_SIZE];
            int written = snprintf(
                response,
                sizeof(response),
                "OK|LOGIN|%s\n",
                username
            );

            if (written < 0 || (size_t)written >= sizeof(response)) {
                break;
            }

            printf("[LOGIN] %s registered.\n", username);

            if (send_all(client_fd, response) < 0) {
                break;
            }

            /* Após o login, o cliente já recebe o estado atual do leilão. */
            if (send_auction_status(client_fd) < 0) {
                break;
            }

            continue;
        }

        if (strncmp(buffer, "BID|", 4) == 0) {
            if (!logged_in) {
                if (send_all(
                        client_fd,
                        "ERROR|NOT_LOGGED_IN|Login required\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            if (process_bid(client_fd, username, buffer + 4) < 0) {
                perror("[SERVER] process bid");
                break;
            }
            continue;
        }

        if (strcmp(buffer, "HISTORY") == 0) {
            if (!logged_in) {
                if (send_all(
                        client_fd,
                        "ERROR|NOT_LOGGED_IN|Login required\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            if (send_bid_history(client_fd) < 0) {
                perror("[SERVER] send history");
                break;
            }
            continue;
        }

        if (strcmp(buffer, "USERS") == 0) {
            if (!logged_in) {
                if (send_all(
                        client_fd,
                        "ERROR|NOT_LOGGED_IN|Login required\n"
                    ) < 0) {
                    break;
                }
                continue;
            }

            if (send_users(client_fd) < 0) {
                perror("[SERVER] send users");
                break;
            }
            continue;
        }

        if (strcmp(buffer, "QUIT") == 0) {
            (void)send_all(client_fd, "BYE\n");
            printf("[SERVER] Session closed by client.\n");
            break;
        }

        if (send_all(
                client_fd,
                "ERROR|UNKNOWN_COMMAND|Unknown command\n"
            ) < 0) {
            perror("[SERVER] send");
            break;
        }
    }

    if (logged_in) {
        unregister_client(client_fd);
        printf("[LOGOUT] %s removed from registry.\n", username);
    }
}

static void *client_thread(void *argument) {
    /* Cada thread assume a responsabilidade pelo descritor recebido. */
    int client_fd = *(int *)argument;
    free(argument);

    handle_client(client_fd);
    close(client_fd);

    return NULL;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    /* Um cliente desconectado não deve encerrar o servidor durante send(). */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);

    if (argc >= 2) {
        port = atoi(argv[1]);

        if (port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    listening_socket_fd = server_fd;

    int reuse = 1;
    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        ) < 0) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons((uint16_t)port);

    if (bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] SocketAuction listening on port %d.\n", port);
    printf("[SERVER] M8: hardened protocol and graceful shutdown enabled.\n");

    for (;;) {
        struct sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_address,
            &client_length
        );

        if (client_fd < 0) {
            if (stop_requested) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            break;
        }

        print_client_address(&client_address);

        /* O descritor é alocado porque a thread pode continuar após esta iteração do laço. */
        int *thread_client_fd = malloc(sizeof(*thread_client_fd));
        if (thread_client_fd == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *thread_client_fd = client_fd;

        pthread_t thread_id;
        int thread_error = pthread_create(
            &thread_id,
            NULL,
            client_thread,
            thread_client_fd
        );

        if (thread_error != 0) {
            fprintf(
                stderr,
                "[SERVER] pthread_create failed: %s\n",
                strerror(thread_error)
            );
            free(thread_client_fd);
            close(client_fd);
            continue;
        }

        /* A thread é detached porque o servidor não precisa fazer join depois. */
        thread_error = pthread_detach(thread_id);
        if (thread_error != 0) {
            fprintf(
                stderr,
                "[SERVER] pthread_detach failed: %s\n",
                strerror(thread_error)
            );
        }
    }

    if (!stop_requested) {
        close(server_fd);
    }
    listening_socket_fd = -1;

    printf("[SERVER] Shutdown complete.\n");
    return EXIT_SUCCESS;
}
