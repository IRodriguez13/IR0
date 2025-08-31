// test_user_mode.c - Test file for user mode functionality
#include <ir0/print.h>
#include <process/process.h>

// Simple test to verify we can compile and link the user mode functionality
void test_user_mode_compilation(void)
{
    print("Testing user mode compilation...\n");
    
    // Test that we can call process_exec
    char *test_argv[] = {"test", NULL};
    char *test_envp[] = {NULL};
    
    print("Calling process_exec...\n");
    
    // This should work if our implementation is correct
    int result = process_exec("test_program", test_argv, test_envp);
    
    print("process_exec returned: ");
    print_int32(result);
    print("\n");
    
    print("User mode compilation test completed.\n");
}

// Test function that can be called from the shell
void test_user_mode_from_shell(void)
{
    print("=== USER MODE TEST ===\n");
    print("This test will attempt to:\n");
    print("1. Create a user process\n");
    print("2. Switch to user mode (ring 3)\n");
    print("3. Execute code in user mode\n");
    print("4. Return to kernel mode\n");
    print("======================\n");
    
    test_user_mode_compilation();
}
