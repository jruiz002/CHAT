#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h>

/* ─────────────────────────────────────────────────
 * Comandos Cliente → Servidor
 * ───────────────────────────────────────────────── */
#define CMD_REGISTER    1   /* Registrar usuario                    */
#define CMD_BROADCAST   2   /* Mensaje a todos                      */
#define CMD_DIRECT      3   /* Mensaje privado                      */
#define CMD_LIST        4   /* Pedir lista de usuarios conectados   */
#define CMD_INFO        5   /* Pedir info de un usuario             */
#define CMD_STATUS      6   /* Cambiar status propio                */
#define CMD_LOGOUT      7   /* Desconectarse                        */

/* ─────────────────────────────────────────────────
 * Respuestas Servidor → Cliente
 * ───────────────────────────────────────────────── */
#define CMD_OK           8  /* Operación exitosa                    */
#define CMD_ERROR        9  /* Error en la operación                */
#define CMD_MSG         10  /* Mensaje entrante (broadcast/directo) */
#define CMD_USER_LIST   11  /* Lista de usuarios                    */
#define CMD_USER_INFO   12  /* Info de un usuario específico        */
#define CMD_DISCONNECTED 13 /* Notificación: alguien se desconectó  */

/* ─────────────────────────────────────────────────
 * Valores de status
 * ───────────────────────────────────────────────── */
#define STATUS_ACTIVO   "ACTIVO"
#define STATUS_OCUPADO  "OCUPADO"
#define STATUS_INACTIVO "INACTIVO"

/* ─────────────────────────────────────────────────
 * Timeout de inactividad (segundos)
 * ───────────────────────────────────────────────── */
#define INACTIVITY_TIMEOUT 60

/* ─────────────────────────────────────────────────
 * Paquete principal — SIEMPRE 1024 bytes
 *
 * Convención de campos por comando:
 *
 * CMD_REGISTER    : sender=username  target=""          payload=username
 * CMD_BROADCAST   : sender=username  target=""          payload=mensaje
 * CMD_DIRECT      : sender=username  target=destinatario payload=mensaje
 * CMD_LIST        : sender=username  target=""          payload=""
 * CMD_INFO        : sender=username  target=usuario     payload=""
 * CMD_STATUS      : sender=username  target=""          payload=ACTIVO|OCUPADO|INACTIVO
 * CMD_LOGOUT      : sender=username  target=""          payload=""
 *
 * CMD_OK          : sender="SERVER"  target=usuario     payload=descripción
 * CMD_ERROR       : sender="SERVER"  target=usuario     payload=descripción error
 * CMD_MSG         : sender=remitente target=ALL|usuario payload=mensaje
 * CMD_USER_LIST   : sender="SERVER"  target=usuario     payload="user1,STATUS1;user2,STATUS2;..."
 * CMD_USER_INFO   : sender="SERVER"  target=usuario     payload="IP,STATUS"
 * CMD_DISCONNECTED: sender="SERVER"  target="ALL"       payload=username que salió
 * ───────────────────────────────────────────────── */
typedef struct {
    uint8_t  command;       /*   1 byte  — operación a realizar      */
    uint16_t payload_len;   /*   2 bytes — bytes válidos en payload  */
    char     sender[32];    /*  32 bytes — nombre del remitente      */
    char     target[32];    /*  32 bytes — destinatario (vacío=N/A)  */
    char     payload[957];  /* 957 bytes — contenido del mensaje     */
} __attribute__((packed)) ChatPacket;
/* TOTAL: 1 + 2 + 32 + 32 + 957 = 1024 bytes exactos */

#endif /* PROTOCOLO_H */
