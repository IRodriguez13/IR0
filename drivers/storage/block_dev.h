/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Block device abstraction
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Interfaz genérica para dispositivos de bloque. Los drivers (ATA, AHCI, etc.)
 * registran sus dispositivos; el VFS y los sistemas de archivos usan esta capa
 * en lugar de acceder al hardware directamente.
 */

#ifndef IR0_BLOCK_DEV_H
#define IR0_BLOCK_DEV_H

#include <stdbool.h>
#include <stdint.h>

#define BLOCK_DEV_SECTOR_SIZE 512

/**
 * block_dev_ops_t - Operaciones de un dispositivo de bloque
 *
 * Todas las operaciones usan sectores de 512 bytes. El dev_id identifica
 * el dispositivo concreto dentro del driver (ej. 0=hda, 1=hdb para ATA).
 */
typedef struct block_dev_ops {
	bool (*read_sectors)(uint8_t dev_id, uint32_t lba, uint8_t n, void *buf);
	bool (*write_sectors)(uint8_t dev_id, uint32_t lba, uint8_t n, const void *buf);
	uint64_t (*get_sector_count)(uint8_t dev_id);
	bool (*is_present)(uint8_t dev_id);
} block_dev_ops_t;

/**
 * block_dev_register - Registra un dispositivo de bloque
 * @name: Nombre lógico (ej. "hda", "hdb")
 * @ops: Operaciones del driver
 * @dev_id: ID del dispositivo dentro del driver
 *
 * Returns: 0 en éxito, -1 si el nombre ya existe o la tabla está llena
 */
int block_dev_register(const char *name, const block_dev_ops_t *ops, uint8_t dev_id);

/**
 * block_dev_get - Obtiene operaciones e ID de un dispositivo por nombre
 * @name: Nombre del dispositivo
 * @dev_id_out: Donde escribir el dev_id (puede ser NULL)
 *
 * Returns: Puntero a ops o NULL si no existe
 */
const block_dev_ops_t *block_dev_get(const char *name, uint8_t *dev_id_out);

/**
 * block_dev_read_sectors - Lee sectores de un dispositivo por nombre
 * @name: Nombre del dispositivo
 * @lba: Dirección LBA de inicio
 * @n: Número de sectores (máx 255)
 * @buf: Buffer de destino
 *
 * Returns: true si OK, false en error
 */
bool block_dev_read_sectors(const char *name, uint32_t lba, uint8_t n, void *buf);

/**
 * block_dev_write_sectors - Escribe sectores en un dispositivo por nombre
 */
bool block_dev_write_sectors(const char *name, uint32_t lba, uint8_t n, const void *buf);

/**
 * block_dev_get_sector_count - Obtiene el número total de sectores
 */
uint64_t block_dev_get_sector_count(const char *name);

/**
 * block_dev_is_present - Comprueba si el dispositivo está presente
 */
bool block_dev_is_present(const char *name);

/**
 * block_dev_count - Número de dispositivos de bloque registrados
 */
int block_dev_count(void);

/**
 * block_dev_name_at - Nombre de dispositivo por índice de registro
 *
 * Returns: puntero al nombre o NULL si el índice es inválido.
 */
const char *block_dev_name_at(int index);

/**
 * block_dev_legacy_name - Mapea disk_id legado (0..3) a nombre lógico
 *
 * Compatibilidad temporal para rutas que aún usan disk_id estilo ATA.
 * Returns: "hda"/"hdb"/"hdc"/"hdd" o NULL si disk_id inválido.
 */
const char *block_dev_legacy_name(uint8_t disk_id);

/**
 * ata_block_register - Registra los discos ATA en block_dev
 *
 * Llamar tras ata_init(). Registra hda, hdb, hdc, hdd según corresponda.
 */
void ata_block_register(void);

#endif /* IR0_BLOCK_DEV_H */
