// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM STRUCTURES (Linux 0.0.1 style)
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// MINIX FILESYSTEM CONSTANTS
// ===============================================================================

#define MINIX_BLOCK_SIZE 1024
#define MINIX_INODE_SIZE 32
#define MINIX_NAME_LEN 14
#define MINIX_DIR_ENTRY_SIZE 16

// ===============================================================================
// MINIX INODE STRUCTURE (Linux 0.0.1 style)
// ===============================================================================

typedef struct minix_inode 
{
    uint16_t i_mode;      // Permisos y tipo de archivo
    uint16_t i_uid;       // ID del usuario propietario
    uint32_t i_size;      // Tamaño del archivo en bytes
    uint32_t i_time;      // Tiempo de modificación
    uint8_t i_gid;        // ID del grupo
    uint8_t i_nlinks;     // Número de enlaces
    uint16_t i_zone[9];   // Zonas/bloques (7 directos + 1 indirecto + 1 doble indirecto)
} minix_inode_t;

// ===============================================================================
// MINIX DIRECTORY ENTRY STRUCTURE
// ===============================================================================

typedef struct minix_dir_entry 
{
    uint16_t inode;       // Número de inode
    char name[MINIX_NAME_LEN]; // Nombre del archivo
} minix_dir_entry_t;

// ===============================================================================
// MINIX SUPERBLOCK STRUCTURE
// ===============================================================================

typedef struct minix_superblock 
{
    uint16_t s_ninodes;   // Número de inodes
    uint16_t s_nzones;    // Número de zonas
    uint16_t s_imap_blocks; // Bloques del bitmap de inodes
    uint16_t s_zmap_blocks; // Bloques del bitmap de zonas
    uint16_t s_firstdatazone; // Primera zona de datos
    uint16_t s_log_zone_size; // Tamaño de zona en bloques
    uint32_t s_max_size;  // Tamaño máximo de archivo
    uint16_t s_magic;     // Número mágico
} minix_superblock_t;

// ===============================================================================
// INODE MODE FLAGS
// ===============================================================================

#define MINIX_IFMT  00170000  // Tipo de archivo
#define MINIX_IFDIR  0040000  // Directorio
#define MINIX_IFCHR  0020000  // Dispositivo de caracteres
#define MINIX_IFBLK  0060000  // Dispositivo de bloques
#define MINIX_IFREG  0100000  // Archivo regular
#define MINIX_IFLNK  0120000  // Enlace simbólico
#define MINIX_IFSOCK 0140000  // Socket

// Permisos
#define MINIX_IRWXU 0000700  // Usuario: lectura, escritura, ejecución
#define MINIX_IRUSR 0000400  // Usuario: lectura
#define MINIX_IWUSR 0000200  // Usuario: escritura
#define MINIX_IXUSR 0000100  // Usuario: ejecución

#define MINIX_IRWXG 0000070  // Grupo: lectura, escritura, ejecución
#define MINIX_IRGRP 0000040  // Grupo: lectura
#define MINIX_IWGRP 0000020  // Grupo: escritura
#define MINIX_IXGRP 0000010  // Grupo: ejecución

#define MINIX_IRWXO 0000007  // Otros: lectura, escritura, ejecución
#define MINIX_IROTH 0000004  // Otros: lectura
#define MINIX_IWOTH 0000002  // Otros: escritura
#define MINIX_IXOTH 0000001  // Otros: ejecución

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Verificar si un inode es un directorio
static inline bool minix_is_dir(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFDIR;
}

// Verificar si un inode es un archivo regular
static inline bool minix_is_reg(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFREG;
}

// Verificar si un inode es un dispositivo de caracteres
static inline bool minix_is_chr(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFCHR;
}

// Verificar si un inode es un dispositivo de bloques
static inline bool minix_is_blk(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IFMT) == MINIX_IFBLK;
}

// Obtener permisos de usuario
static inline uint16_t minix_get_uid_perms(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IRWXU) >> 6;
}

// Obtener permisos de grupo
static inline uint16_t minix_get_gid_perms(const minix_inode_t *inode) {
    return (inode->i_mode & MINIX_IRWXG) >> 3;
}

// Obtener permisos de otros
static inline uint16_t minix_get_oth_perms(const minix_inode_t *inode) {
    return inode->i_mode & MINIX_IRWXO;
}

// ===============================================================================
// MINIX FILESYSTEM FUNCTIONS
// ===============================================================================

// Inicializar el filesystem Minix
int minix_fs_init(void);

// Formatear el disco con Minix filesystem
int minix_fs_format(void);

// Crear directorio
int minix_fs_mkdir(const char *path);

// Listar directorio
int minix_fs_ls(const char *path);

// Limpiar el filesystem
void minix_fs_cleanup(void);
