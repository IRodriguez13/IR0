// kernel/syscalls/syscalls.h - Sistema de system calls
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Tipos básicos que faltan
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef int64_t off_t;
typedef uint32_t sigset_t;

// Códigos de error
#define EPERM           1   // Operation not permitted
#define ENOENT          2   // No such file or directory
#define ESRCH           3   // No such process
#define EINTR           4   // Interrupted system call
#define EIO             5   // Input/output error
#define ENXIO           6   // No such device or address
#define E2BIG           7   // Argument list too long
#define ENOEXEC         8   // Exec format error
#define EBADF           9   // Bad file descriptor
#define ECHILD          10  // No child processes
#define EAGAIN          11  // Resource temporarily unavailable
#define ENOMEM          12  // Cannot allocate memory
#define EACCES          13  // Permission denied
#define EFAULT          14  // Bad address
#define ENOTBLK         15  // Block device required
#define EBUSY           16  // Device or resource busy
#define EEXIST          17  // File exists
#define EXDEV           18  // Invalid cross-device link
#define ENODEV          19  // No such device
#define ENOTDIR         20  // Not a directory
#define EISDIR          21  // Is a directory
#define EINVAL          22  // Invalid argument
#define ENFILE          23  // Too many open files in system
#define EMFILE          24  // Too many open files
#define ENOTTY          25  // Inappropriate I/O control operation
#define ETXTBSY         26  // Text file busy
#define EFBIG           27  // File too large
#define ENOSPC          28  // No space left on device
#define ESPIPE          29  // Invalid seek
#define EROFS           30  // Read-only file system
#define EMLINK          31  // Too many links
#define EPIPE           32  // Broken pipe
#define EDOM            33  // Numerical argument out of domain
#define ERANGE          34  // Numerical result out of range
#define EDEADLK         35  // Resource deadlock avoided
#define ENAMETOOLONG    36  // File name too long
#define ENOLCK          37  // No locks available
#define ENOSYS          38  // Function not implemented
#define ENOTEMPTY       39  // Directory not empty
#define ELOOP           40  // Too many levels of symbolic links
#define ENOMSG          41  // No message of desired type
#define EIDRM           42  // Identifier removed
#define ECHRNG          43  // Channel number out of range
#define EL2NSYNC        44  // Level 2 not synchronized
#define EL3HLT          45  // Level 3 halted
#define EL3RST          46  // Level 3 reset
#define ELNRNG          47  // Link number out of range
#define EUNATCH         48  // Protocol driver not attached
#define ENOCSI          49  // No CSI structure available
#define EL2HLT          50  // Level 2 halted
#define EBADE           51  // Invalid exchange
#define EBADR           52  // Invalid request descriptor
#define EXFULL          53  // Exchange full
#define ENOANO          54  // No anode
#define EBADRQC         55  // Invalid request code
#define EBADSLT         56  // Invalid slot
#define EDEADLOCK       EDEADLK
#define EBFONT          59  // Bad font file format
#define ENOSTR          60  // Device not a stream
#define ENODATA         61  // No data available
#define ETIME           62  // Timer expired
#define ENOSR           63  // Out of streams resources
#define ENONET          64  // Machine is not on the network
#define ENOPKG          65  // Package not installed
#define EREMOTE         66  // Object is remote
#define ENOLINK         67  // Link has been severed
#define EADV            68  // Advertise error
#define ESRMNT          69  // Srmount error
#define ECOMM           70  // Communication error on send
#define EPROTO          71  // Protocol error
#define EMULTIHOP       72  // Multihop attempted
#define EDOTDOT         73  // RFS specific error
#define EBADMSG         74  // Bad message
#define EOVERFLOW       75  // Value too large for defined data type
#define ENOTUNIQ        76  // Name not unique on network
#define EBADFD          77  // File descriptor in bad state
#define EREMCHG         78  // Remote address changed
#define ELIBACC         79  // Can not access a needed shared library
#define ELIBBAD         80  // Accessing a corrupted shared library
#define ELIBSCN         81  // .lib section in a.out corrupted
#define ELIBMAX         82  // Attempting to link in too many shared libraries
#define ELIBEXEC        83  // Cannot exec a shared library directly
#define EILSEQ          84  // Invalid or incomplete multibyte or wide character
#define ERESTART        85  // Interrupted system call should be restarted
#define ESTRPIPE        86  // Streams pipe error
#define EUSERS          87  // Too many users
#define ENOTSOCK        88  // Socket operation on non-socket
#define EDESTADDRREQ    89  // Destination address required
#define EMSGSIZE        90  // Message too long
#define EPROTOTYPE      91  // Protocol wrong type for socket
#define ENOPROTOOPT     92  // Protocol not available
#define EPROTONOSUPPORT 93  // Protocol not supported
#define ESOCKTNOSUPPORT 94  // Socket type not supported
#define EOPNOTSUPP      95  // Operation not supported
#define EPFNOSUPPORT    96  // Protocol family not supported
#define EAFNOSUPPORT    97  // Address family not supported by protocol
#define EADDRINUSE      98  // Address already in use
#define EADDRNOTAVAIL   99  // Cannot assign requested address
#define ENETDOWN        100 // Network is down
#define ENETUNREACH     101 // Network is unreachable
#define ENETRESET       102 // Network dropped connection on reset
#define ECONNABORTED    103 // Software caused connection abort
#define ECONNRESET      104 // Connection reset by peer
#define ENOBUFS         105 // No buffer space available
#define EISCONN         106 // Transport endpoint is already connected
#define ENOTCONN        107 // Transport endpoint is not connected
#define ESHUTDOWN       108 // Cannot send after transport endpoint shutdown
#define ETOOMANYREFS    109 // Too many references: cannot splice
#define ETIMEDOUT       110 // Connection timed out
#define ECONNREFUSED    111 // Connection refused
#define EHOSTDOWN       112 // Host is down
#define EHOSTUNREACH    113 // No route to host
#define EALREADY        114 // Operation already in progress
#define EINPROGRESS     115 // Operation now in progress
#define ESTALE          116 // Stale file handle
#define EUCLEAN         117 // Structure needs cleaning
#define ENOTNAM         118 // Not a XENIX named type file
#define ENAVAIL         119 // No XENIX semaphores available
#define EISNAM          120 // Is a named type file
#define EREMOTEIO       121 // Remote I/O error
#define EDQUOT          122 // Disk quota exceeded
#define ENOMEDIUM       123 // No medium found
#define EMEDIUMTYPE     124 // Wrong medium type
#define ECANCELED       125 // Operation canceled
#define ENOKEY          126 // Required key not available
#define EKEYEXPIRED     127 // Key has expired
#define EKEYREVOKED     128 // Key has been revoked
#define EKEYREJECTED    129 // Key was rejected by service
#define EOWNERDEAD      130 // Owner died
#define ENOTRECOVERABLE 131 // State not recoverable
#define ERFKILL         132 // Operation not possible due to RF-kill
#define EHWPOISON       133 // Memory page has hardware error

