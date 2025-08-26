// ===============================================================================
// IR0 KERNEL - FILESYSTEM REAL TEST
// ===============================================================================

#include <ir0/print.h>
#include <string.h>

// Declaraciones externas
extern int vfs_simple_mkdir(const char *path);
extern int vfs_simple_ls(const char *path);
extern int vfs_directory_exists(const char *pathname);

void test_filesystem_real(void)
{
    print("\n=== FILESYSTEM REAL TEST ===\n");

    // Test 1: Crear directorio /test
    print("1. Creating directory /test...\n");
    int result = vfs_simple_mkdir("/test");

    if (result == 0)
    {
        print("SUCCESS: Directory /test created\n");
    }
    else
    {
        print("ERROR: Failed to create /test\n");
        return;
    }

    // Test 2: Verificar que existe
    print("\n2. Checking if /test exists...\n");
    if (vfs_directory_exists("/test"))
    {
        print("SUCCESS: /test directory exists\n");
    }
    else
    {
        print("ERROR: /test directory not found\n");
        return;
    }

    // Test 3: Crear directorio /home
    print("\n3. Creating directory /home...\n");
    result = vfs_simple_mkdir("/home");

    if (result == 0)
    {
        print("SUCCESS: Directory /home created\n");
    }
    else
    {
        print("ERROR: Failed to create /home\n");
        return;
    }

    // Test 4: Listar directorio raíz
    print("\n4. Listing root directory...\n");
    result = vfs_simple_ls("/");

    if (result == 0)
    {
        print("SUCCESS: Root directory listed\n");
    }
    else
    {
        print("ERROR: Failed to list root directory\n");
        return;
    }

    // Test 5: Intentar crear directorio que ya existe
    print("\n5. Trying to create existing directory /test...\n");
    result = vfs_simple_mkdir("/test");

    if (result != 0)
    {
        print("SUCCESS: Correctly refused to create existing directory\n");
    }
    else
    {
        print("ERROR: Allowed creation of existing directory\n");
        return;
    }

    // Test 6: Verificar que ambos directorios existen
    print("\n6. Verifying both directories exist...\n");
    if (vfs_directory_exists("/test") && vfs_directory_exists("/home"))
    {
        print("SUCCESS: Both directories exist\n");
    }
    else
    {
        print("ERROR: One or both directories missing\n");
        return;
    }

    print("\n=== FILESYSTEM REAL TEST COMPLETED SUCCESSFULLY ===\n");
    print("🎉 The filesystem is working for real!\n");
    print("📁 Directories are being created and stored in memory\n");
    print("🔍 Directory existence is being verified\n");
    print("📋 Directory listing is working\n");
    print("🚫 Duplicate prevention is working\n");
}

// Función para ser llamada desde el kernel
void run_fs_test(void)
{
    test_filesystem_real();
}
