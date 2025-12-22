# IR0 Network Protocol Implementation Guide

Este documento muestra cómo implementar protocolos de red usando el sistema de registro de IR0.

## Arquitectura Escalable

El sistema de registro de protocolos permite agregar nuevos protocolos sin modificar el código core:

```
Driver (RTL8139)
    ↓
net_receive() [Core]
    ↓
Protocol Registry Lookup
    ↓
Protocol Handler (ARP, IP, ICMP, TCP, UDP, etc.)
```

## Ejemplo 1: Implementar ARP (Layer 2.5)

```c
// net/arp.c
#include <ir0/net.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* ARP Header Structure */
struct arp_header {
    uint16_t hw_type;      /* Hardware type (1 = Ethernet) */
    uint16_t proto_type;   /* Protocol type (0x0800 = IPv4) */
    uint8_t hw_len;        /* Hardware address length (6 for MAC) */
    uint8_t proto_len;     /* Protocol address length (4 for IPv4) */
    uint16_t opcode;       /* Operation (1 = Request, 2 = Reply) */
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

/* ARP Cache Entry */
struct arp_cache_entry {
    ip4_addr_t ip;
    mac_addr_t mac;
    uint32_t timestamp;
    struct arp_cache_entry *next;
};

static struct arp_cache_entry *arp_cache = NULL;
static struct net_protocol arp_proto;

/* ARP Handler */
static void arp_receive_handler(struct net_device *dev, const void *data, 
                                 size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct arp_header))
        return;
    
    const struct arp_header *arp = (const struct arp_header *)data;
    
    serial_print("ARP: Received packet\n");
    serial_print("ARP: Operation: ");
    serial_print_hex32(ntohs(arp->opcode));
    serial_print("\n");
    
    /* Handle ARP Request/Reply */
    if (ntohs(arp->opcode) == 1) { /* ARP Request */
        /* TODO: Check if request is for us, send reply */
    } else if (ntohs(arp->opcode) == 2) { /* ARP Reply */
        /* TODO: Update ARP cache */
    }
}

/* Initialize ARP Protocol */
int arp_init(void)
{
    memset(&arp_proto, 0, sizeof(arp_proto));
    arp_proto.name = "ARP";
    arp_proto.ethertype = ETHERTYPE_ARP;
    arp_proto.handler = arp_receive_handler;
    arp_proto.priv = NULL;
    
    return net_register_protocol(&arp_proto);
}
```

## Ejemplo 2: Implementar IP (Layer 3)

```c
// net/ip.c
#include <ir0/net.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* IP Header Structure */
struct ip_header {
    uint8_t version_ihl;   /* Version (4) + IHL */
    uint8_t tos;           /* Type of Service */
    uint16_t total_len;    /* Total length */
    uint16_t id;           /* Identification */
    uint16_t flags_frag;   /* Flags + Fragment offset */
    uint8_t ttl;           /* Time to Live */
    uint8_t protocol;      /* Protocol (ICMP, TCP, UDP) */
    uint16_t checksum;     /* Header checksum */
    uint32_t src_ip;       /* Source IP */
    uint32_t dest_ip;      /* Destination IP */
} __attribute__((packed));

static struct net_protocol ip_proto;

/* IP Handler - Demultiplexes to upper layer protocols */
static void ip_receive_handler(struct net_device *dev, const void *data,
                                size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct ip_header))
        return;
    
    const struct ip_header *ip = (const struct ip_header *)data;
    uint8_t protocol = ip->protocol;
    
    /* Extract IP payload */
    const void *payload = (const uint8_t *)data + (ip->version_ihl & 0x0F) * 4;
    size_t payload_len = len - (ip->version_ihl & 0x0F) * 4;
    
    /* Look up upper layer protocol (ICMP, TCP, UDP) */
    struct net_protocol *upper_proto = net_find_protocol_by_ipproto(protocol);
    if (upper_proto && upper_proto->handler)
    {
        /* Call upper layer protocol handler */
        upper_proto->handler(dev, payload, payload_len, upper_proto->priv);
    }
    else
    {
        serial_print("IP: No handler for protocol ");
        serial_print_hex32(protocol);
        serial_print("\n");
    }
}

/* Initialize IP Protocol */
int ip_init(void)
{
    memset(&ip_proto, 0, sizeof(ip_proto));
    ip_proto.name = "IPv4";
    ip_proto.ethertype = ETHERTYPE_IP;
    ip_proto.handler = ip_receive_handler;
    ip_proto.priv = NULL;
    
    return net_register_protocol(&ip_proto);
}
```

## Ejemplo 3: Implementar ICMP (Layer 3, sobre IP)

