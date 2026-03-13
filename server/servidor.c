/*
 * servidor.c — Servidor de chat en C
 * Universidad del Valle de Guatemala · Sistemas Operativos 2025
 *
 * Compilar: gcc -o servidor servidor.c -lpthread
 * Ejecutar: ./servidor <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../protocol.h"

/* ─── Configuración ─────────────────────────────── */
#define MAX_CLIENTES 100

/* ─── Estructura de cliente conectado ───────────── */
typedef struct {
    int      sockfd;                  /* socket del cliente              */
    char     username[32];            /* nombre de usuario               */
    char     ip[INET_ADDRSTRLEN];     /* IP del cliente                  */
    char     status[16];              /* ACTIVO / OCUPADO / INACTIVO     */
    int      activo;                  /* 1 = conectado, 0 = libre        */
    time_t   ultimo_mensaje;          /* timestamp del último mensaje     */
} Cliente;

/* ─── Estado global del servidor ────────────────── */
static Cliente         lista[MAX_CLIENTES];
static int             num_clientes = 0;
static pthread_mutex_t mutex_lista  = PTHREAD_MUTEX_INITIALIZER;

/* Envía un paquete ya armado a un socket */
static void enviar_pkt(int sockfd, ChatPacket *pkt) {
    send(sockfd, pkt, sizeof(ChatPacket), 0);
}

/* Envía CMD_OK con mensaje de confirmación */
static void enviar_ok(int sockfd, const char *destinatario, const char *msg) {
    ChatPacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.command = CMD_OK;
    strncpy(resp.sender,  "SERVER",      31);
    strncpy(resp.target,  destinatario,  31);
    strncpy(resp.payload, msg,           956);
    resp.payload_len = (uint16_t)strlen(resp.payload);
    enviar_pkt(sockfd, &resp);
}

/* Envía CMD_ERROR con descripción del error */
static void enviar_error(int sockfd, const char *destinatario, const char *msg) {
    ChatPacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.command = CMD_ERROR;
    strncpy(resp.sender,  "SERVER",      31);
    strncpy(resp.target,  destinatario,  31);
    strncpy(resp.payload, msg,           956);
    resp.payload_len = (uint16_t)strlen(resp.payload);
    enviar_pkt(sockfd, &resp);
}

/* Busca un cliente por nombre (debe llamarse con mutex tomado) */
static int buscar_por_nombre(const char *username) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (lista[i].activo && strcmp(lista[i].username, username) == 0)
            return i;
    }
    return -1;
}

/* Busca un slot libre (debe llamarse con mutex tomado) */
static int buscar_slot_libre(void) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (!lista[i].activo) return i;
    }
    return -1;
}

/* Notifica a TODOS los clientes activos (excepto excluir_fd) */
static void broadcast_pkt(ChatPacket *pkt, int excluir_fd) {
    pthread_mutex_lock(&mutex_lista);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (lista[i].activo && lista[i].sockfd != excluir_fd)
            enviar_pkt(lista[i].sockfd, pkt);
    }
    pthread_mutex_unlock(&mutex_lista);
}

/* Actualiza el timestamp de último mensaje del cliente */
static void tocar_actividad(int sockfd) {
    pthread_mutex_lock(&mutex_lista);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (lista[i].activo && lista[i].sockfd == sockfd) {
            lista[i].ultimo_mensaje = time(NULL);
            /* Si estaba INACTIVO, vuelve a ACTIVO automáticamente */
            if (strcmp(lista[i].status, STATUS_INACTIVO) == 0)
                strncpy(lista[i].status, STATUS_ACTIVO, 15);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_lista);
}


