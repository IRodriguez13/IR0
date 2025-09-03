/*
IR0 Kernel - Sistema de Estrategias de Compilación
==================================================

Este panel te permite tener estrategias de compilación según el caso de uso que le des al Kernel IR0.
Si es para servidor o espacio de virtualización SSL, si es para integración con IoT, o si buscas utilizarlo en desk.

Cada uno referencia a un makefile que permite capar y compilar los subsistemas recomendados para tu objetivo, 
pero también podes compilar lo que necesites a mano.

ESTRATEGIAS DISPONIBLES:
- IR0_DESKTOP: Sistema de escritorio con GUI, audio, USB, networking
- IR0_SERVER: Servidor de alto rendimiento con networking, SSL, virtualización
- IR0_IOT: Sistema IoT ligero con power management, timers de baja potencia
- IR0_EMBEDDED: Sistema embebido mínimo sin GUI ni networking
*/

#ifndef IR0_KERNELCONFIG_H
#define IR0_KERNELCONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ===============================================================================
// ESTRATEGIAS DE COMPILACIÓN
// ===============================================================================

#ifdef IR0_DESKTOP
    // Estrategia Desktop: Sistema completo de escritorio
    #define IR0_STRATEGY_NAME "Desktop"
    #define IR0_STRATEGY_DESCRIPTION "Sistema de escritorio completo con GUI, audio y multimedia"
    
    // Características habilitadas
    #define IR0_ENABLE_GUI 1
    #define IR0_ENABLE_AUDIO 1
    #define IR0_ENABLE_USB 1
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 1
    #define IR0_ENABLE_PRINTING 1
    #define IR0_ENABLE_VFS 1
    #define IR0_ENABLE_TCPIP 1
    #define IR0_ENABLE_SOCKETS 1
    #define IR0_ENABLE_ETHERNET 1
    #define IR0_ENABLE_USB_DRIVER 1
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 1
    #define IR0_ENABLE_WINDOW_MANAGER 1
    #define IR0_ENABLE_SOUND_DRIVER 1
    #define IR0_ENABLE_AUDIO_MIXER 1
    #define IR0_ENABLE_USER_MODE 1
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 1
    
    // Configuración de memoria
    #define IR0_HEAP_SIZE (256 * 1024 * 1024)  // 256MB
    #define IR0_MAX_PROCESSES 1024
    #define IR0_MAX_THREADS 4096
    #define IR0_SCHEDULER_QUANTUM 10  // ms
    #define IR0_IO_BUFFER_SIZE (64 * 1024)  // 64KB
    
    // Funciones de inicialización
    void ir0_desktop_init(void) 
    {
        // Inicializar subsistemas de escritorio
        ir0_init_gui();
        ir0_init_audio();
        ir0_init_usb();
        ir0_init_networking();
        ir0_init_multimedia();
        ir0_init_printing();
    }

#elif defined(IR0_SERVER)
    // Estrategia Server: Servidor de alto rendimiento
    #define IR0_STRATEGY_NAME "Server"
    #define IR0_STRATEGY_DESCRIPTION "Servidor de alto rendimiento con networking y virtualización"
    
    // Características habilitadas
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 1
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    #define IR0_ENABLE_VFS 1
    #define IR0_ENABLE_TCPIP 1
    #define IR0_ENABLE_SOCKETS 1
    #define IR0_ENABLE_ETHERNET 1
    #define IR0_ENABLE_USB_DRIVER 1
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 0
    #define IR0_ENABLE_WINDOW_MANAGER 0
    #define IR0_ENABLE_SOUND_DRIVER 0
    #define IR0_ENABLE_AUDIO_MIXER 0
    #define IR0_ENABLE_USER_MODE 1
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 1
    #define IR0_ENABLE_NETWORK_SECURITY 1
    
    // Configuración de memoria
    #define IR0_HEAP_SIZE (1024 * 1024 * 1024)  // 1GB
    #define IR0_MAX_PROCESSES 4096
    #define IR0_MAX_THREADS 16384
    #define IR0_SCHEDULER_QUANTUM 5   // ms
    #define IR0_IO_BUFFER_SIZE (256 * 1024)  // 256KB
    
    // Funciones de inicialización
    void ir0_server_init(void) 
    {
        // Inicializar subsistemas de servidor
        ir0_init_networking();
        ir0_init_ssl();
        ir0_init_docker_runtime();
        ir0_init_virtualization();
        ir0_init_network_security();
    }

