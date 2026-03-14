# Protocolo de Comunicación — Chat en C
**Universidad del Valle de Guatemala · Sistemas Operativos 2025**
> **Versión:** 2.0 · **Transporte:** TCP · **Formato:** Binario (struct fijo de 1024 bytes)

---

## 1. Ejecución

```bash
# Servidor
./servidor <puerto>

# Cliente
./cliente <username> <IP_servidor> <puerto>
```

---

## 2. Struct — `protocolo.h`

> Copiar este archivo **exactamente igual** en todos los grupos. No modificar orden ni tamaños.

```c
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h>

/* ── Comandos Cliente → Servidor ── */
#define CMD_REGISTER    1
#define CMD_BROADCAST   2
#define CMD_DIRECT      3
#define CMD_LIST        4
#define CMD_INFO        5
#define CMD_STATUS      6
#define CMD_LOGOUT      7

/* ── Respuestas Servidor → Cliente ── */
#define CMD_OK          8
#define CMD_ERROR       9
#define CMD_MSG         10
#define CMD_USER_LIST   11
#define CMD_USER_INFO   12
#define CMD_DISCONNECTED 13

/* ── Valores de status ── */
#define STATUS_ACTIVO   "ACTIVE"
#define STATUS_OCUPADO  "BUSY"
#define STATUS_INACTIVO "INACTIVE"

/* ── Timeouts ── */
#define INACTIVITY_TIMEOUT 60   /* segundos sin actividad → INACTIVE */

/* ── Struct principal: siempre 1024 bytes ── */
typedef struct {
    uint8_t  command;       /*   1 byte  — operación a realizar       */
    uint16_t payload_len;   /*   2 bytes — bytes válidos en payload   */
    char     sender[32];    /*  32 bytes — nombre del remitente       */
    char     target[32];    /*  32 bytes — destinatario (vacío=todos) */
    char     payload[957];  /* 957 bytes — contenido del mensaje      */
} __attribute__((packed)) ChatPacket;
/* TOTAL: 1 + 2 + 32 + 32 + 957 = 1024 bytes */

#endif
```

### ¿Por qué 1024 bytes?
- **Potencia de 2** → alineación eficiente en memoria
- **Cabe en un segmento TCP** (MTU Ethernet ≈ 1500 B; con cabeceras TCP/IP quedan ≈ 1460 B útiles)
- **`recv()` siempre lee lo mismo** → sin lógica extra para detectar fin de mensaje

---

## 3. Convención de campos por comando

| Comando | `sender` | `target` | `payload` |
|---|---|---|---|
| `CMD_REGISTER` | username | *(vacío)* | username |
| `CMD_BROADCAST` | username | *(vacío)* | mensaje |
| `CMD_DIRECT` | username | **destinatario** | mensaje |
| `CMD_LIST` | username | *(vacío)* | *(vacío)* |
| `CMD_INFO` | username | **usuario a consultar** | *(vacío)* |
| `CMD_STATUS` | username | *(vacío)* | `ACTIVE` / `BUSY` / `INACTIVE` |
| `CMD_LOGOUT` | username | *(vacío)* | *(vacío)* |
| `CMD_OK` | `"SERVER"` | destinatario | mensaje de confirmación |
| `CMD_ERROR` | `"SERVER"` | destinatario | descripción del error |
| `CMD_MSG` | remitente original | destinatario o `"ALL"` | mensaje |
| `CMD_USER_LIST` | `"SERVER"` | solicitante | `"alice,ACTIVE;bob,BUSY;..."` |
| `CMD_USER_INFO` | `"SERVER"` | solicitante | `"IP,STATUS"` |
| `CMD_DISCONNECTED` | `"SERVER"` | `"ALL"` | username que salió |

> **Campo vacío:** al hacer `memset(&pkt, 0, sizeof(pkt))` antes de llenar el struct, todos los campos no usados quedan en `\0` automáticamente.

---

## 4. Enviar y recibir (patrón base)

```c
// Enviar
ChatPacket pkt;
memset(&pkt, 0, sizeof(pkt));
pkt.command = CMD_BROADCAST;
strncpy(pkt.sender,  "alice", 31);
strncpy(pkt.payload, "Hola a todos!", 956);
pkt.payload_len = strlen(pkt.payload);
send(fd, &pkt, sizeof(pkt), 0);

// Recibir
ChatPacket pkt;
recv(fd, &pkt, sizeof(pkt), MSG_WAITALL);
// pkt.command → decide qué hacer
// pkt.sender  → quién lo mandó
// pkt.target  → a quién va dirigido
// pkt.payload → el contenido
```

