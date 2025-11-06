// hello.c - Simple test program for IR0 ELF loader
// This program will test basic ELF loading and execution

// Minimal syscall interface
static inline long syscall1(long number, long arg1) 
{
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg1)
                   : "memory");
  return result;
}

static inline long syscall3(long number, long arg1, long arg2, long arg3) {
  long result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                   : "memory");
  return result;
}

// Syscall numbers (must match kernel)
#define SYS_EXIT 0
#define SYS_WRITE 1

// Simple write function
void write_string(const char *str) {
  int len = 0;
  while (str[len])
    len++;                                // Calculate length
  syscall3(SYS_WRITE, 1, (long)str, len); // fd=1 (stdout)
}

// Program entry point
void _start(void) {
  write_string("Hello from userspace ELF program!\n");
  write_string("ELF loader is working!\n");
  write_string("This is running in Ring 3 user mode.\n");

  // Exit with code 42
  syscall1(SYS_EXIT, 42);

  // Should never reach here
  while (1)
    ;
}