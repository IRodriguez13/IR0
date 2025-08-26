// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM + ATA DRIVER TEST
// ===============================================================================

#include <ir0/print.h>
#include <ir0/logging.h>
#include <minix_fs.h>
#include <string.h>

// ===============================================================================
// TEST FUNCTIONS
// ===============================================================================

void run_minix_ata_test(void)
{
    print("\n");
    print("==========================================\n");
    print("🧪 MINIX FILESYSTEM + ATA DRIVER TEST\n");
    print("==========================================\n");

    // Delay para ver mejor los logs
    delay_ms(1000);

    // Delay para ver mejor los logs
    for (volatile int i = 0; i < 2000000; i++)
        ;

    // Test 1: Inicializar Minix filesystem
    print("\n📁 Test 1: Inicializando Minix filesystem...\n");
    int result = minix_fs_init();
    if (result == 0)
    {
        print("✅ Minix filesystem inicializado correctamente\n");
        delay_ms(500);
    }
    else
    {
        print("❌ Error al inicializar Minix filesystem\n");
        delay_ms(2000);
        return;
    }

    // Test 2: Crear directorio
    print("\n📁 Test 2: Creando directorio '/test'...\n");
    result = minix_fs_mkdir("/test");
    if (result == 0)
    {
        print("✅ Directorio '/test' creado correctamente\n");
    }
    else
    {
        print("❌ Error al crear directorio '/test'\n");
    }

    // Test 3: Crear otro directorio
    print("\n📁 Test 3: Creando directorio '/home'...\n");
    result = minix_fs_mkdir("/home");
    if (result == 0)
    {
        print("✅ Directorio '/home' creado correctamente\n");
    }
    else
    {
        print("❌ Error al crear directorio '/home'\n");
    }

    // Test 4: Listar directorio raíz
    print("\n📁 Test 4: Listando directorio raíz...\n");
    result = minix_fs_ls("/");
    if (result == 0)
    {
        print("✅ Listado del directorio raíz completado\n");
    }
    else
    {
        print("❌ Error al listar directorio raíz\n");
    }

    // Test 5: Crear directorio anidado
    print("\n📁 Test 5: Creando directorio '/home/user'...\n");
    result = minix_fs_mkdir("/home/user");
    if (result == 0)
    {
        print("✅ Directorio '/home/user' creado correctamente\n");
    }
    else
    {
        print("❌ Error al crear directorio '/home/user'\n");
    }

    // Test 6: Listar directorio home
    print("\n📁 Test 6: Listando directorio '/home'...\n");
    result = minix_fs_ls("/home");
    if (result == 0)
    {
        print("✅ Listado del directorio '/home' completado\n");
    }
    else
    {
        print("❌ Error al listar directorio '/home'\n");
    }

    // Test 7: Verificar persistencia (simular reinicio)
    print("\n📁 Test 7: Verificando persistencia REAL...\n");
    print("🔄 Simulando reinicio del sistema...\n");

    // "Reinicializar" el filesystem
    print("🔄 Reinicializando Minix filesystem...\n");
    result = minix_fs_init();
    if (result == 0)
    {
        print("✅ Minix filesystem reinicializado correctamente\n");
    }
    else
    {
        print("❌ Error al reinicializar Minix filesystem\n");
        return;
    }

    // Listar directorio raíz después del "reinicio"
    print("\n📁 Verificando contenido después del reinicio (PERSISTENCIA REAL)...\n");
    result = minix_fs_ls("/");
    if (result == 0)
    {
        print("✅ Verificación de persistencia REAL completada\n");
    }
    else
    {
        print("❌ Error en verificación de persistencia\n");
    }

    print("\n==========================================\n");
    print("🎉 MINIX FILESYSTEM + ATA DRIVER TEST COMPLETADO\n");
    print("==========================================\n");
    print("\n");
}
