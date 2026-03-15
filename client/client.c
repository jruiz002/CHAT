/*
 * client.c — Cliente de chat en C
 * Universidad del Valle de Guatemala · Sistemas Operativos 2025
 *
 * Compilar: gcc -o cliente client.c -lpthread
 * Ejecutar: ./cliente <username> <IP_servidor> <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../protocol.h"

/* ─── Estado global del cliente ─────────────────── */
static int  sockfd   = -1;
static char mi_usuario[32] = "";
static char mi_status[16]  = "ACTIVE";

/* ════════════════════════════════════════════════
 * Utilidades de envío
 * ════════════════════════════════════════════════ */

static void enviar_pkt(ChatPacket *pkt) {
    if (send(sockfd, pkt, sizeof(ChatPacket), 0) < 0)
        perror("send");
}

static void enviar_cmd(uint8_t command,
                       const char *target,
                       const char *payload) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.command = command;
    strncpy(pkt.sender, mi_usuario, 31);
    if (target  && target[0])  strncpy(pkt.target,  target,  31);
    if (payload && payload[0]) strncpy(pkt.payload, payload, 956);
    pkt.payload_len = payload ? (uint16_t)strlen(pkt.payload) : 0;
    enviar_pkt(&pkt);
}

/* ════════════════════════════════════════════════
 * Ayuda
 * ════════════════════════════════════════════════ */

static void mostrar_ayuda(void) {
    printf("\n"
           "┌─────────────────────────────────────────────────┐\n"
           "│              COMANDOS DISPONIBLES               │\n"
           "├──────────────────────────┬──────────────────────┤\n"
           "│ /broadcast <mensaje>     │ Mensaje a todos      │\n"
           "│ /msg <usuario> <mensaje> │ Mensaje privado      │\n"
           "│ /status <estado>         │ Cambiar estado       │\n"
           "│   estados: ACTIVE        │                      │\n"
           "│            BUSY          │                      │\n"
           "│            INACTIVE      │                      │\n"
           "│ /list                    │ Ver usuarios online  │\n"
           "│ /info <usuario>          │ Info de un usuario   │\n"
           "│ /help                    │ Mostrar esta ayuda   │\n"
           "│ /exit                    │ Salir del chat       │\n"
           "└──────────────────────────┴──────────────────────┘\n\n");
}

/* ════════════════════════════════════════════════
 * Thread de escucha — recibe mensajes del servidor
 * Corre en paralelo al hilo principal (UI)
 * ════════════════════════════════════════════════ */

