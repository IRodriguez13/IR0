#include <stdint.h>
#include <stddef.h>
#include "../../arch/common/common_paging.h"
#include "Paging_x86-32.h"

extern void idt_flush();

// Tamaño std para directiorio/tablas en 32-bit paging

#define PAGE_ENTRIES 1024                           // esto es 2 a la 10 bits por cada nivel, ya que el nvl1 procesa 10 bits, el segundo tambien y el tercero contiene el offset de los 12 restantes
#define PAGE_SIZE 4096                              // 4kb que es lo que suele medir cada tabla (porque son páginas también)
#define TABLE_RANGE_SIZE (PAGE_ENTRIES * PAGE_SIZE) // 4MB por tabla

// Alineo a 4 kb por necesidad de la cpu. Estos son mis dos niveles de paginación en caada tabla que arme.

__attribute__((aligned(PAGE_SIZE))) uint32_t page_directory[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t first_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t second_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t third_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fouth_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Ten_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Eleven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twelve_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fourteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Sixteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Seventeen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Eighteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Nineteen_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_one_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_two_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_three_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_four_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Twenty_nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_one_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_two_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_three_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_four_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Thirty_nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_one_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_two_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_three_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_four_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Forty_nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_one_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_two_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_three_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_four_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fifty_nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Sixty_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Sixty_one_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Sixty_two_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Sixty_three_page_table[PAGE_ENTRIES]; // 256MB de paginación

void Fill_Table_Page(uint32_t Directory_index, uint32_t *table, uint32_t start_adress)
{
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        table[i] = (start_adress + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    // Ahora le digo al directorio de páginas que esta tabla es usable
    page_directory[Directory_index] = ((uint32_t)table) | PAGE_PRESENT | PAGE_WRITE;
}

void Clean_Remaining_Tables(uint32_t dir_index)
{
    // La CPU va a seguir buscando los 1023 tablas restantes que no están creadas asi que las tengo que inicialiozar en 0x00
    for (uint32_t i = dir_index; i < PAGE_ENTRIES; i++)
    {
        page_directory[i] = 0x00; // Convierto en NULL las próximas 1018 páginas. O las que falten dependiendo de la cantidad de tablas que tenga.
    }
}

void init_paging_x86()
{
    // Mapear 32 tablas de 4MB cada una = 128MB total
    Fill_Table_Page(0, first_page_table, 0 * TABLE_RANGE_SIZE);
    Fill_Table_Page(1, second_page_table, 1 * TABLE_RANGE_SIZE);
    Fill_Table_Page(2, third_page_table, 2 * TABLE_RANGE_SIZE);
    Fill_Table_Page(3, Fouth_page_table, 3 * TABLE_RANGE_SIZE);
    Fill_Table_Page(4, Five_page_table, 4 * TABLE_RANGE_SIZE);
    Fill_Table_Page(5, Six_page_table, 5 * TABLE_RANGE_SIZE);
    Fill_Table_Page(6, Seven_page_table, 6 * TABLE_RANGE_SIZE);
    Fill_Table_Page(7, Eight_page_table, 7 * TABLE_RANGE_SIZE);
    Fill_Table_Page(8, Nine_page_table, 8 * TABLE_RANGE_SIZE);
    Fill_Table_Page(9, Ten_page_table, 9 * TABLE_RANGE_SIZE);
    Fill_Table_Page(10, Eleven_page_table, 10 * TABLE_RANGE_SIZE);
    Fill_Table_Page(11, Twelve_page_table, 11 * TABLE_RANGE_SIZE);
    Fill_Table_Page(12, Thirteen_page_table, 12 * TABLE_RANGE_SIZE);
    Fill_Table_Page(13, Fourteen_page_table, 13 * TABLE_RANGE_SIZE);
    Fill_Table_Page(14, Fifteen_page_table, 14 * TABLE_RANGE_SIZE);
    Fill_Table_Page(15, Sixteen_page_table, 15 * TABLE_RANGE_SIZE);
    Fill_Table_Page(16, Seventeen_page_table, 16 * TABLE_RANGE_SIZE);
    Fill_Table_Page(17, Eighteen_page_table, 17 * TABLE_RANGE_SIZE);
    Fill_Table_Page(18, Nineteen_page_table, 18 * TABLE_RANGE_SIZE);
    Fill_Table_Page(19, Twenty_page_table, 19 * TABLE_RANGE_SIZE);
    Fill_Table_Page(20, Twenty_one_page_table, 20 * TABLE_RANGE_SIZE);
    Fill_Table_Page(21, Twenty_two_page_table, 21 * TABLE_RANGE_SIZE);
    Fill_Table_Page(22, Twenty_three_page_table, 22 * TABLE_RANGE_SIZE);
    Fill_Table_Page(23, Twenty_four_page_table, 23 * TABLE_RANGE_SIZE);
    Fill_Table_Page(24, Twenty_five_page_table, 24 * TABLE_RANGE_SIZE);
    Fill_Table_Page(25, Twenty_six_page_table, 25 * TABLE_RANGE_SIZE);
    Fill_Table_Page(26, Twenty_seven_page_table, 26 * TABLE_RANGE_SIZE);
    Fill_Table_Page(27, Twenty_eight_page_table, 27 * TABLE_RANGE_SIZE);
    Fill_Table_Page(28, Twenty_nine_page_table, 28 * TABLE_RANGE_SIZE);
    Fill_Table_Page(29, Thirty_page_table, 29 * TABLE_RANGE_SIZE);
    Fill_Table_Page(30, Thirty_one_page_table, 30 * TABLE_RANGE_SIZE);
    Fill_Table_Page(31, Thirty_two_page_table, 31 * TABLE_RANGE_SIZE);
    Fill_Table_Page(32, Thirty_three_page_table, 32 * TABLE_RANGE_SIZE);
    Fill_Table_Page(33, Thirty_four_page_table, 33 * TABLE_RANGE_SIZE);
    Fill_Table_Page(34, Thirty_five_page_table, 34 * TABLE_RANGE_SIZE);
    Fill_Table_Page(35, Thirty_six_page_table, 35 * TABLE_RANGE_SIZE);
    Fill_Table_Page(36, Thirty_seven_page_table, 36 * TABLE_RANGE_SIZE);
    Fill_Table_Page(37, Thirty_eight_page_table, 37 * TABLE_RANGE_SIZE);
    Fill_Table_Page(38, Thirty_nine_page_table, 38 * TABLE_RANGE_SIZE);
    Fill_Table_Page(39, Forty_page_table, 39 * TABLE_RANGE_SIZE);
    Fill_Table_Page(40, Forty_one_page_table, 40 * TABLE_RANGE_SIZE);
    Fill_Table_Page(41, Forty_two_page_table, 41 * TABLE_RANGE_SIZE);
    Fill_Table_Page(42, Forty_three_page_table, 42 * TABLE_RANGE_SIZE);
    Fill_Table_Page(43, Forty_four_page_table, 43 * TABLE_RANGE_SIZE);
    Fill_Table_Page(44, Forty_five_page_table, 44 * TABLE_RANGE_SIZE);
    Fill_Table_Page(45, Forty_six_page_table, 45 * TABLE_RANGE_SIZE);
    Fill_Table_Page(46, Forty_seven_page_table, 46 * TABLE_RANGE_SIZE);
    Fill_Table_Page(47, Forty_eight_page_table, 47 * TABLE_RANGE_SIZE);
    Fill_Table_Page(48, Forty_nine_page_table, 48 * TABLE_RANGE_SIZE);
    Fill_Table_Page(49, Fifty_page_table, 49 * TABLE_RANGE_SIZE);
    Fill_Table_Page(50, Fifty_one_page_table, 50 * TABLE_RANGE_SIZE);
    Fill_Table_Page(51, Fifty_two_page_table, 51 * TABLE_RANGE_SIZE);
    Fill_Table_Page(52, Fifty_three_page_table, 52 * TABLE_RANGE_SIZE);
    Fill_Table_Page(53, Fifty_four_page_table, 53 * TABLE_RANGE_SIZE);
    Fill_Table_Page(54, Fifty_five_page_table, 54 * TABLE_RANGE_SIZE);
    Fill_Table_Page(55, Fifty_six_page_table, 55 * TABLE_RANGE_SIZE);
    Fill_Table_Page(56, Fifty_seven_page_table, 56 * TABLE_RANGE_SIZE);
    Fill_Table_Page(57, Fifty_eight_page_table, 57 * TABLE_RANGE_SIZE);
    Fill_Table_Page(58, Fifty_nine_page_table, 58 * TABLE_RANGE_SIZE);
    Fill_Table_Page(59, Sixty_page_table, 59 * TABLE_RANGE_SIZE);
    Fill_Table_Page(60, Sixty_one_page_table, 60 * TABLE_RANGE_SIZE);
    Fill_Table_Page(61, Sixty_two_page_table, 61 * TABLE_RANGE_SIZE);
    Fill_Table_Page(62, Sixty_three_page_table, 62 * TABLE_RANGE_SIZE);

    Clean_Remaining_Tables(63);

    paging_set_cpu((uint32_t) page_directory);
}




