#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>   // Para open(), read(), close()
#include <ncurses.h> // Para la interfaz de usuario
#include <errno.h>   // Para strerror
#include <string.h>  // Para memcpy, memset

// Tamaño del sector MBR
#define MBR_SIZE 512

// --- Offsets de los campos del MBR ---
#define MBR_PARTITION_TABLE_OFFSET 0x1BE // Donde empieza la tabla de particiones (4 entradas x 16 bytes)
#define MBR_SIGNATURE_OFFSET       0x1FE // Donde está la firma 0x55AA

// --- Offsets dentro de cada entrada de partición (16 bytes) ---
#define PART_BOOT_INDICATOR_OFFSET 0x00
#define PART_START_CHS_OFFSET      0x01
#define PART_TYPE_OFFSET           0x04
#define PART_END_CHS_OFFSET        0x05
#define PART_START_LBA_OFFSET      0x08
#define PART_NUM_SECTORS_OFFSET    0x0C

// --- Funciones de Ncurses ---
int get_key_input() {
    int ch;
    ch = getch();
    return ch;
}

// Función para extraer un unsigned int de 4 bytes en little-endian
unsigned int get_little_endian_uint(const unsigned char *buffer, int offset) {
    return (unsigned int)buffer[offset] |
           ((unsigned int)buffer[offset + 1] << 8) |
           ((unsigned int)buffer[offset + 2] << 16) |
           ((unsigned int)buffer[offset + 3] << 24);
}

// Función para leer el MBR de la imagen
// Ahora mbr_data es un puntero a unsigned char (array de bytes)
int read_mbr(const char *image_path, unsigned char *mbr_data) {
    int fd;
    ssize_t bytes_read;

    fd = open(image_path, O_RDONLY);
    if (fd == -1) {
        return -1; // Error al abrir
    }

    bytes_read = read(fd, mbr_data, MBR_SIZE);
    if (bytes_read == -1) {
        close(fd);
        return -1; // Error al leer
    }
    if (bytes_read != MBR_SIZE) {
        close(fd);
        return -1; // Lectura incompleta
    }

    close(fd);

    // Verificar la firma del MBR (0xAA55)
    // Se lee como un short en little-endian desde el offset MBR_SIGNATURE_OFFSET
    unsigned short mbr_signature = (unsigned short)mbr_data[MBR_SIGNATURE_OFFSET] |
                                   ((unsigned short)mbr_data[MBR_SIGNATURE_OFFSET + 1] << 8);

    if (mbr_signature != 0xAA55) {
        return -1; // Firma inválida
    }

    return 0; // Éxito
}

const unsigned char* get_partition_entry_ptr(const unsigned char *mbr_data, int index) {
    return mbr_data + MBR_PARTITION_TABLE_OFFSET + (index * 16);
}

