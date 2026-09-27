#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 2048

typedef struct {
    int socket_fd;
} ReceiverArgs;

/* Envia a mensagem inteira, tratando envios parciais de send(). */
static int send_all(int socket_fd, const char *message) {
    size_t sent_total = 0;
    size_t message_length = strlen(message);

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
            return -1;
        }

        sent_total += (size_t)sent;
    }

    return 0;
}

/* Lê uma resposta completa do servidor até a quebra de linha do protocolo. */
static ssize_t receive_line(int socket_fd, char *buffer, size_t capacity) {
    size_t used = 0;

    if (capacity == 0) {
        return -1;
    }

    while (used + 1 < capacity) {
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

        if (ch != '\r') {
            buffer[used++] = ch;
        }
    }

    buffer[used] = '\0';
    return (ssize_t)used;
}

/* Mantém a recepção independente da entrada do usuário para permitir eventos assíncronos. */
static void *receiver_thread(void *argument) {
    ReceiverArgs *args = argument;
    char response[BUFFER_SIZE];

    for (;;) {
        ssize_t received = receive_line(
            args->socket_fd,
            response,
            sizeof(response)
        );

        if (received < 0) {
            perror("[CLIENT] recv");
            break;
        }

        if (received == 0) {
            printf("\n[CLIENT] Server closed the connection.\n");
            break;
        }

        printf("\n[SERVER] %s\n", response);
        fflush(stdout);

        if (strcmp(response, "BYE") == 0) {
            break;
        }

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

/* Configura a conexão e mantém a thread principal dedicada ao envio de comandos. */
int main(int argc, char *argv[]) {
    const char *server_ip = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }

    if (argc >= 3) {
        port = atoi(argv[2]);

        if (port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", server_ip);
        close(socket_fd);
        return EXIT_FAILURE;
    }

    if (connect(
            socket_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) < 0) {
        perror("connect");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    printf("[CLIENT] Connected to %s:%d.\n", server_ip, port);
    printf(
        "[CLIENT] Commands: LOGIN|name, BID|amount, USERS, HISTORY, "
        "STATUS, PING, QUIT\n"
    );

    ReceiverArgs receiver_args = {
        .socket_fd = socket_fd
    };

    /* A thread receptora permite receber broadcasts enquanto o usuário digita. */
    pthread_t receiver_id;
    int thread_error = pthread_create(
        &receiver_id,
        NULL,
        receiver_thread,
        &receiver_args
    );

    if (thread_error != 0) {
        fprintf(
            stderr,
            "[CLIENT] pthread_create failed: %s\n",
            strerror(thread_error)
        );
        close(socket_fd);
        return EXIT_FAILURE;
    }

    char input[BUFFER_SIZE];

    for (;;) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n[CLIENT] Input closed.\n");
            shutdown(socket_fd, SHUT_WR);
            break;
        }

        size_t length = strlen(input);
        if (length == 0) {
            continue;
        }

        /* Se fgets() não capturou '\n', a linha ultrapassou o espaço disponível. */
        if (input[length - 1] != '\n') {
            if (length + 1 >= sizeof(input)) {
                fprintf(stderr, "[CLIENT] Command too long.\n");

                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF) {
                }

                continue;
            }

            input[length] = '\n';
            input[length + 1] = '\0';
        }

        int quitting = strcmp(input, "QUIT\n") == 0;

        if (send_all(socket_fd, input) < 0) {
            perror("[CLIENT] send");
            shutdown(socket_fd, SHUT_RDWR);
            break;
        }

        if (quitting) {
            break;
        }
    }

    thread_error = pthread_join(receiver_id, NULL);
    if (thread_error != 0) {
        fprintf(
            stderr,
            "[CLIENT] pthread_join failed: %s\n",
            strerror(thread_error)
        );
    }

    close(socket_fd);
    return EXIT_SUCCESS;
}
