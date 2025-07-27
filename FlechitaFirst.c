#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>

#define MBR_PARTITION_TABLE_OFFSET 0x1BE // Donde empieza la tabla de particiones (4 entradas x 16 bytes)
#define MBR_SIGNATURE_OFFSET       0x1FE // Donde está la firma 0x55AA

#define PART_TYPE_OFFSET           0x04
#define PART_START_LBA_OFFSET      0x08
#define PART_NUM_SECTORS_OFFSET    0x0C
#define PART_START_CHS_OFFSET      0x01
#define PART_END_CHS_OFFSET        0x05

int fd;

char *mapFile(char *filePath) {
    /* Abre archivo */
    fd = open(filePath, O_RDONLY);
    if (fd == -1) {
        perror("Error abriendo el archivo");
        return(NULL);
    }

    /* Mapea archivo */
    struct stat st;
    fstat(fd,&st);
    long fs = st.st_size;

    char *map = mmap(0, fs, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        perror("Error mapeando el archivo");
        return(NULL);
    }

    return map;
}


int leeChar() {

    int chars[5];
    int ch, i = 0;
    nodelay(stdscr, TRUE);
    while ((ch = getch()) == ERR); /* Espera activa */
    ungetch(ch);
    while ((ch = getch()) != ERR) {
        chars[i++] = ch;
    }

/* convierte a numero con todo lo leido */

    int res = 0;
    for (int j = 0; j < i; j++) {
        res <<= 8;
        res |= chars[j];

    }

return res;

}

const char* get_partition_entry_ptr(unsigned char *mbr_data, int index) {
    return mbr_data + MBR_PARTITION_TABLE_OFFSET + (index * 16);
}

void mostrar_particiones(char *map) {
    clear();
    int selected_partition_index = 0;
    // Cabecera ajustada para mayor claridad y consistencia
    mvprintw(4, 0, "Particion | Inicio CHS (C|H|S) | Fin CHS (C|H|S) | Tipo | LBA Inicio | Tamano (Sectores)");
    for (int i = 0; i < 4; i++) {
        unsigned char *p_entry = (unsigned char *)map + MBR_PARTITION_TABLE_OFFSET + (i * 16);
        // Validar si la entrada de la partición es válida (por ejemplo, si el tipo de partición no es 0)
        // O basarse en num_sectors como ya lo haces.
        int num_sectors = *(unsigned int *)&p_entry[PART_NUM_SECTORS_OFFSET]; // Usar el offset correcto

        if (num_sectors == 0) {
            mvprintw(5 + i, 0, "Particion %d: VACIA", i + 1);
        } else {
            if (i == selected_partition_index) {
                attron(A_REVERSE);
            }
            // Lectura de CHS de inicio
            unsigned char start_chs_head = p_entry[PART_START_CHS_OFFSET];
            unsigned char start_chs_sector_cyl_high = p_entry[PART_START_CHS_OFFSET + 1];
            unsigned char start_chs_cyl_low = p_entry[PART_START_CHS_OFFSET + 2];

            unsigned int start_head = start_chs_head;
            unsigned int start_sector = start_chs_sector_cyl_high & 0x3F; // Los 6 bits menos significativos son el sector
            unsigned int start_cyl = (unsigned int)start_chs_cyl_low + ((start_chs_sector_cyl_high & 0xC0) << 2);

            // Lectura de CHS de fin
            unsigned char end_chs_head = p_entry[PART_END_CHS_OFFSET];
            unsigned char end_chs_sector_cyl_high = p_entry[PART_END_CHS_OFFSET + 1];
            unsigned char end_chs_cyl_low = p_entry[PART_END_CHS_OFFSET + 2];

            unsigned int end_head = end_chs_head;
            unsigned int end_sector = end_chs_sector_cyl_high & 0x3F;
            unsigned int end_cyl = (unsigned int)end_chs_cyl_low + ((end_chs_sector_cyl_high & 0xC0) << 2);
            
            // LBA de inicio y número de sectores se leen directamente
            unsigned int start_lba = *(unsigned int *)&p_entry[PART_START_LBA_OFFSET];

            mvprintw(5 + i, 0, "Particion %d: C:%-4u H:%-4u S:%-4u | C:%-4u H:%-4u S:%-4u | 0x%-2X | %-11u | %-17u",
                     i + 1,
                     start_cyl, start_head, start_sector,
                     end_cyl, end_head, end_sector,
                     p_entry[PART_TYPE_OFFSET], // Tipo de partición
                     start_lba,
                     num_sectors);

            attroff(A_REVERSE);
        }
    }
    mvprintw(10, 0, "Presiona 'q' para salir.");
    mvprintw(11, 0, "Usa ARRIBA/ABAJO para seleccionar particion. Presiona ENTER para ver detalles.");
    refresh();
}


int main(int argc, char const *argv[])
{
    if(argc != 2){
        printf("se usa %s \n", argv[0]);
        return (-1);
    }
    char *map = mapFile((char *)argv[1]);
    if (map == NULL) {
        return -1; // Error al mapear el archivo
    }
    int c;
    initscr();
    raw();
    noecho(); /* No muestres el caracter leido */
    keypad(stdscr, TRUE);
    mostrar_particiones(map);


    char *lista[] = {"Uno", "Dos", "Tres", "Cuatro", "Cinco"};
    int num = 5;
    int i = 0; // Indice de la lista

// cbreak(); /* Haz que los caracteres se le pasen al usuario */

    return 0;

} 