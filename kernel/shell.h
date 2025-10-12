#pragma once
#include <print.h>

void shell_entry(void);
void fb_print(const char *str, uint8_t color);
void fb_putchar(char c, uint8_t color);
void process_command(const char *cmd);
void shell_init(void);
const char *find_arg(const char *cmd, const char *command);