```c
// net/icmp.c
#include <ir0/net.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* ICMP Header Structure */
struct icmp_header {
    uint8_t type;          /* ICMP type */
    uint8_t code;           /* ICMP code */
    uint16_t checksum;      /* Checksum */
    uint16_t identifier;    /* Identifier */
    uint16_t sequence;      /* Sequence number */
} __attribute__((packed));

#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_ECHO_REQUEST 8

static struct net_protocol icmp_proto;

/* ICMP Handler */
static void icmp_receive_handler(struct net_device *dev, const void *data,
                                  size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct icmp_header))
        return;
    
    const struct icmp_header *icmp = (const struct icmp_header *)data;
    
    serial_print("ICMP: Received packet type ");
    serial_print_hex32(icmp->type);
    serial_print("\n");
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST)
    {
        /* TODO: Send ICMP Echo Reply */
        serial_print("ICMP: Echo Request received\n");
    }
}

/* Initialize ICMP Protocol */
int icmp_init(void)
{
    memset(&icmp_proto, 0, sizeof(icmp_proto));
    icmp_proto.name = "ICMP";
    icmp_proto.ipproto = IPPROTO_ICMP;  /* Note: Uses IP protocol number */
    icmp_proto.handler = icmp_receive_handler;
    icmp_proto.priv = NULL;
    
    return net_register_protocol(&icmp_proto);
}
```

## Ejemplo 4: Implementar TCP (Layer 4, sobre IP)

```c
// net/tcp.c
#include <ir0/net.h>
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <string.h>

/* TCP Header Structure */
struct tcp_header {
    uint16_t src_port;     /* Source port */
    uint16_t dest_port;    /* Destination port */
    uint32_t seq_num;       /* Sequence number */
    uint32_t ack_num;       /* Acknowledgment number */
    uint8_t data_offset;    /* Data offset + reserved */
    uint8_t flags;         /* TCP flags */
    uint16_t window;       /* Window size */
    uint16_t checksum;     /* Checksum */
    uint16_t urgent_ptr;    /* Urgent pointer */
} __attribute__((packed));

static struct net_protocol tcp_proto;

/* TCP Handler */
static void tcp_receive_handler(struct net_device *dev, const void *data,
                                 size_t len, void *priv)
{
    (void)priv;
    
    if (len < sizeof(struct tcp_header))
        return;
    
    const struct tcp_header *tcp = (const struct tcp_header *)data;
    
    serial_print("TCP: Received packet from port ");
    serial_print_hex32(ntohs(tcp->src_port));
    serial_print(" to port ");
    serial_print_hex32(ntohs(tcp->dest_port));
    serial_print("\n");
    
    /* TODO: Handle TCP connection state, demultiplex to sockets */
}

/* Initialize TCP Protocol */
int tcp_init(void)
{
    memset(&tcp_proto, 0, sizeof(tcp_proto));
    tcp_proto.name = "TCP";
    tcp_proto.ipproto = IPPROTO_TCP;
    tcp_proto.handler = tcp_receive_handler;
    tcp_proto.priv = NULL;
    
    return net_register_protocol(&tcp_proto);
}
```

## Inicialización en el Kernel

En `kernel/main.c` o en un módulo de inicialización de red:

```c
void network_stack_init(void)
{
    /* Initialize protocols in order */
    arp_init();   /* Layer 2.5 - Must be before IP */
    ip_init();    /* Layer 3 - Must be before ICMP/TCP/UDP */
    icmp_init();  /* Layer 3+ */
    tcp_init();   /* Layer 4 */
    udp_init();   /* Layer 4 */
    
    log_subsystem_ok("NETWORK_STACK");
}
```

## Ventajas del Diseño

1. **Escalable**: Agregar nuevos protocolos es solo registrar un handler
2. **Modular**: Cada protocolo es independiente
3. **No invasivo**: No necesitas modificar código core para agregar protocolos
4. **Demultiplexado automático**: IP automáticamente demultiplexa a ICMP/TCP/UDP
5. **Consistente**: Mismo patrón para todos los protocolos



┌─────────────────────────────────────────┐
│         Driver (RTL8139)                │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      net_receive() [Core Layer]          │
│  - Busca protocolo por EtherType        │
└──────────────┬──────────────────────────┘
               │
               ▼
    ┌──────────┴──────────┐
    │                      │
    ▼                      ▼
┌─────────┐          ┌─────────┐
│   ARP   │          │   IP    │
│ Handler │          │ Handler │
└─────────┘          └────┬────┘
                           │
                           ▼
              ┌────────────┴────────────┐
              │                         │
              ▼                         ▼
         ┌─────────┐              ┌─────────┐
         │  ICMP   │              │   TCP   │
         │ Handler │              │ Handler │
         └─────────┘              └─────────┘


## Flujo de Datos

```
Ethernet Frame recibido
    ↓
net_receive() busca protocolo por EtherType
    ↓
Si es IP → ip_receive_handler()
    ↓
IP extrae protocol number y busca por IPPROTO
    ↓
Si es TCP → tcp_receive_handler()
    ↓
TCP demultiplexa por puerto a socket
```

## Extensibilidad Futura

Para agregar nuevos protocolos:

1. Define las estructuras de header
2. Implementa el handler
3. Registra el protocolo con `net_register_protocol()`
4. ¡Listo! El sistema lo manejará automáticamente

No necesitas modificar `net.c` ni `net.h` (a menos que agregues nuevas constantes).