static void handle_register(int sockfd, const char *ip, ChatPacket *pkt) {
    const char *username = pkt->payload;

    pthread_mutex_lock(&mutex_lista);

    /* ¿Nombre ya existe? */
    if (buscar_por_nombre(username) >= 0) {
        pthread_mutex_unlock(&mutex_lista);
        enviar_error(sockfd, username, "El nombre de usuario ya está en uso");
        return;
    }

    /* ¿Slot disponible? */
    int slot = buscar_slot_libre();
    if (slot < 0) {
        pthread_mutex_unlock(&mutex_lista);
        enviar_error(sockfd, username, "Servidor lleno, intenta más tarde");
        return;
    }

    /* Registrar */
    memset(&lista[slot], 0, sizeof(Cliente));
    lista[slot].sockfd = sockfd;
    strncpy(lista[slot].username, username, 31);
    strncpy(lista[slot].ip,       ip,       INET_ADDRSTRLEN - 1);
    strncpy(lista[slot].status,   STATUS_ACTIVO, 15);
    lista[slot].activo          = 1;
    lista[slot].ultimo_mensaje  = time(NULL);
    num_clientes++;

    pthread_mutex_unlock(&mutex_lista);

    printf("[+] Usuario registrado: %s (%s)\n", username, ip);
    enviar_ok(sockfd, username, "Registro exitoso");

    /* Notificar a todos que llegó alguien nuevo */
    ChatPacket notif;
    memset(&notif, 0, sizeof(notif));
    notif.command = CMD_MSG;
    strncpy(notif.sender,  "SERVER", 31);
    strncpy(notif.target,  "ALL",    31);
    snprintf(notif.payload, 957, "%s se ha conectado", username);
    notif.payload_len = (uint16_t)strlen(notif.payload);
    broadcast_pkt(&notif, sockfd);
}

static void handle_broadcast(int sockfd, ChatPacket *pkt) {
    tocar_actividad(sockfd);

    /* Armar el CMD_MSG para reenviar a todos */
    ChatPacket fwd;
    memset(&fwd, 0, sizeof(fwd));
    fwd.command = CMD_MSG;
    strncpy(fwd.sender,  pkt->sender,  31);
    strncpy(fwd.target,  "ALL",        31);
    strncpy(fwd.payload, pkt->payload, 956);
    fwd.payload_len = pkt->payload_len;

    /* Enviar a TODOS incluyendo al remitente (para que vea su propio msg) */
    pthread_mutex_lock(&mutex_lista);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (lista[i].activo)
            enviar_pkt(lista[i].sockfd, &fwd);
    }
    pthread_mutex_unlock(&mutex_lista);
}

static void handle_direct(int sockfd, ChatPacket *pkt) {
    tocar_actividad(sockfd);

    pthread_mutex_lock(&mutex_lista);
    int idx = buscar_por_nombre(pkt->target);

    if (idx < 0) {
        pthread_mutex_unlock(&mutex_lista);
        enviar_error(sockfd, pkt->sender, "El usuario no está conectado");
        return;
    }

    /* Armar y reenviar solo al destinatario */
    ChatPacket fwd;
    memset(&fwd, 0, sizeof(fwd));
    fwd.command = CMD_MSG;
    strncpy(fwd.sender,  pkt->sender,  31);
    strncpy(fwd.target,  pkt->target,  31);
    strncpy(fwd.payload, pkt->payload, 956);
    fwd.payload_len = pkt->payload_len;
    enviar_pkt(lista[idx].sockfd, &fwd);
    pthread_mutex_unlock(&mutex_lista);
}

static void handle_list(int sockfd, ChatPacket *pkt) {
    tocar_actividad(sockfd);

    /* Construir payload: "user1,ACTIVO;user2,OCUPADO;..." */
    char lista_str[957] = "";
    pthread_mutex_lock(&mutex_lista);
    int primero = 1;
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (lista[i].activo) {
            if (!primero) strncat(lista_str, ";", 956);
            strncat(lista_str, lista[i].username, 956);
            strncat(lista_str, ",",               956);
            strncat(lista_str, lista[i].status,   956);
            primero = 0;
        }
    }
    pthread_mutex_unlock(&mutex_lista);

    ChatPacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.command = CMD_USER_LIST;
    strncpy(resp.sender,  "SERVER",      31);
    strncpy(resp.target,  pkt->sender,   31);
    strncpy(resp.payload, lista_str,     956);
    resp.payload_len = (uint16_t)strlen(lista_str);
    enviar_pkt(sockfd, &resp);
}