static void *thread_receptor(void *arg) {
    (void)arg;
    ChatPacket pkt;

    while (1) {
        memset(&pkt, 0, sizeof(pkt));
        ssize_t n = recv(sockfd, &pkt, sizeof(pkt), MSG_WAITALL);

        if (n <= 0) {
            printf("\n[!] Conexión con el servidor perdida.\n");
            exit(EXIT_FAILURE);
        }

        switch (pkt.command) {

            case CMD_OK:
                printf("\n[OK] %s\n> ", pkt.payload);
                /* Si el servidor confirmó un cambio de status, actualizar local */
                if (strcmp(pkt.payload, STATUS_ACTIVO)   == 0 ||
                    strcmp(pkt.payload, STATUS_OCUPADO)  == 0 ||
                    strcmp(pkt.payload, STATUS_INACTIVO) == 0) {
                    strncpy(mi_status, pkt.payload, 15);
                }
                fflush(stdout);
                break;

            case CMD_ERROR:
                printf("\n[ERROR] %s\n> ", pkt.payload);
                fflush(stdout);
                break;

            case CMD_MSG:
                /* Si el servidor notifica cambio de status, actualizar local */
                if (strcmp(pkt.sender, "SERVER") == 0) {
                    if (strstr(pkt.payload, STATUS_INACTIVO))
                        strncpy(mi_status, STATUS_INACTIVO, 15);
                    else if (strstr(pkt.payload, STATUS_OCUPADO))
                        strncpy(mi_status, STATUS_OCUPADO,  15);
                    else if (strstr(pkt.payload, STATUS_ACTIVO))
                        strncpy(mi_status, STATUS_ACTIVO,   15);
                }

                /* Mostrar el mensaje según su origen */
                if (strcmp(pkt.target, "ALL") == 0)
                    printf("\n[General] %s: %s\n> ", pkt.sender, pkt.payload);
                else if (strcmp(pkt.sender, "SERVER") == 0)
                    printf("\n[Servidor] %s (mi status: %s)\n> ", pkt.payload, mi_status);
                else
                    printf("\n[Privado] %s → ti: %s\n> ", pkt.sender, pkt.payload);
                fflush(stdout);
                break;

            case CMD_USER_LIST: {
                printf("\n╔══════════════════════════════╗\n");
                printf("║     USUARIOS CONECTADOS      ║\n");
                printf("╠═══════════════════╦══════════╣\n");
                printf("║ %-17s ║ %-8s ║\n", "Usuario", "Status");
                printf("╠═══════════════════╬══════════╣\n");

                char buf[957];
                strncpy(buf, pkt.payload, 956);
                /* Usar strtok_r para parseo anidado (;  y  ,) sin
                 * que el contexto exterior se pierda con el interior */
                char *save_outer, *save_inner;
                char *entrada = strtok_r(buf, ";", &save_outer);
                while (entrada) {
                    char *nombre = strtok_r(entrada, ",", &save_inner);
                    char *estado = strtok_r(NULL,    ",", &save_inner);
                    if (nombre && estado)
                        printf("║ %-17s ║ %-8s ║\n", nombre, estado);
                    entrada = strtok_r(NULL, ";", &save_outer);
                }
                printf("╚═══════════════════╩══════════╝\n> ");
                fflush(stdout);
                break;
            }

            case CMD_USER_INFO: {
                /* payload = "IP,STATUS" */
                char buf[957];
                strncpy(buf, pkt.payload, 956);
                char *ip_str    = strtok(buf, ",");
                char *status_str = strtok(NULL, ",");
                printf("\n╔══════════════════════════════╗\n");
                printf("║     INFO DE USUARIO          ║\n");
                printf("╠══════════════════════════════╣\n");
                printf("║ IP     : %-20s║\n", ip_str     ? ip_str     : "?");
                printf("║ Status : %-20s║\n", status_str ? status_str : "?");
                printf("╚══════════════════════════════╝\n> ");
                fflush(stdout);
                break;
            }

            case CMD_DISCONNECTED:
                printf("\n[!] %s se desconectó\n> ", pkt.payload);
                fflush(stdout);
                break;

            default:
                printf("\n[?] Paquete desconocido (cmd=%d)\n> ", pkt.command);
                fflush(stdout);
                break;
        }
    }
    return NULL;
}

/* ════════════════════════════════════════════════
 * Procesar comando del usuario
 * ════════════════════════════════════════════════ */

/*
 * Retorna:
 *   0 → continuar
 *  -1 → salir (/exit)
 */