// Estructuras para señales
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};

// Estructuras para recursos
struct timeval {
    uint64_t tv_sec;
    uint64_t tv_usec;
};

struct rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    uint64_t ru_maxrss;
    uint64_t ru_ixrss;
    uint64_t ru_idrss;
    uint64_t ru_isrss;
    uint64_t ru_minflt;
    uint64_t ru_majflt;
    uint64_t ru_nswap;
    uint64_t ru_inblock;
    uint64_t ru_oublock;
    uint64_t ru_msgsnd;
    uint64_t ru_msgrcv;
    uint64_t ru_nsignals;
    uint64_t ru_nvcsw;
    uint64_t ru_nivcsw;
};

struct tms {
    uint64_t tms_utime;
    uint64_t tms_stime;
    uint64_t tms_cutime;
    uint64_t tms_cstime;
};

// ===============================================================================
// CONSTANTES Y DEFINICIONES
// ===============================================================================

#define MAX_SYSCALLS 256
#define SYSCALL_INVALID -1

// Números de system calls
typedef enum {
    SYS_EXIT = 0,           // Terminar proceso
    SYS_FORK = 1,           // Crear proceso hijo
    SYS_READ = 2,           // Leer de descriptor
    SYS_WRITE = 3,          // Escribir a descriptor
    SYS_OPEN = 4,           // Abrir archivo
    SYS_CLOSE = 5,          // Cerrar descriptor
    SYS_EXEC = 6,           // Ejecutar programa
    SYS_WAIT = 7,           // Esperar proceso hijo
    SYS_KILL = 8,           // Enviar señal
    SYS_GETPID = 9,         // Obtener PID actual
    SYS_GETPPID = 10,       // Obtener PID del padre
    SYS_SLEEP = 11,         // Dormir por milisegundos
    SYS_YIELD = 12,         // Ceder CPU
    SYS_BRK = 13,           // Cambiar tamaño del heap
    SYS_MMAP = 14,          // Mapear memoria
    SYS_MUNMAP = 15,        // Desmapear memoria
    SYS_GETTIME = 16,       // Obtener tiempo actual
    SYS_GETUID = 17,        // Obtener UID
    SYS_SETUID = 18,        // Establecer UID
    SYS_CHDIR = 19,         // Cambiar directorio
    SYS_GETCWD = 20,        // Obtener directorio actual
    SYS_MKDIR = 21,         // Crear directorio
    SYS_RMDIR = 22,         // Eliminar directorio
    SYS_LINK = 23,          // Crear enlace duro
    SYS_UNLINK = 24,        // Eliminar enlace
    SYS_STAT = 25,          // Obtener información de archivo
    SYS_FSTAT = 26,         // Obtener información de descriptor
    SYS_LSEEK = 27,         // Reposicionar en archivo
    SYS_DUP = 28,           // Duplicar descriptor
    SYS_DUP2 = 29,          // Duplicar descriptor a número específico
    SYS_PIPE = 30,          // Crear pipe
    SYS_ALARM = 31,         // Establecer alarma
    SYS_SIGNAL = 32,        // Establecer manejador de señal
    SYS_SIGACTION = 33,     // Establecer acción de señal
    SYS_SIGPROCMASK = 34,   // Cambiar máscara de señales
    SYS_SIGSUSPEND = 35,    // Suspender hasta señal
    SYS_SOCKET = 36,        // Crear socket
    SYS_BIND = 37,          // Vincular socket
    SYS_CONNECT = 38,       // Conectar socket
    SYS_LISTEN = 39,        // Escuchar en socket
    SYS_ACCEPT = 40,        // Aceptar conexión
    SYS_SEND = 41,          // Enviar datos
    SYS_RECV = 42,          // Recibir datos
    SYS_SHUTDOWN = 43,      // Cerrar socket
    SYS_GETSOCKOPT = 44,    // Obtener opción de socket
    SYS_SETSOCKOPT = 45,    // Establecer opción de socket
    SYS_GETPEERNAME = 46,   // Obtener nombre del peer
    SYS_GETSOCKNAME = 47,   // Obtener nombre del socket
    SYS_SELECT = 48,        // Multiplexación I/O
    SYS_POLL = 49,          // Poll de descriptores
    SYS_EPOLL_CREATE = 50,  // Crear epoll
    SYS_EPOLL_CTL = 51,     // Controlar epoll
    SYS_EPOLL_WAIT = 52,    // Esperar eventos epoll
    SYS_CLONE = 53,         // Clonar proceso/thread
    SYS_SET_THREAD_AREA = 54, // Establecer área de thread
    SYS_GET_THREAD_AREA = 55, // Obtener área de thread
    SYS_TGKILL = 56,        // Enviar señal a thread
    SYS_IO_SETUP = 57,      // Configurar AIO
    SYS_IO_DESTROY = 58,    // Destruir AIO
    SYS_IO_SUBMIT = 59,     // Enviar AIO
    SYS_IO_CANCEL = 60,     // Cancelar AIO
    SYS_IO_GETEVENTS = 61,  // Obtener eventos AIO
    SYS_MQ_OPEN = 62,       // Abrir cola de mensajes
    SYS_MQ_UNLINK = 63,     // Eliminar cola de mensajes
    SYS_MQ_TIMEDSEND = 64,  // Enviar mensaje con timeout
    SYS_MQ_TIMEDRECEIVE = 65, // Recibir mensaje con timeout
    SYS_MQ_NOTIFY = 66,     // Notificar cola de mensajes
    SYS_MQ_GETSETATTR = 67, // Obtener/establecer atributos
    SYS_GETDENTS = 68,      // Obtener entradas de directorio
    SYS_FCNTL = 69,         // Control de archivo
    SYS_FLOCK = 70,         // Bloquear archivo
    SYS_FSYNC = 71,         // Sincronizar archivo
    SYS_FDATASYNC = 72,     // Sincronizar datos
    SYS_TRUNCATE = 73,      // Truncar archivo
    SYS_FTRUNCATE = 74,     // Truncar descriptor
    SYS_GETRLIMIT = 75,     // Obtener límite de recursos
    SYS_SETRLIMIT = 76,     // Establecer límite de recursos
    SYS_GETRUSAGE = 77,     // Obtener uso de recursos
    SYS_TIMES = 78,         // Obtener tiempos de proceso
    SYS_PTRACE = 79,        // Trazado de proceso
    SYS_GETUID32 = 80,      // Obtener UID (32-bit)
    SYS_GETGID32 = 81,      // Obtener GID (32-bit)
    SYS_GETEUID32 = 82,     // Obtener EUID (32-bit)
    SYS_GETEGID32 = 83,     // Obtener EGID (32-bit)
    SYS_SETUID32 = 84,      // Establecer UID (32-bit)
    SYS_SETGID32 = 85,      // Establecer GID (32-bit)
    SYS_SETEUID32 = 86,     // Establecer EUID (32-bit)
    SYS_SETEGID32 = 87,     // Establecer EGID (32-bit)
    SYS_GETGROUPS32 = 88,   // Obtener grupos (32-bit)
    SYS_SETGROUPS32 = 89,   // Establecer grupos (32-bit)
    SYS_FCHOWN32 = 90,      // Cambiar propietario (32-bit)
    SYS_SETRESUID32 = 91,   // Establecer UIDs real/efectivo/guardado
    SYS_GETRESUID32 = 92,   // Obtener UIDs real/efectivo/guardado
    SYS_SETRESGID32 = 93,   // Establecer GIDs real/efectivo/guardado
    SYS_GETRESGID32 = 94,   // Obtener GIDs real/efectivo/guardado
    SYS_CHOWN32 = 95,       // Cambiar propietario (32-bit)
    SYS_SETREUID32 = 96,    // Establecer UIDs real/efectivo
    SYS_SETREGID32 = 97,    // Establecer GIDs real/efectivo
    SYS_RENAME = 98,        // Renombrar archivo
    SYS_TRUNCATE64 = 99,    // Truncar archivo (64-bit)
    SYS_FTRUNCATE64 = 100,  // Truncar descriptor (64-bit)
    SYS_STAT64 = 101,       // Obtener información de archivo (64-bit)
    SYS_LSTAT64 = 102,      // Obtener información de enlace (64-bit)
    SYS_FSTAT64 = 103,      // Obtener información de descriptor (64-bit)
    SYS_LSEEK64 = 104,      // Reposicionar en archivo (64-bit)
    SYS_MMAP2 = 105,        // Mapear memoria (versión 2)
    SYS_FADVISE64 = 106,    // Consejo de acceso a archivo
    SYS_NEWFSTATAT = 107,   // Obtener información de archivo relativo
    SYS_READLINKAT = 108,   // Leer enlace simbólico relativo
    SYS_FCHMODAT = 109,     // Cambiar modo de archivo relativo
    SYS_FACCESSAT = 110,    // Verificar acceso a archivo relativo
    SYS_PSELECT6 = 111,     // Select con timeout (versión 6)
    SYS_PPOLL = 112,        // Poll con timeout
    SYS_UNSHARE = 113,      // Descompartir namespace
    SYS_SET_ROBUST_LIST = 114, // Establecer lista robusta de futex
    SYS_GET_ROBUST_LIST = 115, // Obtener lista robusta de futex
    SYS_SPLICE = 116,       // Empalmar datos entre descriptores
    SYS_TEE = 117,          // Duplicar datos entre descriptores
    SYS_SYNC_FILE_RANGE = 118, // Sincronizar rango de archivo
    SYS_VMSPLICE = 119,     // Empalmar datos desde memoria
    SYS_MOVE_PAGES = 120,   // Mover páginas entre nodos NUMA
    SYS_UTIMENSAT = 121,    // Cambiar timestamps de archivo relativo
    SYS_EPOLL_PWAIT = 122,  // Esperar eventos epoll con timeout
    SYS_SIGNALFD = 123,     // Crear descriptor de señal
    SYS_TIMERFD_CREATE = 124, // Crear descriptor de timer
    SYS_EVENTFD = 125,      // Crear descriptor de evento
    SYS_FALLOCATE = 126,    // Pre-asignar espacio en archivo
    SYS_TIMERFD_SETTIME = 127, // Establecer tiempo de timer
    SYS_TIMERFD_GETTIME = 128, // Obtener tiempo de timer
    SYS_ACCEPT4 = 129,      // Aceptar conexión con flags
    SYS_SIGNALFD4 = 130,    // Crear descriptor de señal con flags
    SYS_EVENTFD2 = 131,     // Crear descriptor de evento con flags
    SYS_EPOLL_CREATE1 = 132, // Crear epoll con flags
    SYS_DUP3 = 133,         // Duplicar descriptor con flags
    SYS_PIPE2 = 134,        // Crear pipe con flags
    SYS_INOTIFY_INIT1 = 135, // Inicializar inotify con flags
    SYS_PREADV = 136,       // Leer vectorizado con offset
    SYS_PWRITEV = 137,      // Escribir vectorizado con offset
    SYS_RT_TGSIGQUEUEINFO = 138, // Encolar señal en tiempo real
    SYS_PERF_EVENT_OPEN = 139, // Abrir evento de performance
    SYS_RECVMMSG = 140,     // Recibir múltiples mensajes
    SYS_FANOTIFY_INIT = 141, // Inicializar fanotify
    SYS_FANOTIFY_MARK = 142, // Marcar archivo para fanotify
    SYS_PRLIMIT64 = 143,    // Obtener/establecer límite de recursos (64-bit)
    SYS_NAME_TO_HANDLE_AT = 144, // Convertir nombre a handle
    SYS_OPEN_BY_HANDLE_AT = 145, // Abrir por handle
    SYS_CLOCK_ADJTIME = 146, // Ajustar reloj del sistema
    SYS_SYNCFS = 147,       // Sincronizar filesystem
    SYS_SENDMMSG = 148,     // Enviar múltiples mensajes
    SYS_SETNS = 149,        // Establecer namespace
    SYS_PROCESS_VM_READV = 150, // Leer vectorizado de proceso
    SYS_PROCESS_VM_WRITEV = 151, // Escribir vectorizado a proceso
    SYS_KCMP = 152,         // Comparar procesos
    SYS_FINIT_MODULE = 153, // Cargar módulo del kernel
    SYS_SCHED_SETATTR = 154, // Establecer atributos de scheduling
    SYS_SCHED_GETATTR = 155, // Obtener atributos de scheduling
    SYS_RENAMEAT2 = 156,    // Renombrar archivo (versión 2)
    SYS_SECCOMP = 157,      // Configurar seccomp
    SYS_GETRANDOM = 158,    // Obtener bytes aleatorios
    SYS_MEMFD_CREATE = 159, // Crear descriptor de memoria anónimo
    SYS_KEXEC_FILE_LOAD = 160, // Cargar kernel para kexec
    SYS_BPF = 161,          // Sistema de Berkeley Packet Filter
    SYS_EXECVEAT = 162,     // Ejecutar programa relativo
    SYS_USERFAULTFD = 163,  // Crear descriptor de userfault
    SYS_MEMBARRIER = 164,   // Barrera de memoria
    SYS_MLOCK2 = 165,       // Bloquear memoria (versión 2)
    SYS_COPY_FILE_RANGE = 166, // Copiar rango de archivo
    SYS_PREADV2 = 167,      // Leer vectorizado con offset y flags
    SYS_PWRITEV2 = 168,     // Escribir vectorizado con offset y flags
    SYS_PKEY_MPROTECT = 169, // Proteger memoria con key
    SYS_PKEY_ALLOC = 170,   // Allocar protection key
    SYS_PKEY_FREE = 171,    // Liberar protection key
    SYS_STATX = 172,        // Obtener información de archivo extendida
    SYS_IO_PGETEVENTS = 173, // Obtener eventos AIO con timeout
    SYS_RSEQ = 174,         // Restartable sequences
    SYS_PIDFD_SEND_SIGNAL = 175, // Enviar señal por file descriptor
    SYS_IO_URING_SETUP = 176, // Configurar io_uring
    SYS_IO_URING_ENTER = 177, // Entrar en io_uring
    SYS_IO_URING_REGISTER = 178, // Registrar buffers/archivos
    SYS_OPEN_TREE = 179,    // Abrir árbol de archivos
    SYS_MOVE_MOUNT = 180,   // Mover mount point
    SYS_FSOPEN = 181,       // Abrir filesystem
    SYS_FSCONFIG = 182,     // Configurar filesystem
    SYS_FSMOUNT = 183,      // Montar filesystem
    SYS_FSPICK = 184,       // Seleccionar filesystem
    SYS_PIDFD_OPEN = 185,   // Abrir file descriptor de proceso
    SYS_CLONE3 = 186,       // Clonar proceso/thread (versión 3)
    SYS_CLOSE_RANGE = 187,  // Cerrar rango de descriptores
    SYS_OPENAT2 = 188,      // Abrir archivo relativo (versión 2)
    SYS_PIDFD_GETFD = 189,  // Obtener file descriptor de proceso
    SYS_FACCESSAT2 = 190,   // Verificar acceso a archivo relativo (versión 2)
    SYS_PROCESS_MADVISE = 191, // Consejo de memoria para proceso
    SYS_EPOLL_PWAIT2 = 192, // Esperar eventos epoll con timeout (versión 2)
    SYS_MOUNT_SETATTR = 193, // Establecer atributos de mount
    SYS_QUOTACTL_FD = 194,  // Control de cuotas por file descriptor
    SYS_LANDLOCK_CREATE_RULESET = 195, // Crear ruleset de Landlock
    SYS_LANDLOCK_ADD_RULE = 196, // Agregar regla a Landlock
    SYS_LANDLOCK_RESTRICT_SELF = 197, // Restringir proceso con Landlock
    SYS_MEMFD_SECRET = 198, // Crear descriptor de memoria secreta
    SYS_PROCESS_MRELEASE = 199, // Liberar memoria de proceso
    SYS_WAITPID = 200,      // Esperar proceso específico
    SYS_OLDLSTAT = 201,     // Obtener información de enlace (legacy)
    SYS_OLDSELECT = 202,    // Select (legacy)
    SYS_OLDLSEEK = 203,     // Lseek (legacy)
    SYS_OLDFSTAT = 204,     // Obtener información de descriptor (legacy)
    SYS_OLDFCNTL = 205,     // Fcntl (legacy)
    SYS_OLDFSYNC = 206,     // Fsync (legacy)
    SYS_OLDFTRUNCATE = 207, // Ftruncate (legacy)
    SYS_OLDFSTATAT = 208,   // Fstatat (legacy)
    SYS_OLDLSTATAT = 209,   // Lstatat (legacy)
    SYS_OLDFSTATAT64 = 210, // Fstatat64 (legacy)
    SYS_OLDLSTATAT64 = 211, // Lstatat64 (legacy)
    SYS_OLDFSTAT64 = 212,   // Fstat64 (legacy)
    SYS_OLDLSTAT64 = 213,   // Lstat64 (legacy)
    SYS_OLDSTAT64 = 214,    // Stat64 (legacy)
    SYS_OLDFTRUNCATE64 = 215, // Ftruncate64 (legacy)
    SYS_OLDLSEEK64 = 216,   // Lseek64 (legacy)
    SYS_OLDFCNTL64 = 217,   // Fcntl64 (legacy)
    SYS_OLDFSYNC64 = 218,   // Fsync64 (legacy)
    SYS_OLDFSTATAT64_2 = 219, // Fstatat64 (legacy, versión 2)
    SYS_OLDLSTATAT64_2 = 220, // Lstatat64 (legacy, versión 2)
    SYS_OLDFSTAT64_2 = 221, // Fstat64 (legacy, versión 2)
    SYS_OLDLSTAT64_2 = 222, // Lstat64 (legacy, versión 2)
    SYS_OLDSTAT64_2 = 223,  // Stat64 (legacy, versión 2)
    SYS_OLDFTRUNCATE64_2 = 224, // Ftruncate64 (legacy, versión 2)
    SYS_OLDLSEEK64_2 = 225, // Lseek64 (legacy, versión 2)
    SYS_OLDFCNTL64_2 = 226, // Fcntl64 (legacy, versión 2)
    SYS_OLDFSYNC64_2 = 227, // Fsync64 (legacy, versión 2)
    SYS_OLDFSTATAT64_3 = 228, // Fstatat64 (legacy, versión 3)
    SYS_OLDLSTATAT64_3 = 229, // Lstatat64 (legacy, versión 3)
    SYS_OLDFSTAT64_3 = 230, // Fstat64 (legacy, versión 3)
    SYS_OLDLSTAT64_3 = 231, // Lstat64 (legacy, versión 3)
    SYS_OLDSTAT64_3 = 232,  // Stat64 (legacy, versión 3)
    SYS_OLDFTRUNCATE64_3 = 233, // Ftruncate64 (legacy, versión 3)
    SYS_OLDLSEEK64_3 = 234, // Lseek64 (legacy, versión 3)
    SYS_OLDFCNTL64_3 = 235, // Fcntl64 (legacy, versión 3)
    SYS_OLDFSYNC64_3 = 236, // Fsync64 (legacy, versión 3)
    SYS_OLDFSTATAT64_4 = 237, // Fstatat64 (legacy, versión 4)
    SYS_OLDLSTATAT64_4 = 238, // Lstatat64 (legacy, versión 4)
    SYS_OLDFSTAT64_4 = 239, // Fstat64 (legacy, versión 4)
    SYS_OLDLSTAT64_4 = 240, // Lstat64 (legacy, versión 4)
    SYS_OLDSTAT64_4 = 241,  // Stat64 (legacy, versión 4)
    SYS_OLDFTRUNCATE64_4 = 242, // Ftruncate64 (legacy, versión 4)
    SYS_OLDLSEEK64_4 = 243, // Lseek64 (legacy, versión 4)
    SYS_OLDFCNTL64_4 = 244, // Fcntl64 (legacy, versión 4)
    SYS_OLDFSYNC64_4 = 245, // Fsync64 (legacy, versión 4)
    SYS_OLDFSTATAT64_5 = 246, // Fstatat64 (legacy, versión 5)
    SYS_OLDLSTATAT64_5 = 247, // Lstatat64 (legacy, versión 5)
    SYS_OLDFSTAT64_5 = 248, // Fstat64 (legacy, versión 5)
    SYS_OLDLSTAT64_5 = 249, // Lstat64 (legacy, versión 5)
    SYS_OLDSTAT64_5 = 250,  // Stat64 (legacy, versión 5)
    SYS_OLDFTRUNCATE64_5 = 251, // Ftruncate64 (legacy, versión 5)
    SYS_OLDLSEEK64_5 = 252, // Lseek64 (legacy, versión 5)
    SYS_OLDFCNTL64_5 = 253, // Fcntl64 (legacy, versión 5)
    SYS_OLDFSYNC64_5 = 254, // Fsync64 (legacy, versión 5)
    SYS_OLDFSTATAT64_6 = 255  // Fstatat64 (legacy, versión 6)
} syscall_number_t;

