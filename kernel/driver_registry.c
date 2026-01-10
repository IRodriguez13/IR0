// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Driver Registry Implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: driver_registry.c
 * Description: Multi-language driver registration and management system
 */

#include <ir0/driver.h>
#include <ir0/memory/kmem.h>
#include <ir0/logging.h>
#include <ir0/validation.h>
#include <string.h>
#include <drivers/serial/serial.h>

/* Maximum number of drivers (can be made dynamic) */
#define MAX_DRIVERS 128

/* Driver internal structure */
struct ir0_driver {
    ir0_driver_info_t info;
    ir0_driver_ops_t ops;
    ir0_driver_state_t state;
    void* private_data;             // Driver-specific data
    struct ir0_driver* next;        // Linked list
};

/* Driver registry state */
static struct {
    ir0_driver_t* drivers;          // Linked list of drivers
    size_t count;                   // Number of registered drivers
    int initialized;                // Registry initialized flag
} driver_registry = { NULL, 0, 0 };

/* INTERNAL HELPER FUNCTIONS */

static const char* lang_to_string(ir0_driver_lang_t lang)
{
    switch (lang) {
        case IR0_DRIVER_LANG_C:    return "C";
        case IR0_DRIVER_LANG_CPP:  return "C++";
        case IR0_DRIVER_LANG_RUST: return "Rust";
        default:                   return "Unknown";
    }
}

static const char* state_to_string(ir0_driver_state_t state)
{
    switch (state) {
        case IR0_DRIVER_STATE_UNREGISTERED: return "Unregistered";
        case IR0_DRIVER_STATE_REGISTERED:   return "Registered";
        case IR0_DRIVER_STATE_INITIALIZED:  return "Initialized";
        case IR0_DRIVER_STATE_ACTIVE:       return "Active";
        case IR0_DRIVER_STATE_FAILED:       return "Failed";
        default:                            return "Unknown";
    }
}

static int validate_driver_info(const ir0_driver_info_t* info)
{
    if (!info) {
        LOG_ERROR("DriverRegistry", "Driver info is NULL");
        return 0;
    }
    
    if (!info->name || strlen(info->name) == 0) {
        LOG_ERROR("DriverRegistry", "Driver name is NULL or empty");
        return 0;
    }
    
    if (strlen(info->name) > 64) {
        LOG_ERROR_FMT("DriverRegistry", "Driver name too long: %s", info->name);
        return 0;
    }
    
    return 1;
}

static int validate_driver_ops(const ir0_driver_ops_t* ops)
{
    if (!ops) {
        LOG_ERROR("DriverRegistry", "Driver ops is NULL");
        return 0;
    }
    
    /* At least init function is required */
    if (!ops->init) {
        LOG_ERROR("DriverRegistry", "Driver must have init function");
        return 0;
    }
    
    return 1;
}

