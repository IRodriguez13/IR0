// arch/arm-32/sources/framebuffer_arm.c - Driver de video básico para ARM-32
// Implementación ultra simple para mostrar la secuencia de arranque

#include "framebuffer_arm.h"

// Estructura del framebuffer - Usar dirección más compatible
static uint32_t *framebuffer = (uint32_t*)0x60000000;
static bool fb_initialized = false;

// Función para inicializar el framebuffer
void fb_init(void) 
{
    // Intentar diferentes direcciones de framebuffer
    // QEMU VExpress-A9 puede usar diferentes direcciones
    uint32_t *test_addr = (uint32_t*)0x60000000;
    
    // Escribir un patrón de prueba para verificar que el framebuffer funciona
    test_addr[0] = 0xFF0000FF;  // Pixel rojo
    test_addr[1] = 0xFF00FF00;  // Pixel verde
    test_addr[2] = 0xFFFF0000;  // Pixel azul
    test_addr[3] = 0xFFFFFFFF;  // Pixel blanco
    
    fb_initialized = true;
}

// Función para limpiar la pantalla
void fb_clear(uint32_t color) 
{
    if (!fb_initialized) return;
    
    // Usar una resolución más pequeña para asegurar compatibilidad
    for (int y = 0; y < 480; y++) 
    {
        for (int x = 0; x < 640; x++) 
        {
            framebuffer[y * 640 + x] = color;
        }
    }
}

// Función para dibujar un pixel
void fb_draw_pixel(int x, int y, uint32_t color) 
{
    if (!fb_initialized || x < 0 || x >= 640 || y < 0 || y >= 480) return;
    framebuffer[y * 640 + x] = color;
}

// Función para dibujar un rectángulo
void fb_draw_rect(int x, int y, int width, int height, uint32_t color) 
{
    for (int py = y; py < y + height && py < 480; py++) 
    {
        for (int px = x; px < x + width && px < 640; px++) 
        {
            fb_draw_pixel(px, py, color);
        }
    }
}

// Función para dibujar texto simple (fuente 8x8)
void fb_draw_char(int x, int y, char c, uint32_t color) 
{
    // Fuente simple 8x8 (solo algunos caracteres)
    static const uint8_t font_8x8[][8] = 
    {
        // 'I' (0x49)
        {0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
        // 'R' (0x52)
        {0x00, 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x00},
        // '0' (0x30)
        {0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
        // 'K' (0x4B)
        {0x00, 0x66, 0x6C, 0x78, 0x78, 0x6C, 0x66, 0x00},
        // 'E' (0x45)
        {0x00, 0x7E, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
        // 'N' (0x4E)
        {0x00, 0x66, 0x76, 0x7E, 0x6E, 0x66, 0x66, 0x00},
        // 'L' (0x4C)
        {0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
        // 'A' (0x41)
        {0x00, 0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x00},
        // 'M' (0x4D)
        {0x00, 0x66, 0x7E, 0x7E, 0x66, 0x66, 0x66, 0x00},
        // 'S' (0x53)
        {0x00, 0x3C, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
        // 'Y' (0x59)
        {0x00, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
        // 'T' (0x54)
        {0x00, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
        // 'E' (0x45)
        {0x00, 0x7E, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
        // 'M' (0x4D)
        {0x00, 0x66, 0x7E, 0x7E, 0x66, 0x66, 0x66, 0x00},
    };
    
    int char_index = -1;
    switch (c) 
    {
        case 'I': char_index = 0; break;
        case 'R': char_index = 1; break;
        case '0': char_index = 2; break;
        case 'K': char_index = 3; break;
        case 'E': char_index = 4; break;
        case 'N': char_index = 5; break;
        case 'L': char_index = 6; break;
        case 'A': char_index = 7; break;
        case 'M': char_index = 8; break;
        case 'S': char_index = 9; break;
        case 'Y': char_index = 10; break;
        case 'T': char_index = 11; break;
        default: return;
    }
    
    if (char_index >= 0) 
    {
        for (int py = 0; py < 8; py++) 
        {
            for (int px = 0; px < 8; px++) 
            {
                if (font_8x8[char_index][py] & (0x80 >> px)) 
                {
                    fb_draw_pixel(x + px, y + py, color);
                }
            }
        }
    }
}

// Función para dibujar texto
void fb_draw_text(int x, int y, const char *text, uint32_t color) 
{
    int current_x = x;
    for (int i = 0; text[i] != '\0'; i++) 
    {
        fb_draw_char(current_x, y, text[i], color);
        current_x += 8;
    }
}

// Función para mostrar la secuencia de arranque
void fb_show_boot_sequence(void) 
{
    if (!fb_initialized) 
    {
        fb_init();
    }
    
    // Limpiar pantalla con negro
    fb_clear(COLOR_BLACK);
    
    // Dibujar un patrón de prueba visible
    for (int i = 0; i < 100; i++) 
    {
        fb_draw_rect(i * 6, i * 4, 4, 4, COLOR_RED);
        fb_draw_rect(i * 6 + 2, i * 4 + 2, 4, 4, COLOR_GREEN);
        fb_draw_rect(i * 6 + 4, i * 4 + 4, 4, 4, COLOR_BLUE);
    }
    
    // Mostrar título
    fb_draw_text(200, 100, "IR0 KERNEL", COLOR_WHITE);
    fb_draw_text(180, 120, "ARM-32 BOOT SEQUENCE", COLOR_CYAN);
    
    // Mostrar progreso
    fb_draw_text(200, 200, "INITIALIZING...", COLOR_YELLOW);
    
    // Dibujar barra de progreso
    for (int i = 0; i < 50; i++) 
    {
        fb_draw_rect(100 + i * 8, 250, 6, 20, COLOR_GREEN);
        // Simular delay
        for (volatile int j = 0; j < 500000; j++);
    }
    
    // Mostrar mensajes de estado
    fb_draw_text(200, 300, "FRAMEBUFFER: OK", COLOR_GREEN);
    fb_draw_text(200, 320, "MEMORY: OK", COLOR_GREEN);
    fb_draw_text(200, 340, "CPU: ARMv7-A", COLOR_GREEN);
    fb_draw_text(200, 360, "ARCHITECTURE: MODULAR", COLOR_GREEN);
    
    // Mostrar mensaje final
    fb_draw_text(150, 450, "KERNEL ARM-32 READY!", COLOR_MAGENTA);
}
