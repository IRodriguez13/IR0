// fs/vfs_simple.h - Simplified Virtual File System Interface
#pragma once

#include <stdint.h>
#include <stdbool.h>


/**
 * Inicializa el sistema de archivos simple
 */
void vfs_simple_init(void);

/**
 * Crea un directorio
 * @param path Nombre del directorio a crear
 * @return 0 en éxito, -1 en error
 */
int vfs_simple_mkdir(const char* path);

/**
 * Lista el contenido de un directorio
 * @param path Ruta del directorio a listar
 * @return 0 en éxito, -1 en error
 */
int vfs_simple_ls(const char* path);

/**
 * Crea un archivo en un directorio
 * @param path Ruta del directorio
 * @param filename Nombre del archivo
 * @param size Tamaño del archivo
 * @return 0 en éxito, -1 en error
 */
int vfs_simple_create_file(const char* path, const char* filename, uint32_t size);

/**
 * Obtiene el número de directorios creados
 * @return Número de directorios
 */
int vfs_simple_get_directory_count(void);

/**
 * Obtiene el nombre de un directorio por índice
 * @param index Índice del directorio
 * @return Nombre del directorio o NULL si no existe
 */
const char* vfs_simple_get_directory_name(int index);

/**
 * Verifica si un archivo existe
 * @param pathname Ruta del archivo
 * @return 1 si existe, 0 si no existe
 */
int vfs_file_exists(const char *pathname);

/**
 * Verifica si un directorio existe
 * @param pathname Ruta del directorio
 * @return 1 si existe, 0 si no existe
 */
int vfs_directory_exists(const char *pathname);

// Funciones de compatibilidad
int vfs_allocate_sectors(int count);
int vfs_remove_directory(const char *path);
