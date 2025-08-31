# IR0 Kernel - Quick Start Guide
# IR0 Kernel - Guía de Inicio Rápido

## 🚀 Quick Commands / Comandos Rápidos

### Run Kernel / Ejecutar Kernel
```bash
# 64-bit with GUI / 64-bit con GUI
./scripts/quick_run.sh -64 -g

# 32-bit without GUI / 32-bit sin GUI  
./scripts/quick_run.sh -32 -n

# 64-bit with debugging / 64-bit con debugging
./scripts/quick_run.sh -64 -d

# 32-bit with debugging in terminal / 32-bit con debugging en terminal
./scripts/quick_run.sh -32 -d -n
```

### Alternative Commands / Comandos Alternativos
```bash
# Make commands / Comandos make:
make run-64                                # 64-bit with GUI
make run-32                                # 32-bit with GUI
make run-64-debug                          # 64-bit with debugging
make run-32-debug-nographic                # 32-bit with debugging in terminal
```

## 🐛 Debugging / Depuración

### Run with Debugging / Ejecutar con Debugging
```bash
# Generate debug logs / Generar logs de debugging
./scripts/quick_run.sh -64 -d

# Analyze debug logs / Analizar logs de debugging
./scripts/analyze_debug_logs.sh qemu_debug.log
```

### Debug Options / Opciones de Debug
```bash
# Analyze specific issues / Analizar problemas específicos:
./scripts/analyze_debug_logs.sh -p qemu_debug.log    # Page faults
./scripts/analyze_debug_logs.sh -i qemu_debug.log    # Interrupts
./scripts/analyze_debug_logs.sh -s qemu_debug.log    # Summary only
```

## ⚙️ Configuration / Configuración

### Kernel Configuration / Configuración del Kernel
```c
// In kernel_start.c:
#define IR0_DEVELOPMENT_MODE    // For testing
#define IR0_DESKTOP            // For desktop
#define IR0_SERVER             // For server
#define IR0_IOT                // For IoT
#define IR0_EMBEDDED           // For embedded
```

### What Each Mode Enables / Qué Habilita Cada Modo

| Mode | Memory | Process | File System | Drivers | GUI |
|------|--------|---------|-------------|---------|-----|
| **DEVELOPMENT** | ✅ Bump | ❌ | ❌ | ✅ All | ❌ |
| **DESKTOP** | ✅ All | ✅ All | ✅ All | ✅ All | ✅ |
| **SERVER** | ✅ All | ✅ All | ✅ All | ✅ All | ❌ |
| **IoT** | ✅ Basic | ❌ | ✅ Basic | ✅ Basic | ❌ |
| **EMBEDDED** | ✅ Bump | ❌ | ❌ | ✅ Basic | ❌ |

## 🛠️ Build Commands / Comandos de Compilación

### Clean Build / Compilación Limpia
```bash
# Clean and build / Limpiar y compilar
./scripts/quick_run.sh -64 -c -g

# Manual clean / Limpieza manual
make clean
make ARCH=x86-64
```

### All Architectures / Todas las Arquitecturas
```bash
make all-arch           # Build all architectures
make all-targets        # Build all targets
make all-combinations   # Build all combinations
```

## 📁 Important Files / Archivos Importantes

- `kernel/kernel_start.c` - Main kernel entry point
- `setup/subsystem_config.h` - Subsystem configuration
- `includes/ir0/kernel_includes.h` - Conditional includes
- `scripts/quick_run.sh` - Quick run script
- `scripts/analyze_debug_logs.sh` - Debug log analyzer

## 🎯 Common Use Cases / Casos de Uso Comunes

### Testing Bump Allocator / Testing del Bump Allocator
```bash
# 1. Configure kernel / Configurar kernel
# In kernel_start.c: #define IR0_DEVELOPMENT_MODE

# 2. Run with debugging / Ejecutar con debugging
./scripts/quick_run.sh -64 -d

# 3. Analyze results / Analizar resultados
./scripts/analyze_debug_logs.sh -p qemu_debug.log
```

### Development Workflow / Flujo de Desarrollo
```bash
# 1. Make changes / Hacer cambios
# Edit kernel code

# 2. Clean build / Compilación limpia
./scripts/quick_run.sh -64 -c -g

# 3. Test with debugging / Testing con debugging
./scripts/quick_run.sh -64 -d

# 4. Analyze logs / Analizar logs
./scripts/analyze_debug_logs.sh qemu_debug.log
```

## 🔧 Troubleshooting / Solución de Problemas

### Common Issues / Problemas Comunes

**QEMU not found / QEMU no encontrado:**
```bash
sudo apt install qemu-system-x86 qemu-system-i386
```

**Permission denied / Permiso denegado:**
```bash
chmod +x scripts/*.sh
```

**Build errors / Errores de compilación:**
```bash
make clean
./scripts/quick_run.sh -64 -c -g
```

**Debug logs not found / Logs de debugging no encontrados:**
```bash
# Run with debugging first / Ejecutar con debugging primero
./scripts/quick_run.sh -64 -d
```

## 🎉 That's It! / ¡Eso es Todo!

**One command to rule them all! / ¡Un comando para gobernarlos a todos!**

```bash
./scripts/quick_run.sh -64 -g    # Start here / Comenzar aquí
```