// Función para mostrar la información del MBR y la tabla de particiones
void display_mbr_and_partitions(const unsigned char *mbr_data, int selected_index) {
    clear();



    mvprintw(6, 0, "Particion | H1   | S1 | CF      | HF | SF | Tamano (Sectores)");
    
    for (int i = 0; i < 4; i++) {
        const unsigned char *p_entry = get_partition_entry_ptr(mbr_data, i);

        unsigned char partition_type = p_entry[PART_TYPE_OFFSET];
        unsigned int start_lba = get_little_endian_uint(p_entry, PART_START_LBA_OFFSET);
        unsigned int num_sectors = get_little_endian_uint(p_entry, PART_NUM_SECTORS_OFFSET);

        if (partition_type == 0x00 && start_lba == 0 && num_sectors == 0) {
            mvprintw(8 + i, 0, "%-9d | <Sin Particion>                                  |", i + 1);
        } else {
            if (i == selected_index) {
                attron(A_REVERSE);
            }
            // Extrayendo los bytes CHS
            unsigned char start_chs_cyl_msb = p_entry[PART_START_CHS_OFFSET + 2]; // Parte alta del cilindro
            unsigned char start_chs_cyl_lsb = p_entry[PART_START_CHS_OFFSET + 3]; // Parte baja del cilindro y sector
            unsigned char start_chs_head = p_entry[PART_START_CHS_OFFSET + 1];

            unsigned int start_cyl = (unsigned int)start_chs_cyl_msb + ((start_chs_cyl_lsb & 0xC0) << 2);
            unsigned int start_head = (unsigned int)start_chs_head;
            unsigned int start_sector = (unsigned int)(start_chs_cyl_lsb & 0x3F);

            unsigned char end_chs_cyl_msb = p_entry[PART_END_CHS_OFFSET + 2];
            unsigned char end_chs_cyl_lsb = p_entry[PART_END_CHS_OFFSET + 3];
            unsigned char end_chs_head = p_entry[PART_END_CHS_OFFSET + 1];

            unsigned int end_cyl = (unsigned int)end_chs_cyl_msb + ((end_chs_cyl_lsb & 0xC0) << 2);
            unsigned int end_head = (unsigned int)end_chs_head;
            unsigned int end_sector = (unsigned int)(end_chs_cyl_lsb & 0x3F);


            mvprintw(8 + i, 0, "%-9d | C:%-3u | H:%-3u | S:%-3u | C:%-3u | H:%-3u | S:%-3u | %-16u | %-17u ",
                     i + 1,
                     start_cyl, start_head, start_sector,
                     partition_type,
                     end_cyl, end_head, end_sector,
                     start_lba,
                     num_sectors);
            attroff(A_REVERSE);
        }
    }
    mvprintw(15, 0, "Usa ARRIBA/ABAJO para seleccionar particion. Presiona ENTER para ver detalles, 'q' para salir.");
    refresh();
}

// Función para mostrar los detalles de una partición específica
void display_partition_details(const unsigned char *mbr_data, int index) {
    clear();
    const unsigned char *p_entry = get_partition_entry_ptr(mbr_data, index);

    unsigned char boot_indicator = p_entry[PART_BOOT_INDICATOR_OFFSET];
    unsigned char partition_type = p_entry[PART_TYPE_OFFSET];
    unsigned int start_lba = get_little_endian_uint(p_entry, PART_START_LBA_OFFSET);
    unsigned int num_sectors = get_little_endian_uint(p_entry, PART_NUM_SECTORS_OFFSET);

    // Extrayendo los bytes CHS para detalles (igual que en display_mbr_and_partitions)
    unsigned char start_chs_cyl_msb = p_entry[PART_START_CHS_OFFSET + 2];
    unsigned char start_chs_cyl_lsb = p_entry[PART_START_CHS_OFFSET + 3];
    unsigned char start_chs_head = p_entry[PART_START_CHS_OFFSET + 1];

    unsigned int start_cyl = (unsigned int)start_chs_cyl_msb + ((start_chs_cyl_lsb & 0xC0) << 2);
    unsigned int start_head = (unsigned int)start_chs_head;
    unsigned int start_sector = (unsigned int)(start_chs_cyl_lsb & 0x3F);

    unsigned char end_chs_cyl_msb = p_entry[PART_END_CHS_OFFSET + 2];
    unsigned char end_chs_cyl_lsb = p_entry[PART_END_CHS_OFFSET + 3];
    unsigned char end_chs_head = p_entry[PART_END_CHS_OFFSET + 1];

    unsigned int end_cyl = (unsigned int)end_chs_cyl_msb + ((end_chs_cyl_lsb & 0xC0) << 2);
    unsigned int end_head = (unsigned int)end_chs_head;
    unsigned int end_sector = (unsigned int)(end_chs_cyl_lsb & 0x3F);


    mvprintw(0, 0, "--- Detalles de la Particion %d ---", index + 1);
    mvprintw(2, 0, "Indicador de arranque: 0x%02X (%s)", boot_indicator,
             (boot_indicator == 0x80) ? "Booteable" : "No Booteable");
    mvprintw(3, 0, "Tipo de Particion: 0x%02X (%s)", partition_type,
             (partition_type == 0x07) ? "NTFS/HPFS" : "Otro");

    mvprintw(5, 0, "Inicio (CHS): Cilindro=%u, Cabeza=%u, Sector=%u", start_cyl, start_head, start_sector);
    mvprintw(6, 0, "Fin (CHS):    Cilindro=%u, Cabeza=%u, Sector=%u", end_cyl, end_head, end_sector);

    mvprintw(8, 0, "LBA de Inicio: %u (sector)", start_lba);
    mvprintw(9, 0, "Numero de Sectores: %u", num_sectors);
    mvprintw(10, 0, "Tamano: %.2f GB", (double)num_sectors * 512 / (1024 * 1024 * 1024));

    mvprintw(12, 0, "Presiona 'b' para volver al menu principal.");
    refresh();
}