static ir0_driver_t* find_driver_by_name(const char* name)
{
    if (!name) return NULL;
    
    ir0_driver_t* current = driver_registry.drivers;
    while (current) {
        if (strcmp(current->info.name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/* PUBLIC API IMPLEMENTATION */

void ir0_driver_registry_init(void)
{
    if (driver_registry.initialized) {
        LOG_WARNING("DriverRegistry", "Driver registry already initialized");
        return;
    }
    
    driver_registry.drivers = NULL;
    driver_registry.count = 0;
    driver_registry.initialized = 1;
    
    LOG_INFO("DriverRegistry", "Driver registry initialized");
}

ir0_driver_t* ir0_register_driver(const ir0_driver_info_t* info, 
                                   const ir0_driver_ops_t* ops)
{
    /* Validate inputs */
    if (!validate_driver_info(info)) 
    {
        return NULL;
    }
    
    if (!validate_driver_ops(ops)) 
    {
        return NULL;
    }
    
    /* Check if registry is initialized */
    if (!driver_registry.initialized) 
    {
        LOG_WARNING("DriverRegistry", "Driver registry not initialized, initializing now");
        ir0_driver_registry_init();
    }
    
    /* Check if driver already exists */
    if (find_driver_by_name(info->name)) 
    {
        LOG_ERROR_FMT("DriverRegistry", "Driver '%s' already registered", info->name);
        return NULL;
    }
    
    /* Check driver limit */
    if (driver_registry.count >= MAX_DRIVERS) 
    {
        LOG_ERROR_FMT("DriverRegistry", "Maximum number of drivers (%d) reached", MAX_DRIVERS);
        return NULL;
    }
    
    /* Allocate driver structure */
    ir0_driver_t* driver = (ir0_driver_t*)kmalloc(sizeof(ir0_driver_t));
    
    if (!driver) 
    {
        LOG_ERROR_FMT("DriverRegistry", "Failed to allocate memory for driver '%s'", info->name);
        return NULL;
    }
    
    /* Allocate and copy driver name (persistent storage) */
    size_t name_len = strlen(info->name) + 1;
    char* name_copy = (char*)kmalloc(name_len);
    
    if (!name_copy) 
    {
        kfree(driver);
        LOG_ERROR("DriverRegistry", "Failed to allocate memory for driver name");
        return NULL;
    }
    
    memcpy(name_copy, info->name, name_len);
    
    /* Copy version if provided */
    char* version_copy = NULL;
    
    if (info->version) 
    {
        size_t version_len = strlen(info->version) + 1;
        version_copy = (char*)kmalloc(version_len);
    
        if (version_copy) 
        {
            memcpy(version_copy, info->version, version_len);
        }
    }
    
    /* Copy author if provided */
    char* author_copy = NULL;
    
    if (info->author) 
    {
        size_t author_len = strlen(info->author) + 1;
        author_copy = (char*)kmalloc(author_len);
    
        if (author_copy) 
        {
            memcpy(author_copy, info->author, author_len);
        }
    }
    
    /* Copy description if provided */
    char* desc_copy = NULL;
    
    if (info->description) 
    {
        size_t desc_len = strlen(info->description) + 1;
        desc_copy = (char*)kmalloc(desc_len);
     
        if (desc_copy) 
        {
            memcpy(desc_copy, info->description, desc_len);
        }
    }
    
    /* Initialize driver structure */
    driver->info.name = name_copy;
    driver->info.version = version_copy ? version_copy : "1.0";
    driver->info.author = author_copy ? author_copy : "Unknown";
    driver->info.description = desc_copy ? desc_copy : "";
    driver->info.language = info->language;
    driver->ops = *ops;
    driver->state = IR0_DRIVER_STATE_REGISTERED;
    driver->private_data = NULL;
    driver->next = NULL;
    
    /* Add to registry (at head of linked list) */
    driver->next = driver_registry.drivers;
    driver_registry.drivers = driver;
    driver_registry.count++;
    
    /* Log registration */
    LOG_INFO_FMT("DriverRegistry", "Registered driver: %s (v%s) [%s]", 
             driver->info.name, 
             driver->info.version,
             lang_to_string(driver->info.language));
    
    /* Call driver init function */
    if (driver->ops.init) 
    {
        LOG_INFO_FMT("DriverRegistry", "Initializing driver: %s", driver->info.name);
        int32_t result = driver->ops.init();
        
        if (result == 0) 
        {
            driver->state = IR0_DRIVER_STATE_INITIALIZED;
            LOG_INFO_FMT("DriverRegistry", "Driver '%s' initialized successfully", driver->info.name);
        } else 
        {
            driver->state = IR0_DRIVER_STATE_FAILED;
            LOG_ERROR_FMT("DriverRegistry", "Driver '%s' initialization failed: %d", driver->info.name, result);
        }
    }
    
    return driver;
}

int32_t ir0_unregister_driver(ir0_driver_t* driver)
{
    if (!driver) {
        LOG_ERROR("DriverRegistry", "Cannot unregister NULL driver");
        return IR0_DRIVER_ERR_INVAL;
    }
    
    /* Find driver in list */
    ir0_driver_t* current = driver_registry.drivers;
    ir0_driver_t* prev = NULL;
    
    while (current) {
        if (current == driver) {
            /* Call shutdown if available */
            if (driver->ops.shutdown) {
                LOG_INFO_FMT("DriverRegistry", "Shutting down driver: %s", driver->info.name);
                driver->ops.shutdown();
            }
            
            /* Remove from list */
            if (prev) {
                prev->next = driver->next;
            } else {
                driver_registry.drivers = driver->next;
            }
            
            /* Free allocated memory */
            if (driver->info.name) kfree((void*)driver->info.name);
            
            if (driver->info.version && strcmp(driver->info.version, "1.0") != 0) {
                kfree((void*)driver->info.version);
            }
            if (driver->info.author && strcmp(driver->info.author, "Unknown") != 0) {
                kfree((void*)driver->info.author);
            }
            if (driver->info.description && strlen(driver->info.description) > 0) {
                kfree((void*)driver->info.description);
            }
            kfree(driver);
            
            driver_registry.count--;
            LOG_INFO("DriverRegistry", "Driver unregistered successfully");
            
            return IR0_DRIVER_OK;
        }
        
        prev = current;
        current = current->next;
    }
    
    LOG_ERROR("DriverRegistry", "Driver not found in registry");
    return IR0_DRIVER_ERR_NOTFOUND;
}

ir0_driver_t* ir0_find_driver(const char* name)
{
    return find_driver_by_name(name);
}

ir0_driver_state_t ir0_driver_get_state(ir0_driver_t* driver)
{
    if (!driver) 
    {
        return IR0_DRIVER_STATE_UNREGISTERED;
    }
    
    return driver->state;
}


/**
 * List all registered drivers (for debugging)
 */
void ir0_driver_list_all(void)
{
    LOG_INFO_FMT("DriverRegistry", "=== Registered Drivers (%zu) ===", driver_registry.count);
    
    ir0_driver_t* current = driver_registry.drivers;
    int index = 1;
    
    while (current) 
    {
        LOG_INFO_FMT("DriverRegistry", "%d. %s (v%s) - %s [%s] - State: %s",
                index,
                current->info.name,
                current->info.version,
                current->info.description,
                lang_to_string(current->info.language),
                state_to_string(current->state));
        
        current = current->next;
        index++;
    }
    
    if (driver_registry.count == 0) 
    {
        LOG_INFO("DriverRegistry", "No drivers registered");
    }
}

int ir0_driver_list_to_buffer(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    /* Initialize buffer to zero */
    memset(buf, 0, count);

    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "NAME\tVERSION\tLANG\tSTATE\tDESC\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;

    ir0_driver_t *current = driver_registry.drivers;
    while (current && off < count - 1)
    {
        const char *lang = lang_to_string(current->info.language);
        const char *state = state_to_string(current->state);

        n = snprintf(buf + off, count - off,
                     "%s\t%s\t%s\t%s\t%s\n",
                     current->info.name ? current->info.name : "",
                     current->info.version ? current->info.version : "",
                     lang ? lang : "",
                     state ? state : "",
                     current->info.description ? current->info.description : "");
        if (n < 0)
            break;
        if (n >= (int)(count - off))
            n = (int)(count - off) - 1;
        off += (size_t)n;
        current = current->next;
    }

    /* Ensure null termination */
    if (off < count)
        buf[off] = '\0';

    return (int)off;
}
