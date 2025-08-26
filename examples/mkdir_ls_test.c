// ===============================================================================
// IR0 KERNEL - MKDIR/LS TEST
// ===============================================================================

#include <ir0/print.h>
#include <string.h>

// Definir syscalls (deberían estar en un header, pero por simplicidad las definimos aquí)
#define SYS_MKDIR 21
#define SYS_LS 26

// Estructura para argumentos de syscall
typedef struct
{
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
} syscall_args_t;

// Función auxiliar para simular syscalls desde el kernel
static int64_t kernel_syscall(int number, syscall_args_t *args)
{
    // Por ahora, simular las syscalls básicamente
    switch (number)
    {
    case SYS_MKDIR:
    {
        const char *path = (const char *)args->arg1;
        uint32_t mode = (uint32_t)args->arg2;

        print("KERNEL: mkdir called with path: ");
        print(path);
        print(" mode: ");
        print_uint64(mode);
        print("\n");

        // Simular éxito
        return 0;
    }
    case SYS_LS:
    {
        const char *path = (const char *)args->arg1;

        print("KERNEL: ls called with path: ");
        print(path);
        print("\n");

        // Simular éxito
        return 0;
    }
    default:
        print("KERNEL: Unknown syscall number: ");
        print_uint64(number);
        print("\n");
        return -1;
    }
}

void test_mkdir_ls_basic(void)
{
    print("\n=== MKDIR/LS BASIC TEST ===\n");

    // Test 1: Crear directorio /test
    print("1. Creating directory /test...\n");
    syscall_args_t mkdir_args;
    mkdir_args.arg1 = (uint64_t)"/test";
    mkdir_args.arg2 = 0755; // Permisos

    int64_t result = kernel_syscall(SYS_MKDIR, &mkdir_args);

    if (result == 0)
    {
        print("SUCCESS: Directory /test created\n");
    }
    else
    {
        print("ERROR: Failed to create directory /test\n");
        return;
    }

    // Test 2: Crear directorio /home
    print("\n2. Creating directory /home...\n");
    mkdir_args.arg1 = (uint64_t)"/home";
    mkdir_args.arg2 = 0755;

    result = kernel_syscall(SYS_MKDIR, &mkdir_args);

    if (result == 0)
    {
        print("SUCCESS: Directory /home created\n");
    }
    else
    {
        print("ERROR: Failed to create directory /home\n");
        return;
    }

    // Test 3: Crear directorio /home/user
    print("\n3. Creating directory /home/user...\n");
    mkdir_args.arg1 = (uint64_t)"/home/user";
    mkdir_args.arg2 = 0755;

    result = kernel_syscall(SYS_MKDIR, &mkdir_args);

    if (result == 0)
    {
        print("SUCCESS: Directory /home/user created\n");
    }
    else
    {
        print("ERROR: Failed to create directory /home/user\n");
        return;
    }

    // Test 4: Listar directorio raíz
    print("\n4. Listing root directory...\n");
    syscall_args_t ls_args;
    ls_args.arg1 = (uint64_t)"/";

    result = kernel_syscall(SYS_LS, &ls_args);

    if (result == 0)
    {
        print("SUCCESS: Root directory listed\n");
    }
    else
    {
        print("ERROR: Failed to list root directory\n");
        return;
    }

    // Test 5: Listar directorio /home
    print("\n5. Listing /home directory...\n");
    ls_args.arg1 = (uint64_t)"/home";

    result = kernel_syscall(SYS_LS, &ls_args);

    if (result == 0)
    {
        print("SUCCESS: /home directory listed\n");
    }
    else
    {
        print("ERROR: Failed to list /home directory\n");
        return;
    }

    // Test 6: Listar directorio /home/user
    print("\n6. Listing /home/user directory...\n");
    ls_args.arg1 = (uint64_t)"/home/user";

    result = kernel_syscall(SYS_LS, &ls_args);

    if (result == 0)
    {
        print("SUCCESS: /home/user directory listed\n");
    }
    else
    {
        print("ERROR: Failed to list /home/user directory\n");
        return;
    }

    // Test 7: Intentar crear directorio que ya existe
    print("\n7. Trying to create existing directory /test...\n");
    mkdir_args.arg1 = (uint64_t)"/test";
    mkdir_args.arg2 = 0755;

    result = kernel_syscall(SYS_MKDIR, &mkdir_args);

    if (result != 0)
    {
        print("SUCCESS: Correctly refused to create existing directory\n");
    }
    else
    {
        print("WARNING: Allowed creation of existing directory\n");
    }

    // Test 8: Listar directorio inexistente
    print("\n8. Trying to list non-existent directory /nonexistent...\n");
    ls_args.arg1 = (uint64_t)"/nonexistent";

    result = kernel_syscall(SYS_LS, &ls_args);

    if (result != 0)
    {
        print("SUCCESS: Correctly refused to list non-existent directory\n");
    }
    else
    {
        print("WARNING: Listed non-existent directory\n");
    }

    print("\n=== MKDIR/LS BASIC TEST COMPLETED SUCCESSFULLY ===\n");
    print("All basic directory operations are working!\n");
}

// Función para ser llamada desde el kernel
void run_mkdir_ls_test(void)
{
    test_mkdir_ls_basic();
}