static void handle_info(int sockfd, ChatPacket *pkt) {
    tocar_actividad(sockfd);

    pthread_mutex_lock(&mutex_lista);
    int idx = buscar_por_nombre(pkt->target);

    if (idx < 0) {
        pthread_mutex_unlock(&mutex_lista);
        enviar_error(sockfd, pkt->sender, "Usuario no encontrado");
        return;
    }

    char info[957];
    snprintf(info, sizeof(info), "%s,%s", lista[idx].ip, lista[idx].status);
    pthread_mutex_unlock(&mutex_lista);

    ChatPacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.command = CMD_USER_INFO;
    strncpy(resp.sender,  "SERVER",    31);
    strncpy(resp.target,  pkt->sender, 31);
    strncpy(resp.payload, info,        956);
    resp.payload_len = (uint16_t)strlen(info);
    enviar_pkt(sockfd, &resp);
}

static void handle_status(int sockfd, ChatPacket *pkt) {
    tocar_actividad(sockfd);

    /* Validar que el status sea uno de los permitidos */
    if (strcmp(pkt->payload, STATUS_ACTIVO)   != 0 &&
        strcmp(pkt->payload, STATUS_OCUPADO)  != 0 &&
        strcmp(pkt->payload, STATUS_INACTIVO) != 0) {
        enviar_error(sockfd, pkt->sender, "Status inválido. Usa: ACTIVO, OCUPADO o INACTIVO");
        return;
    }

    pthread_mutex_lock(&mutex_lista);
    int idx = buscar_por_nombre(pkt->sender);
    if (idx >= 0)
        strncpy(lista[idx].status, pkt->payload, 15);
    pthread_mutex_unlock(&mutex_lista);

    enviar_ok(sockfd, pkt->sender, pkt->payload);
    printf("[~] %s cambió status a %s\n", pkt->sender, pkt->payload);
}

static void handle_logout(int sockfd, const char *username) {
    printf("[-] Usuario desconectado: %s\n", username);

    /* Marcar como inactivo ANTES de notificar */
    pthread_mutex_lock(&mutex_lista);
    int idx = buscar_por_nombre(username);
    if (idx >= 0) {
        lista[idx].activo = 0;
        num_clientes--;
    }
    pthread_mutex_unlock(&mutex_lista);

    enviar_ok(sockfd, username, "Hasta luego");

    /* Notificar a todos que salió */
    ChatPacket notif;
    memset(&notif, 0, sizeof(notif));
    notif.command = CMD_DISCONNECTED;
    strncpy(notif.sender,  "SERVER", 31);
    strncpy(notif.target,  "ALL",    31);
    strncpy(notif.payload, username, 956);
    notif.payload_len = (uint16_t)strlen(username);
    broadcast_pkt(&notif, sockfd);
}

static void *thread_inactividad(void *arg) {
    (void)arg;
    while (1) {
        sleep(15); /* revisar cada 15 segundos */
        time_t ahora = time(NULL);

        pthread_mutex_lock(&mutex_lista);
        for (int i = 0; i < MAX_CLIENTES; i++) {
            if (!lista[i].activo) continue;
            if (strcmp(lista[i].status, STATUS_INACTIVO) == 0) continue;

            double segundos = difftime(ahora, lista[i].ultimo_mensaje);
            if (segundos >= INACTIVITY_TIMEOUT) {
                strncpy(lista[i].status, STATUS_INACTIVO, 15);
                printf("[~] %s marcado como INACTIVO por timeout\n", lista[i].username);

                /* Avisar al cliente que su status cambió */
                ChatPacket notif;
                memset(&notif, 0, sizeof(notif));
                notif.command = CMD_MSG;
                strncpy(notif.sender,  "SERVER",             31);
                strncpy(notif.target,  lista[i].username,    31);
                strncpy(notif.payload, "Tu status cambió a INACTIVO por inactividad", 956);
                notif.payload_len = (uint16_t)strlen(notif.payload);
                enviar_pkt(lista[i].sockfd, &notif);
            }
        }
        pthread_mutex_unlock(&mutex_lista);
    }
    return NULL;
}