static int procesar_input(char *linea) {
    /* Quitar newline */
    linea[strcspn(linea, "\n")] = '\0';

    if (linea[0] == '\0') return 0;

    /* ── /broadcast <mensaje> ── */
    if (strncmp(linea, "/broadcast ", 11) == 0) {
        const char *msg = linea + 11;
        if (strlen(msg) == 0) {
            printf("[!] Uso: /broadcast <mensaje>\n");
            return 0;
        }
        enviar_cmd(CMD_BROADCAST, NULL, msg);
        return 0;
    }

    /* ── /msg <usuario> <mensaje> ── */
    if (strncmp(linea, "/msg ", 5) == 0) {
        char *resto    = linea + 5;
        char *espacio  = strchr(resto, ' ');
        if (!espacio) {
            printf("[!] Uso: /msg <usuario> <mensaje>\n");
            return 0;
        }
        *espacio = '\0';
        char *dest = resto;
        char *msg  = espacio + 1;
        if (strlen(msg) == 0) {
            printf("[!] El mensaje no puede estar vacío\n");
            return 0;
        }
        enviar_cmd(CMD_DIRECT, dest, msg);
        return 0;
    }

    /* ── /status <estado> ── */
    if (strncmp(linea, "/status ", 8) == 0) {
        const char *estado = linea + 8;
        if (strcmp(estado, STATUS_ACTIVO)   != 0 &&
            strcmp(estado, STATUS_OCUPADO)  != 0 &&
            strcmp(estado, STATUS_INACTIVO) != 0) {
            printf("[!] Status válidos: ACTIVE, BUSY, INACTIVE\n");
            return 0;
        }
        enviar_cmd(CMD_STATUS, NULL, estado);
        return 0;
    }

    /* ── /list ── */
    if (strcmp(linea, "/list") == 0) {
        enviar_cmd(CMD_LIST, NULL, NULL);
        return 0;
    }

    /* ── /info <usuario> ── */
    if (strncmp(linea, "/info ", 6) == 0) {
        const char *usuario = linea + 6;
        if (strlen(usuario) == 0) {
            printf("[!] Uso: /info <usuario>\n");
            return 0;
        }
        enviar_cmd(CMD_INFO, usuario, NULL);
        return 0;
    }

    /* ── /help ── */
    if (strcmp(linea, "/help") == 0) {
        mostrar_ayuda();
        return 0;
    }

    /* ── /exit ── */
    if (strcmp(linea, "/exit") == 0) {
        enviar_cmd(CMD_LOGOUT, NULL, NULL);
        return -1;
    }

    /* Comando desconocido */
    printf("[!] Comando no reconocido. Escribe /help para ver los comandos.\n");
    return 0;
}

/* ════════════════════════════════════════════════
 * Main
 * ════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <username> <IP_servidor> <puerto>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *username  = argv[1];
    const char *ip_srv    = argv[2];
    int         puerto    = atoi(argv[3]);

    if (strlen(username) == 0 || strlen(username) > 31) {
        fprintf(stderr, "El nombre de usuario debe tener entre 1 y 31 caracteres.\n");
        exit(EXIT_FAILURE);
    }
    strncpy(mi_usuario, username, 31);

    /* ── Crear socket y conectar ── */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons((uint16_t)puerto);

    if (inet_pton(AF_INET, ip_srv, &srv_addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválida: %s\n", ip_srv);
        exit(EXIT_FAILURE);
    }

    printf("Conectando a %s:%d...\n", ip_srv, puerto);
    if (connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("Conectado. Registrando usuario '%s'...\n\n", username);

    /* ── Enviar CMD_REGISTER y esperar respuesta ── */
    enviar_cmd(CMD_REGISTER, NULL, username);

    ChatPacket resp_reg;
    memset(&resp_reg, 0, sizeof(resp_reg));
    if (recv(sockfd, &resp_reg, sizeof(resp_reg), MSG_WAITALL) <= 0) {
        fprintf(stderr, "Error al recibir respuesta del servidor.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (resp_reg.command == CMD_ERROR) {
        fprintf(stderr, "[ERROR] %s\n", resp_reg.payload);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* ── Lanzar thread receptor ── */
    pthread_t tid_rx;
    pthread_create(&tid_rx, NULL, thread_receptor, NULL);
    pthread_detach(tid_rx);

    /* ── Mostrar bienvenida ── */
    printf("Bienvenido al chat, %s!\n", username);
    mostrar_ayuda();
    printf("> ");
    fflush(stdout);

    /* ── Loop principal: leer comandos del usuario ── */
    char linea[1024];
    while (fgets(linea, sizeof(linea), stdin)) {
        if (procesar_input(linea) == -1)
            break;
        printf("> ");
        fflush(stdout);
    }

    close(sockfd);
    printf("Hasta luego, %s!\n", username);
    return 0;
}