#elif defined(IR0_IOT)
    // Estrategia IoT: Sistema IoT ligero
    #define IR0_STRATEGY_NAME "IoT"
    #define IR0_STRATEGY_DESCRIPTION "Sistema IoT ligero con power management"
    
    // Características habilitadas
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 1
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    #define IR0_ENABLE_VFS 1
    #define IR0_ENABLE_TCPIP 1
    #define IR0_ENABLE_SOCKETS 1
    #define IR0_ENABLE_ETHERNET 1
    #define IR0_ENABLE_USB_DRIVER 0
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 0
    #define IR0_ENABLE_WINDOW_MANAGER 0
    #define IR0_ENABLE_SOUND_DRIVER 0
    #define IR0_ENABLE_AUDIO_MIXER 0
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 0
    #define IR0_ENABLE_POWER_MANAGEMENT 1
    #define IR0_ENABLE_SLEEP_MODES 1
    #define IR0_ENABLE_LOW_POWER_TIMERS 1
    
    // Configuración de memoria
    #define IR0_HEAP_SIZE (16 * 1024 * 1024)  // 16MB
    #define IR0_MAX_PROCESSES 64
    #define IR0_MAX_THREADS 256
    #define IR0_SCHEDULER_QUANTUM 20  // ms
    #define IR0_IO_BUFFER_SIZE (4 * 1024)  // 4KB
    
    // Funciones de inicialización
    void ir0_iot_init(void) 
    {
        // Inicializar subsistemas IoT
        ir0_init_lapic_timer();
        ir0_init_low_power_mode();
        ir0_init_network_lightweight();
        ir0_init_sensor_interface();
        ir0_init_power_management();
    }

#elif defined(IR0_EMBEDDED)
    // Estrategia Embedded: Sistema embebido mínimo
    #define IR0_STRATEGY_NAME "Embedded"
    #define IR0_STRATEGY_DESCRIPTION "Sistema embebido mínimo sin GUI ni networking"
    
    // Características habilitadas
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 0
    #define IR0_ENABLE_FILESYSTEM 0
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    #define IR0_ENABLE_VFS 0
    #define IR0_ENABLE_TCPIP 0
    #define IR0_ENABLE_SOCKETS 0
    #define IR0_ENABLE_ETHERNET 0
    #define IR0_ENABLE_USB_DRIVER 0
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 0
    #define IR0_ENABLE_WINDOW_MANAGER 0
    #define IR0_ENABLE_SOUND_DRIVER 0
    #define IR0_ENABLE_AUDIO_MIXER 0
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 0
    #define IR0_ENABLE_PROCESS_ISOLATION 0
    #define IR0_ENABLE_POWER_MANAGEMENT 1
    #define IR0_ENABLE_SLEEP_MODES 1
    #define IR0_ENABLE_LOW_POWER_TIMERS 1
    
    // Configuración de memoria
    #define IR0_HEAP_SIZE (4 * 1024 * 1024)  // 4MB
    #define IR0_MAX_PROCESSES 16
    #define IR0_MAX_THREADS 64
    #define IR0_SCHEDULER_QUANTUM 50  // ms
    #define IR0_IO_BUFFER_SIZE (1 * 1024)  // 1KB
    
    // Funciones de inicialización
    void ir0_embedded_init(void) 
    {
        // Inicializar subsistemas embebidos
        ir0_init_minimal_timer();
        ir0_init_low_power_mode();
        ir0_init_basic_io();
        ir0_init_power_management();
    }