typedef struct {
    int  sockfd;
    char ip[INET_ADDRSTRLEN];
} ThreadArgs;

static void *handle_client(void *arg) {
    ThreadArgs *args    = (ThreadArgs *)arg;
    int         sockfd  = args->sockfd;
    char        ip[INET_ADDRSTRLEN];
    strncpy(ip, args->ip, INET_ADDRSTRLEN - 1);
    free(arg);

    char      username[32] = "";
    ChatPacket pkt;
    int        registrado  = 0;

    while (1) {
        memset(&pkt, 0, sizeof(pkt));
        ssize_t n = recv(sockfd, &pkt, sizeof(pkt), MSG_WAITALL);

        /* Cliente desconectado abruptamente */
        if (n <= 0) {
            if (registrado && username[0] != '\0')
                handle_logout(sockfd, username);
            break;
        }

        /* Si no está registrado, solo acepta CMD_REGISTER */
        if (!registrado && pkt.command != CMD_REGISTER) {
            enviar_error(sockfd, "", "Debes registrarte primero");
            continue;
        }

        switch (pkt.command) {
            case CMD_REGISTER:
                handle_register(sockfd, ip, &pkt);
                /* Guardar username si el registro fue exitoso */
                pthread_mutex_lock(&mutex_lista);
                if (buscar_por_nombre(pkt.payload) >= 0) {
                    strncpy(username, pkt.payload, 31);
                    registrado = 1;
                }
                pthread_mutex_unlock(&mutex_lista);
                break;

            case CMD_BROADCAST:
                handle_broadcast(sockfd, &pkt);
                break;

            case CMD_DIRECT:
                handle_direct(sockfd, &pkt);
                break;

            case CMD_LIST:
                handle_list(sockfd, &pkt);
                break;

            case CMD_INFO:
                handle_info(sockfd, &pkt);
                break;

            case CMD_STATUS:
                handle_status(sockfd, &pkt);
                break;

            case CMD_LOGOUT:
                handle_logout(sockfd, username);
                goto fin_thread;

            default:
                enviar_error(sockfd, username, "Comando desconocido");
                break;
        }
    }

fin_thread:
    close(sockfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int puerto = atoi(argv[1]);
    if (puerto <= 0 || puerto > 65535) {
        fprintf(stderr, "Puerto inválido: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    /* Inicializar lista */
    memset(lista, 0, sizeof(lista));

    /* Crear socket del servidor */
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* Reusar dirección para no esperar TIME_WAIT al reiniciar */
    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)puerto);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv_fd); exit(EXIT_FAILURE);
    }

    if (listen(srv_fd, 10) < 0) {
        perror("listen"); close(srv_fd); exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en puerto %d...\n", puerto);

    /* Lanzar thread de inactividad */
    pthread_t tid_inact;
    pthread_create(&tid_inact, NULL, thread_inactividad, NULL);
    pthread_detach(tid_inact);

    /* Bucle principal: aceptar conexiones */
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = accept(srv_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) { perror("accept"); continue; }

        /* Preparar args para el thread */
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd = cli_fd;
        inet_ntop(AF_INET, &cli_addr.sin_addr, args->ip, INET_ADDRSTRLEN);

        printf("[?] Nueva conexión desde %s\n", args->ip);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, args);
        pthread_detach(tid);   /* el thread se limpia solo al terminar */
    }

    close(srv_fd);
    return 0;
}