// ===============================================================================
// TIPOS DE DATOS PARA SYSTEM CALLS
// ===============================================================================

// Estructura para argumentos de system call
typedef struct 
{
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
} syscall_args_t;

// Estructura para información de archivo
typedef struct 
{
    uint64_t st_dev;        // ID del dispositivo
    uint64_t st_ino;        // Número de inode
    uint64_t st_nlink;      // Número de enlaces duros
    uint32_t st_mode;       // Modo de archivo
    uint32_t st_uid;        // ID del usuario propietario
    uint32_t st_gid;        // ID del grupo propietario
    uint64_t st_rdev;       // ID del dispositivo (si es especial)
    uint64_t st_size;       // Tamaño en bytes
    uint64_t st_blksize;    // Tamaño de bloque para I/O
    uint64_t st_blocks;     // Número de bloques asignados
    uint64_t st_atime;      // Tiempo de último acceso
    uint64_t st_mtime;      // Tiempo de última modificación
    uint64_t st_ctime;      // Tiempo de último cambio de estado
} stat_t;

// Estructura para información de proceso
typedef struct 
{
    uint32_t si_signo;      // Número de señal
    uint32_t si_errno;      // Código de error
    uint32_t si_code;       // Código de señal
    uint32_t si_pid;        // PID del proceso que envió la señal
    uint32_t si_uid;        // UID del proceso que envió la señal
    uint32_t si_status;     // Estado de salida o señal
    uint64_t si_addr;       // Dirección que causó la señal
    uint64_t si_value;      // Valor de señal
    uint64_t si_band;       // Bandera de señal
    uint64_t si_fd;         // File descriptor
    uint64_t si_timer1;     // Timer 1
    uint64_t si_timer2;     // Timer 2
} siginfo_t;