---

## 5. Flujos de sesión

### 5.1 Registro
```
Cliente                    Servidor
  │── CMD_REGISTER ────────►│  sender="alice", payload="alice"
  │◄─ CMD_OK ──────────────│  payload="Bienvenido alice"
  │                         │  [crea thread, status=ACTIVE]
  │
  │  (nombre ya existe)
  │◄─ CMD_ERROR ───────────│  payload="Usuario ya existe"
  │   [cliente cierra]      │
```

### 5.2 Broadcast
```
Cliente A                  Servidor                  Clientes B, C...
  │── CMD_BROADCAST ───────►│  payload="Hola!"
  │                         │── CMD_MSG ────────────►│  sender="alice", target="ALL"
  │◄────────────────────────│── CMD_MSG ─────────────│  (A también recibe su propio msg)
```

### 5.3 Mensaje directo
```
Cliente A (alice)          Servidor                  Cliente B (bob)
  │── CMD_DIRECT ──────────►│  sender="alice", target="bob", payload="Hola Bob"
  │                         │── CMD_MSG ────────────►│  sender="alice", target="bob"
  │
  │  (bob no conectado)
  │◄─ CMD_ERROR ───────────│  payload="Destinatario no conectado"
```

### 5.4 Listado de usuarios
```
Cliente                    Servidor
  │── CMD_LIST ────────────►│
  │◄─ CMD_USER_LIST ────────│  payload="alice,ACTIVE;bob,BUSY;carlos,INACTIVE"
```

### 5.5 Info de usuario
```
Cliente                    Servidor
  │── CMD_INFO ────────────►│  target="bob"
  │◄─ CMD_USER_INFO ────────│  payload="192.168.1.10,BUSY"
  │
  │  (no existe)
  │◄─ CMD_ERROR ───────────│  payload="Usuario no conectado"
```

### 5.6 Cambio de status
```
Cliente                    Servidor
  │── CMD_STATUS ───────────►│  payload="BUSY"
  │◄─ CMD_OK ───────────────│  payload="BUSY"
  │   [cliente actualiza UI] │

  [sin actividad por INACTIVITY_TIMEOUT segundos]
  │◄─ CMD_MSG ──────────────│  sender="SERVER", payload="Tu status cambió a INACTIVE"
  │   [cliente actualiza UI] │
```

### 5.7 Desconexión controlada
```
Cliente                    Servidor                  Otros clientes
  │── CMD_LOGOUT ───────────►│
  │◄─ CMD_OK ───────────────│
  │   [cierra socket]        │── CMD_DISCONNECTED ──►│  payload="alice"
                             │   [termina thread]
```

### 5.8 Desconexión abrupta (caída del cliente)
```
Cliente                    Servidor
  [crash / red caída]
                            recv() devuelve 0 o -1
                            [remueve al cliente de la lista]
                            [notifica CMD_DISCONNECTED a todos]
                            [termina thread]
```

---

## 6. Estructura del servidor en memoria

```c
typedef struct {
    char     username[32];
    char     ip[INET_ADDRSTRLEN];
    char     status[16];
    int      sockfd;
    int      activo;
    time_t   ultimo_mensaje;   // para detectar inactividad
} Cliente;

Cliente         lista[100];
int             num_clientes = 0;
pthread_mutex_t mutex_lista  = PTHREAD_MUTEX_INITIALIZER;
```

> No se requiere base de datos. Los usuarios conectados son datos volátiles que viven solo mientras el servidor está activo.

---

## 7. Comandos de la UI del cliente

| Comando del usuario | Acción |
|---|---|
| `/broadcast <mensaje>` | Envía `CMD_BROADCAST` |
| `/msg <usuario> <mensaje>` | Envía `CMD_DIRECT` |
| `/status <ACTIVE\|BUSY\|INACTIVE>` | Envía `CMD_STATUS` |
| `/list` | Envía `CMD_LIST` |
| `/info <usuario>` | Envía `CMD_INFO` |
| `/help` | Muestra ayuda local |
| `/exit` | Envía `CMD_LOGOUT` y cierra |

---

## 8. Configuración de red (EC2)

El Security Group de la instancia EC2 debe tener habilitada la regla:

```
Tipo: Custom TCP
Puerto: <puertodelservidor>
Origen: 0.0.0.0/0
```

Sin esta regla, los paquetes no llegan aunque el código sea correcto.
