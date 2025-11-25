// libctest.c - Test program for new libc functions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_strtok() {
    printf("Testing strtok...\n");
    char str[] = "Hello,World,IR0";
    const char *delim = ",";
    char *token;

    token = strtok(str, delim);
    while (token != NULL) {
        printf("Token: %s\n", token);
        token = strtok(NULL, delim);
    }
}

void test_atoi() {
    printf("Testing atoi...\n");
    const char *s1 = "123";
    const char *s2 = "-456";
    const char *s3 = "  789";
    
    printf("atoi('%s') = %d\n", s1, atoi(s1));
    printf("atoi('%s') = %d\n", s2, atoi(s2));
    printf("atoi('%s') = %d\n", s3, atoi(s3));
}

void test_strtod() {
    printf("Testing strtod...\n");
    const char *s1 = "123.456";
    const char *s2 = "-78.90";
    const char *s3 = "1.23e2";
    char *endptr;

    // Note: printf %f might not be fully supported if I didn't verify it, 
    // but let's try. If not, we can print parts.
    // Looking at stdio.c, printf only supports %d, %s, %c, %.
    // So I can't print doubles with printf yet!
    // I should probably implement a simple print_double or just cast to int for verification.
    
    double d1 = strtod(s1, &endptr);
    double d2 = strtod(s2, &endptr);
    double d3 = strtod(s3, &endptr);

    printf("strtod('%s') = %d (approx)\n", s1, (int)d1);
    printf("strtod('%s') = %d (approx)\n", s2, (int)d2);
    printf("strtod('%s') = %d (approx)\n", s3, (int)d3);
}

void test_malloc_free() {
    printf("Testing malloc/free...\n");
    
    // Test 1: Simple allocation
    int *p1 = (int*)malloc(sizeof(int) * 10);
    if (!p1) {
        printf("malloc failed\n");
        return;
    }
    for (int i = 0; i < 10; i++) p1[i] = i;
    printf("p1[5] = %d\n", p1[5]);

    // Test 2: Another allocation
    char *p2 = (char*)malloc(100);
    if (!p2) {
        printf("malloc p2 failed\n");
        return;
    }
    strcpy(p2, "Hello Malloc");
    printf("p2: %s\n", p2);

    // Test 3: Free and reuse
    free(p1);
    
    int *p3 = (int*)malloc(sizeof(int) * 5); // Should fit in p1's old spot
    if (!p3) {
        printf("malloc p3 failed\n");
        return;
    }
    p3[0] = 999;
    printf("p3[0] = %d\n", p3[0]);
    
    // Check if p3 is same address as p1 (reuse)
    if (p3 == p1) {
        printf("Memory reused successfully!\n");
    } else {
        printf("Memory not reused (might be split or new block)\n");
    }

    free(p2);
    free(p3);
    printf("Malloc/Free tests done\n");
}

int main() {
    printf("Starting libc tests\n");
    
    test_strtok();
    test_atoi();
    test_strtod();
    test_malloc_free();

    printf("Tests finished\n");
    return 0;
}