// ===============================================================================
// FUNCIONES DEL SISTEMA DE SYSTEM CALLS
// ===============================================================================

// Inicialización
void syscalls_init(void);

// Handlers de system calls
int64_t syscall_handler(uint64_t number, syscall_args_t *args);

// Wrappers de system calls
void sys_exit_wrapper(syscall_args_t *args);
void sys_fork_wrapper(syscall_args_t *args);
void sys_read_wrapper(syscall_args_t *args);
void sys_write_wrapper(syscall_args_t *args);
void sys_open_wrapper(syscall_args_t *args);
void sys_close_wrapper(syscall_args_t *args);
void sys_exec_wrapper(syscall_args_t *args);
void sys_wait_wrapper(syscall_args_t *args);
void sys_kill_wrapper(syscall_args_t *args);
void sys_getpid_wrapper(syscall_args_t *args);
void sys_getppid_wrapper(syscall_args_t *args);
void sys_sleep_wrapper(syscall_args_t *args);
void sys_yield_wrapper(syscall_args_t *args);
void sys_brk_wrapper(syscall_args_t *args);
void sys_mmap_wrapper(syscall_args_t *args);
void sys_munmap_wrapper(syscall_args_t *args);
void sys_gettime_wrapper(syscall_args_t *args);
void sys_getuid_wrapper(syscall_args_t *args);
void sys_setuid_wrapper(syscall_args_t *args);
void sys_chdir_wrapper(syscall_args_t *args);
void sys_getcwd_wrapper(syscall_args_t *args);
void sys_mkdir_wrapper(syscall_args_t *args);
void sys_rmdir_wrapper(syscall_args_t *args);
void sys_link_wrapper(syscall_args_t *args);
void sys_unlink_wrapper(syscall_args_t *args);
void sys_stat_wrapper(syscall_args_t *args);
void sys_fstat_wrapper(syscall_args_t *args);
void sys_lseek_wrapper(syscall_args_t *args);
void sys_dup_wrapper(syscall_args_t *args);
void sys_dup2_wrapper(syscall_args_t *args);
void sys_pipe_wrapper(syscall_args_t *args);
void sys_alarm_wrapper(syscall_args_t *args);
void sys_signal_wrapper(syscall_args_t *args);
void sys_sigaction_wrapper(syscall_args_t *args);
void sys_sigprocmask_wrapper(syscall_args_t *args);
void sys_sigsuspend_wrapper(syscall_args_t *args);