int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Uso: %s <ruta_a_im_ntfs.img>\n", argv[0]);
        return 1;
    }

    const char *image_file = argv[1];
    unsigned char mbr_bytes[MBR_SIZE]; // Array de bytes para almacenar el MBR
    int selected_partition_index = 0; // Índice de la partición seleccionada (0-3)
    int key_pressed; // Caracter leído
    int menu_state = 0; // 0 = MBR/Particiones, 1 = Detalles de Partición

    // --- Inicialización de Ncurses ---
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE); // Habilita KEY_UP, KEY_DOWN, etc.

    // --- Lectura del MBR ---
    if (read_mbr(image_file, mbr_bytes) != 0) {
        mvprintw(0, 0, "Error al leer o validar el MBR de la imagen '%s'.", image_file);
        mvprintw(1, 0, "Revisa los permisos o si el archivo es un MBR valido.");
        mvprintw(2, 0, "Error: %s", strerror(errno));
        refresh();
        getch(); // Espera a que el usuario vea el error
        endwin();
        return 1;
    }

    // No necesitamos contar "num_active_partitions" para el movimiento si siempre hay 4 entradas.
    // El cursor simplemente se moverá entre 0 y 3.

    // --- Bucle principal del menú de Ncurses ---
    do {
        if (menu_state == 0) { // Menú principal: MBR y selección de particiones
            display_mbr_and_partitions(mbr_bytes, selected_partition_index);

            key_pressed = get_key_input(); // Lee la entrada

            switch (key_pressed) {
                case KEY_UP:
                    selected_partition_index = (selected_partition_index > 0) ? selected_partition_index - 1 : 3;
                    break;
                case KEY_DOWN:
                    selected_partition_index = (selected_partition_index < 3) ? selected_partition_index + 1 : 0;
                    break;
                case 10: // Tecla ENTER
                    // Obtener los datos de la partición seleccionada para verificar si está "vacía"
                    const unsigned char *p_entry_selected = get_partition_entry_ptr(mbr_bytes, selected_partition_index);
                    unsigned char partition_type_selected = p_entry_selected[PART_TYPE_OFFSET];
                    unsigned int start_lba_selected = get_little_endian_uint(p_entry_selected, PART_START_LBA_OFFSET);
                    unsigned int num_sectors_selected = get_little_endian_uint(p_entry_selected, PART_NUM_SECTORS_OFFSET);

                    if (!(partition_type_selected == 0x00 && start_lba_selected == 0 && num_sectors_selected == 0)) {
                        menu_state = 1; // Cambia al estado de detalles de partición
                    }
                    break;
                default:
                    break;
            }
        } else if (menu_state == 1) { // Estado: Mostrando detalles de una partición
            display_partition_details(mbr_bytes, selected_partition_index);
            key_pressed = get_key_input();
            if (key_pressed == 'b' || key_pressed == 'B') { // 'b' para volver
                menu_state = 0;
            }
        }

    } while (key_pressed != 'q' && key_pressed != 'Q'); // 'q' o 'Q' para salir de la aplicación

    // --- Limpieza de Ncurses ---
    endwin();
    return 0;
}