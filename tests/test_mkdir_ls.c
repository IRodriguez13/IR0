// tests/test_mkdir_ls.c - Test program for mkdir and ls system calls
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// System call numbers
#define SYS_EXIT 0
#define SYS_WRITE 3
#define SYS_MKDIR 21
#define SYS_LS 69

// File descriptors
#define STDOUT_FILENO 1

// Error codes
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define ENOMSG 41
#define EIDRM 42
#define ECHRNG 43
#define EL2NSYNC 44
#define EL3HLT 45
#define EL3RST 46
#define ELNRNG 47
#define EUNATCH 48
#define ENOCSI 49
#define EL2HLT 50
#define EBADE 51
#define EBADR 52
#define EXFULL 53
#define ENOANO 54
#define EBADRQC 55
#define EBADSLT 56
#define EDEADLOCK EDEADLK
#define EBFONT 59
#define ENOSTR 60
#define ENODATA 61
#define ETIME 62
#define ENOSR 63
#define ENONET 64
#define ENOPKG 65
#define EREMOTE 66
#define ENOLINK 67
#define EADV 68
#define ESRMNT 69
#define ECOMM 70
#define EPROTO 71
#define EMULTIHOP 72
#define EDOTDOT 73
#define EBADMSG 74
#define EOVERFLOW 75
#define ENOTUNIQ 76
#define EBADFD 77
#define EREMCHG 78
#define ELIBACC 79
#define ELIBBAD 80
#define ELIBSCN 81
#define ELIBMAX 82
#define ELIBEXEC 83
#define EILSEQ 84
#define ERESTART 85
#define ESTRPIPE 86
#define EUSERS 87
#define ENOTSOCK 88
#define EDESTADDRREQ 89
#define EMSGSIZE 90
#define EPROTOTYPE 91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define ESOCKTNOSUPPORT 94
#define EOPNOTSUPP 95
#define EPFNOSUPPORT 96
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define ENETDOWN 100
#define ENETUNREACH 101
#define ENETRESET 102
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ESHUTDOWN 108
#define ETOOMANYREFS 109
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#define EHOSTDOWN 112
#define EHOSTUNREACH 113
#define EALREADY 114
#define EINPROGRESS 115
#define ESTALE 116
#define EUCLEAN 117
#define ENOTNAM 118
#define ENAVAIL 119
#define EISNAM 120
#define EREMOTEIO 121
#define EDQUOT 122
#define ENOMEDIUM 123
#define EMEDIUMTYPE 124
#define ECANCELED 125
#define ENOKEY 126
#define EKEYEXPIRED 127
#define EKEYREVOKED 128
#define EKEYREJECTED 129
#define EOWNERDEAD 130
#define ENOTRECOVERABLE 131
#define ERFKILL 132
#define EHWPOISON 133

// System call structure
typedef struct {
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
} syscall_args_t;

// Function to make system calls
int64_t syscall(uint64_t number, syscall_args_t *args);

// Helper function to print strings
void print_string(const char *str) {
    syscall_args_t args;
    args.arg1 = STDOUT_FILENO;
    args.arg2 = (uint64_t)str;
    args.arg3 = 0;
    while (str[args.arg3]) args.arg3++; // Calculate string length
    syscall(SYS_WRITE, &args);
}

// Helper function to print newline
void print_newline() {
    print_string("\n");
}

// Helper function to print error message
void print_error(const char *message) {
    print_string("ERROR: ");
    print_string(message);
    print_newline();
}

// Helper function to print success message
void print_success(const char *message) {
    print_string("SUCCESS: ");
    print_string(message);
    print_newline();
}

// Test function for mkdir
void test_mkdir() {
    print_string("=== Testing mkdir system call ===");
    print_newline();
    
    // Test 1: Create a simple directory
    print_string("Test 1: Creating directory 'testdir'");
    print_newline();
    
    syscall_args_t args;
    args.arg1 = (uint64_t)"testdir";
    args.arg2 = 0755; // Mode
    args.arg3 = 0;
    
    int64_t result = syscall(SYS_MKDIR, &args);
    
    if (result == 0) {
        print_success("Directory 'testdir' created successfully");
    } else {
        print_error("Failed to create directory 'testdir'");
    }
    
    // Test 2: Create another directory
    print_string("Test 2: Creating directory 'docs'");
    print_newline();
    
    args.arg1 = (uint64_t)"docs";
    args.arg2 = 0755;
    args.arg3 = 0;
    
    result = syscall(SYS_MKDIR, &args);
    
    if (result == 0) {
        print_success("Directory 'docs' created successfully");
    } else {
        print_error("Failed to create directory 'docs'");
    }
    
    // Test 3: Try to create the same directory again (should fail)
    print_string("Test 3: Trying to create 'testdir' again (should fail)");
    print_newline();
    
    args.arg1 = (uint64_t)"testdir";
    args.arg2 = 0755;
    args.arg3 = 0;
    
    result = syscall(SYS_MKDIR, &args);
    
    if (result == 0) {
        print_error("Directory 'testdir' was created again (unexpected)");
    } else {
        print_success("Directory 'testdir' already exists (expected behavior)");
    }
    
    print_newline();
}

// Test function for ls
void test_ls() {
    print_string("=== Testing ls system call ===");
    print_newline();
    
    // Test 1: List root directory
    print_string("Test 1: Listing root directory '/'");
    print_newline();
    
    syscall_args_t args;
    args.arg1 = (uint64_t)"/";
    args.arg2 = 0;
    args.arg3 = 0;
    
    int64_t result = syscall(SYS_LS, &args);
    
    if (result == 0) {
        print_success("Root directory listed successfully");
    } else {
        print_error("Failed to list root directory");
    }
    
    // Test 2: List a non-existent directory
    print_string("Test 2: Listing non-existent directory 'nonexistent'");
    print_newline();
    
    args.arg1 = (uint64_t)"nonexistent";
    args.arg2 = 0;
    args.arg3 = 0;
    
    result = syscall(SYS_LS, &args);
    
    if (result == 0) {
        print_error("Non-existent directory was listed (unexpected)");
    } else {
        print_success("Non-existent directory correctly not found");
    }
    
    print_newline();
}

// Main test function
void test_mkdir_ls() {
    print_string("Starting mkdir and ls system call tests");
    print_newline();
    print_newline();
    
    // Test mkdir functionality
    test_mkdir();
    
    // Test ls functionality
    test_ls();
    
    print_string("All tests completed");
    print_newline();
}

// Entry point
void _start() {
    test_mkdir_ls();
    
    // Exit with success
    syscall_args_t args;
    args.arg1 = 0; // Exit code 0
    args.arg2 = 0;
    args.arg3 = 0;
    syscall(SYS_EXIT, &args);
}