// System calls básicas
int64_t sys_exit(int exit_code);
int64_t sys_fork(void);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_close(int fd);
int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[]);
int64_t sys_wait(int *status);
int64_t sys_kill(pid_t pid, int sig);
int64_t sys_getpid(void);
int64_t sys_getppid(void);
int64_t sys_sleep(uint32_t ms);
int64_t sys_yield(void);

// System calls de memoria
int64_t sys_brk(void *addr);
int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int64_t sys_munmap(void *addr, size_t length);

// System calls de archivos
int64_t sys_stat(const char *pathname, stat_t *statbuf);
int64_t sys_fstat(int fd, stat_t *statbuf);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_dup(int oldfd);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_pipe(int pipefd[2]);

// System calls de directorios
int64_t sys_chdir(const char *path);
int64_t sys_getcwd(char *buf, size_t size);
int64_t sys_mkdir(const char *pathname, mode_t mode);
int64_t sys_rmdir(const char *pathname);
int64_t sys_link(const char *oldpath, const char *newpath);
int64_t sys_unlink(const char *pathname);
int64_t sys_rename(const char *oldpath, const char *newpath);

// System calls de señales
int64_t sys_signal(int signum, void (*handler)(int));
int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int64_t sys_sigsuspend(const sigset_t *mask);

// System calls de tiempo
int64_t sys_gettime(void);
int64_t sys_alarm(unsigned int seconds);

// System calls de usuarios
int64_t sys_getuid(void);
int64_t sys_geteuid(void);
int64_t sys_getgid(void);
int64_t sys_getegid(void);
int64_t sys_setuid(uid_t uid);
int64_t sys_seteuid(uid_t euid);
int64_t sys_setgid(gid_t gid);
int64_t sys_setegid(gid_t egid);

// System calls de recursos
int64_t sys_getrlimit(int resource, struct rlimit *rlim);
int64_t sys_setrlimit(int resource, const struct rlimit *rlim);
int64_t sys_getrusage(int who, struct rusage *usage);
int64_t sys_times(struct tms *buf);

// System calls de debugging
int64_t sys_ptrace(long request, pid_t pid, void *addr, void *data);

// ===============================================================================
// MACROS ÚTILES
// ===============================================================================

#define SYSCALL_RETURN(value) return (int64_t)(value)
#define SYSCALL_ERROR(errno) return (int64_t)(-errno)
#define SYSCALL_SUCCESS(value) return (int64_t)(value)

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

extern void (*syscall_table[MAX_SYSCALLS])(syscall_args_t *);