#else
    // Configuración por defecto (Generic)
    #define IR0_STRATEGY_NAME "Generic"
    #define IR0_STRATEGY_DESCRIPTION "Configuración genérica del kernel"
    
    // Características habilitadas (mínimas)
    #define IR0_ENABLE_GUI 0
    #define IR0_ENABLE_AUDIO 0
    #define IR0_ENABLE_USB 0
    #define IR0_ENABLE_NETWORKING 0
    #define IR0_ENABLE_FILESYSTEM 1
    #define IR0_ENABLE_MULTIMEDIA 0
    #define IR0_ENABLE_PRINTING 0
    #define IR0_ENABLE_VFS 1
    #define IR0_ENABLE_TCPIP 0
    #define IR0_ENABLE_SOCKETS 0
    #define IR0_ENABLE_ETHERNET 0
    #define IR0_ENABLE_USB_DRIVER 0
    #define IR0_ENABLE_VGA_DRIVER 1
    #define IR0_ENABLE_FRAMEBUFFER 0
    #define IR0_ENABLE_WINDOW_MANAGER 0
    #define IR0_ENABLE_SOUND_DRIVER 0
    #define IR0_ENABLE_AUDIO_MIXER 0
    #define IR0_ENABLE_USER_MODE 0
    #define IR0_ENABLE_MEMORY_PROTECTION 1
    #define IR0_ENABLE_PROCESS_ISOLATION 0
    
    // Configuración de memoria
    #define IR0_HEAP_SIZE (64 * 1024 * 1024)  // 64MB
    #define IR0_MAX_PROCESSES 256
    #define IR0_MAX_THREADS 1024
    #define IR0_SCHEDULER_QUANTUM 15  // ms
    #define IR0_IO_BUFFER_SIZE (16 * 1024)  // 16KB
    
    // Funciones de inicialización
    void ir0_generic_init(void) 
    {
        // Inicialización genérica
        ir0_init_basic_subsystems();
    }
#endif

// ===============================================================================
// FUNCIONES DE INICIALIZACIÓN (STUBS - A IMPLEMENTAR)
// ===============================================================================

// Funciones de inicialización de subsistemas
void ir0_init_gui(void) 
{
    // TODO: Implementar inicialización de GUI
}

void ir0_init_audio(void) 
{
    // TODO: Implementar inicialización de audio
}

void ir0_init_usb(void) 
{
    // TODO: Implementar inicialización de USB
}

void ir0_init_networking(void) 
{
    // TODO: Implementar inicialización de networking
}

void ir0_init_multimedia(void) 
{
    // TODO: Implementar inicialización de multimedia
}

void ir0_init_printing(void) 
{
    // TODO: Implementar inicialización de printing
}

void ir0_init_ssl(void) 
{
    // TODO: Implementar inicialización de SSL
}

void ir0_init_docker_runtime(void) 
{
    // TODO: Implementar inicialización de Docker runtime
}

void ir0_init_virtualization(void) 
{
    // TODO: Implementar inicialización de virtualización
}

void ir0_init_network_security(void) 
{
    // TODO: Implementar inicialización de seguridad de red
}

void ir0_init_lapic_timer(void) 
{
    // TODO: Implementar inicialización de timer LAPIC
}

void ir0_init_low_power_mode(void) 
{
    // TODO: Implementar inicialización de modo de baja potencia
}

void ir0_init_network_lightweight(void) 
{
    // TODO: Implementar inicialización de networking ligero
}

void ir0_init_sensor_interface(void) 
{
    // TODO: Implementar inicialización de interfaz de sensores
}

void ir0_init_power_management(void) 
{
    // TODO: Implementar inicialización de gestión de energía
}

void ir0_init_minimal_timer(void) 
{
    // TODO: Implementar inicialización de timer mínimo
}

void ir0_init_basic_io(void) 
{
    // TODO: Implementar inicialización de I/O básico
}

void ir0_init_basic_subsystems(void) 
{
    // TODO: Implementar inicialización de subsistemas básicos
}

// ===============================================================================
// FUNCIONES DE CONFIGURACIÓN
// ===============================================================================

// Obtener información de la estrategia actual
const char* ir0_get_strategy_name(void);
const char* ir0_get_strategy_description(void);

// Verificar si una característica está habilitada
bool ir0_is_feature_enabled(const char* feature);

// Inicializar según la estrategia seleccionada
void ir0_init_strategy(void);

// Mostrar configuración actual
void ir0_print_strategy_config(void);

#endif // IR0_KERNELCONFIG_H
