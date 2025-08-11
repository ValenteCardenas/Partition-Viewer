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
#include <time.h>
#include <inttypes.h>

#include "ntfs.h"
#include "hexEditor.h"

#define MBR_PARTITION_TABLE_OFFSET 0x1BE
#define MBR_SIGNATURE_OFFSET       0x1FE
#define PART_TYPE_OFFSET           0x04
#define PART_START_LBA_OFFSET      0x08
#define PART_NUM_SECTORS_OFFSET    0x0C
#define PART_START_CHS_OFFSET      0x01
#define PART_END_CHS_OFFSET        0x05

int fd;
long mapped_file_size = 0;

char *mapFile(char *filePath) {
    fd = open(filePath, O_RDONLY);
    if (fd == -1) {
        perror("Error abriendo el archivo");
        return NULL;
    }

    struct stat st;
    fstat(fd, &st);
    long fs = st.st_size;
    mapped_file_size = fs;

    char *map = mmap(0, fs, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        perror("Error mapeando el archivo");
        return NULL;
    }

    return map;
}

int leeChar() {
    int chars[5];
    int ch, i = 0;
    nodelay(stdscr, TRUE);
    while ((ch = getch()) == ERR);
    ungetch(ch);
    while ((ch = getch()) != ERR) {
        chars[i++] = ch;
    }

    int res = 0;
    for (int j = 0; j < i; j++) {
        res <<= 8;
        res |= chars[j];
    }
    return res;
}

unsigned char* get_partition_entry_ptr(unsigned char *mbr_data, int index) {
    return mbr_data + MBR_PARTITION_TABLE_OFFSET + (index * 16);
}

void mostrar_particiones(char *map, int selected_partition_index) {
    clear();
    mvprintw(4, 0, "Particion | Inicio CHS (C|H|S) | Fin CHS (C|H|S) | Tipo | LBA Inicio | Tamano (Sectores)");
    for (int i = 0; i < 4; i++) {
        unsigned char *p_entry = (unsigned char *)map + MBR_PARTITION_TABLE_OFFSET + (i * 16);
        int num_sectors = *(unsigned int *)&p_entry[PART_NUM_SECTORS_OFFSET];

        if (num_sectors == 0) {
            mvprintw(5 + i, 0, "Particion %d: VACIA", i + 1);
        } else {
            if (i == selected_partition_index) {
                attron(A_REVERSE);
            }

            unsigned char start_chs_head = p_entry[PART_START_CHS_OFFSET];
            unsigned char start_chs_sector_cyl_high = p_entry[PART_START_CHS_OFFSET + 1];
            unsigned char start_chs_cyl_low = p_entry[PART_START_CHS_OFFSET + 2];

            unsigned int start_head = start_chs_head;
            unsigned int start_sector = start_chs_sector_cyl_high & 0x3F;
            unsigned int start_cyl = (unsigned int)start_chs_cyl_low + ((start_chs_sector_cyl_high & 0xC0) << 2);

            unsigned char end_chs_head = p_entry[PART_END_CHS_OFFSET];
            unsigned char end_chs_sector_cyl_high = p_entry[PART_END_CHS_OFFSET + 1];
            unsigned char end_chs_cyl_low = p_entry[PART_END_CHS_OFFSET + 2];

            unsigned int end_head = end_chs_head;
            unsigned int end_sector = end_chs_sector_cyl_high & 0x3F;
            unsigned int end_cyl = (unsigned int)end_chs_cyl_low + ((end_chs_sector_cyl_high & 0xC0) << 2);
            
            unsigned int start_lba = *(unsigned int *)&p_entry[PART_START_LBA_OFFSET];

            mvprintw(5 + i, 0, "Particion %d: C:%-4u H:%-4u S:%-4u | C:%-4u H:%-4u S:%-4u | 0x%-2X | %-11u | %-17u",
                     i + 1,
                     start_cyl, start_head, start_sector,
                     end_cyl, end_head, end_sector,
                     p_entry[PART_TYPE_OFFSET],
                     start_lba,
                     num_sectors);

            attroff(A_REVERSE);
        }
    }
    mvprintw(10, 0, "Presiona 'q' para salir.");
    mvprintw(11, 0, "Usa ARRIBA/ABAJO para seleccionar particion. Presiona ENTER para ver detalles.");
    refresh();
}

void detalles_particion(unsigned char *mbr_data, int index) {
    clear();

    unsigned char *p_entry = get_partition_entry_ptr(mbr_data, index);
    unsigned int lba_inicio = *(unsigned int *)&p_entry[PART_START_LBA_OFFSET];
    unsigned int offset = lba_inicio * 512;

    unsigned char *boot_sector = mbr_data + offset;

    char oem_id[9];
    memcpy(oem_id, &boot_sector[0x03], 8);
    oem_id[8] = '\0';

    unsigned short bytes_por_sector = *(unsigned short *)&boot_sector[0x0B];
    unsigned char sectors_per_cluster = boot_sector[0x0D];
    unsigned short reserved_sectors = *(unsigned short *)&boot_sector[0x0E];
    unsigned char fat_count = boot_sector[0x10];
    unsigned int fat_size = *(unsigned int *)&boot_sector[0x24];
    unsigned short end_marker = *(unsigned short *)&boot_sector[0x1FE];

    unsigned int start_lba = *(unsigned int *)&p_entry[PART_START_LBA_OFFSET];
    unsigned int num_sectors = *(unsigned int *)&p_entry[PART_NUM_SECTORS_OFFSET];

    mvprintw(2, 0, "--- Detalles de la Particion %d ---", index + 1);
    mvprintw(4, 0, "OEM ID: %s", oem_id);
    mvprintw(5, 0, "Bytes por sector: %d", bytes_por_sector);
    mvprintw(6, 0, "Sectores por cluster: %d", sectors_per_cluster);
    mvprintw(7, 0, "Sectores reservados: %d", reserved_sectors);
    mvprintw(8, 0, "Numero de FATs: %d", fat_count);
    mvprintw(9, 0, "Tamano de cada FAT (bytes): %u", fat_size);
    mvprintw(10, 0, "End of sector marker (esperado 0xAA55): 0x%04X", end_marker);
    mvprintw(11, 0, "LBA de Inicio: %u (sector)", start_lba);
    mvprintw(12, 0, "Numero de Sectores: %u", num_sectors);
    mvprintw(14, 0, "Presiona cualquier tecla para volver...");
    refresh();

    getch();
}

void determinar_tipo_archivo(const char *nombre, char *tipo) {
    const char *ext = strrchr(nombre, '.');
    if (!ext) {
        strcpy(tipo, "Archivo");
        return;
    }
    
    ext++;
    
    if (strcasecmp(ext, "txt") == 0) strcpy(tipo, "Texto");
    else if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) strcpy(tipo, "HTML");
    else if (strcasecmp(ext, "exe") == 0 || strcasecmp(ext, "dll") == 0) strcpy(tipo, "Ejecutable");
    else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) strcpy(tipo, "Imagen JPEG");
    else if (strcasecmp(ext, "png") == 0) strcpy(tipo, "Imagen PNG");
    else if (strcasecmp(ext, "pdf") == 0) strcpy(tipo, "PDF");
    else if (strcasecmp(ext, "doc") == 0 || strcasecmp(ext, "docx") == 0) strcpy(tipo, "Documento Word");
    else if (strcasecmp(ext, "xls") == 0 || strcasecmp(ext, "xlsx") == 0) strcpy(tipo, "Hoja de calculo");
    else strcpy(tipo, "Archivo");
}

void filetime_to_str(LONGLONG ft, char *out, size_t out_sz) {
    if (ft == 0) {
        strncpy(out, "(sin fecha)", out_sz);
        out[out_sz-1] = '\0';
        return;
    }
    uint64_t unix_time_sec = (uint64_t)((ft - 116444736000000000ULL) / 10000000ULL);
    time_t t = (time_t)unix_time_sec;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, out_sz, "%Y-%m-%d %H:%M:%S", &tm);
}

void descargar_archivo(unsigned char *map, off_t offset, size_t longitud, const char *nombre_original) {
    char nombre_destino[256];
    
    mvprintw(LINES - 2, 0, "Guardar como (ESC para cancelar): ");
    echo();
    curs_set(1);
    
    strncpy(nombre_destino, nombre_original, sizeof(nombre_destino) - 1);
    nombre_destino[sizeof(nombre_destino) - 1] = '\0';
    
    move(LINES - 2, strlen("Guardar como (ESC para cancelar): "));
    getnstr(nombre_destino, sizeof(nombre_destino) - 1);

    noecho();
    curs_set(0);
    clear(); 

    if (strlen(nombre_destino) == 0) {
        mvprintw(LINES - 2, 0, "Descarga cancelada. Presiona cualquier tecla para continuar.");
        refresh();
        getch();
        return;
    }

    FILE *outfile = fopen(nombre_destino, "wb");
    if (!outfile) {
        mvprintw(LINES - 2, 0, "Error: No se pudo crear el archivo '%s'. Presiona cualquier tecla.", nombre_destino);
        refresh();
        getch();
        return;
    }

    size_t bytes_escritos = fwrite(map + offset, 1, longitud, outfile);
    
    fclose(outfile); 
    if (bytes_escritos == longitud) {
        mvprintw(LINES - 2, 0, "Archivo '%s' guardado con exito (%zu bytes). Presiona cualquier tecla.", nombre_destino, bytes_escritos);
    } else {
        mvprintw(LINES - 2, 0, "Error al escribir en '%s'. Se escribieron %zu de %zu bytes. Presiona cualquier tecla.", nombre_destino, bytes_escritos, longitud);
    }
    
    refresh();
    getch();
}

void recorrer_mft(unsigned char *map, unsigned int lba_inicio) {
    clear();
    mvprintw(0, 0, "--- Entrada del MFT ---");

    unsigned char *mft_base = map + (lba_inicio * 512);

    unsigned short bytes_por_sector = *(unsigned short *)&mft_base[0x0B];
    unsigned char sectores_por_cluster = mft_base[0x0D];
    LONGLONG mft_cluster = *(LONGLONG *)&mft_base[0x30];
    int tamaño_cluster = bytes_por_sector * sectores_por_cluster;
    int entry_size = 1024;

    #define MAX_ENTRIES 1024
    long entry_data_offset[MAX_ENTRIES];
    size_t entry_data_len[MAX_ENTRIES];
    char entry_name[MAX_ENTRIES][64];
    char entry_date_created[MAX_ENTRIES][20];
    char entry_date_modified[MAX_ENTRIES][20];
    uint64_t entry_real_size[MAX_ENTRIES];
    char entry_type[MAX_ENTRIES][16];
    char entry_attributes[MAX_ENTRIES][64];
    int entry_count = 0;

    unsigned char *mft = map + (lba_inicio * 512) + (mft_cluster * tamaño_cluster);

    for (int i = 0; i < 150 && entry_count < MAX_ENTRIES; i++) {
        struct NTFS_MFT_FILE *mft_file = (struct NTFS_MFT_FILE *)(mft + (i * entry_size));
        if (memcmp(mft_file->szSignature, "FILE", 4) != 0) continue;

        char nombre[256] = "(sin nombre)";
        char tipo[16] = "Archivo";
        char atributos[64] = "";
        int tiene_nombre_valido = 0;
        int es_directorio = 0;
        uint64_t tamaño_real = 0;
        char fecha_creacion[20] = "(sin fecha)";
        char fecha_modificacion[20] = "(sin fecha)";

        long found_data_offset = -1;
        size_t found_data_len = 0;

        NTFS_ATTRIBUTE *attr = (NTFS_ATTRIBUTE *)((char *)mft_file + mft_file->wAttribOffset);
        while ((char *)attr < (char *)mft_file + mft_file->dwRecLength &&
               attr->dwType != 0xFFFFFFFF) {

            if (attr->dwType == 0x10) {
                if (attr->uchNonResFlag == 0) {
                    ATTR_STANDARD *std_info = (ATTR_STANDARD *)((char *)attr + attr->Attr.Resident.wAttrOffset);
                    uint32_t flags = std_info->dwFATAttributes;
                    if (flags & 0x01) strcat(atributos, "RO ");
                    if (flags & 0x02) strcat(atributos, "Oculto ");
                    if (flags & 0x04) strcat(atributos, "Sistema ");
                    if (flags & 0x20) strcat(atributos, "Archive ");
                }
            }
            else if (attr->dwType == 0x30) {
                if (attr->uchNonResFlag == 0) {
                    ATTR_FILENAME *fn = (ATTR_FILENAME *)((char *)attr + attr->Attr.Resident.wAttrOffset);
                    int len = fn->chFileNameLength;
                    for (int j = 0; j < len && j < 255; j++) {
                        nombre[j] = (fn->wFilename[j] < 128) ? fn->wFilename[j] : '?';
                    }
                    nombre[len] = '\0';
                    tiene_nombre_valido = 1;

                    if (fn->dwFlags & 0x10000000) es_directorio = 1;
                    tamaño_real = fn->n64RealSize;
                    filetime_to_str(fn->n64Create, fecha_creacion, sizeof(fecha_creacion));
                    filetime_to_str(fn->n64Modify, fecha_modificacion, sizeof(fecha_modificacion));
                }
            }
            else if (attr->dwType == 0x80) {
                if (attr->uchNonResFlag == 0) {
                    unsigned char *data_ptr = (unsigned char *)attr + attr->Attr.Resident.wAttrOffset;
                    found_data_offset = data_ptr - map;
                    found_data_len = attr->Attr.Resident.dwLength;
                } else {
                    unsigned char *datarun = (unsigned char *)attr + attr->Attr.NonResident.wDatarunOffset;
                    long long prev_lcn = 0;
                    int pos = 0;
                    if (datarun[pos] != 0) {
                        unsigned char header = datarun[pos++];
                        int len_len = header & 0x0F;
                        int off_len = (header >> 4) & 0x0F;
                        unsigned long long cluster_count = 0;
                        for (int k = 0; k < len_len; k++) {
                            cluster_count |= ((unsigned long long)datarun[pos++]) << (8 * k);
                        }
                        long long cluster_offset = 0;
                        if (off_len > 0) {
                            unsigned long long tmp = 0;
                            for (int k = 0; k < off_len; k++) {
                                tmp |= ((unsigned long long)datarun[pos++]) << (8 * k);
                            }
                            unsigned long long signbit = 1ULL << (off_len*8 - 1);
                            if (tmp & signbit) {
                                unsigned long long mask = (~0ULL) << (off_len*8);
                                tmp |= mask;
                            }
                            cluster_offset = (long long)tmp;
                        }
                        prev_lcn += cluster_offset;
                        long long abs_sector = (long long)lba_inicio + (prev_lcn * (long long)sectores_por_cluster);
                        long long abs_byte_offset = abs_sector * (long long)bytes_por_sector;
                        size_t approx_bytes = (size_t)cluster_count * (size_t)tamaño_cluster;

                        if (abs_byte_offset >= 0 && abs_byte_offset < mapped_file_size) {
                            found_data_offset = abs_byte_offset;
                            found_data_len = approx_bytes;
                        }
                    }
                }
            }

            attr = (NTFS_ATTRIBUTE *)((char *)attr + attr->dwFullLength);
        }

        if (es_directorio) {
            strcpy(tipo, "Directorio");
        } else {
            determinar_tipo_archivo(nombre, tipo);
        }

        if (tiene_nombre_valido || nombre[0] == '$') {
            strncpy(entry_name[entry_count], nombre, 63);
            entry_name[entry_count][63] = '\0';
            entry_data_offset[entry_count] = found_data_offset;
            entry_data_len[entry_count] = found_data_len;
            strncpy(entry_date_created[entry_count], fecha_creacion, 19);
            entry_date_created[entry_count][19] = '\0';
            strncpy(entry_date_modified[entry_count], fecha_modificacion, 19);
            entry_date_modified[entry_count][19] = '\0';
            entry_real_size[entry_count] = tamaño_real;
            strncpy(entry_type[entry_count], tipo, 15);
            entry_type[entry_count][15] = '\0';
            strncpy(entry_attributes[entry_count], atributos, 63);
            entry_attributes[entry_count][63] = '\0';
            entry_count++;
        }
    }

    int sel = 0;
    int c;
    do {
        clear();
        mvprintw(0, 0, "--- Entrada del MFT (Selecciona con flechas y ENTER para ver hex) ---");
        mvprintw(1, 0, "Num | Nombre                | Tipo       | Tamano    | Creado             | Modificado");
        mvprintw(2, 0, "----+-----------------------+------------+-----------+---------------------+---------------------");
        
        int start_row = 3;
        int rows = LINES - 5;
        int base = 0;
        
        if (sel >= base + rows) base = sel - rows + 1;
        if (sel < base) base = sel;

        for (int i = 0; i < rows && (base + i) < entry_count; i++) {
            int idx = base + i;
            if (idx == sel) attron(A_REVERSE);
            
            char display_name[22];
            strncpy(display_name, entry_name[idx], 20);
            display_name[20] = '\0';
            if (strlen(entry_name[idx]) > 20) {
                display_name[18] = '.';
                display_name[19] = '.';
                display_name[20] = '\0';
            }
            
            mvprintw(start_row + i, 0, "%3d | %-21s | %-10s | %9" PRIu64 " | %-19s | %-19s",
                     idx, display_name, 
                     entry_type[idx],
                     entry_real_size[idx],
                     entry_date_created[idx],
                     entry_date_modified[idx]);
            
            if (idx == sel) attroff(A_REVERSE);
        }

        mvprintw(LINES - 2, 0, "q=volver  UP/DOWN=mover  ENTER=abrir hex  d/D=descargar  a=ver atributos");
        clrtoeol();
        refresh();

        c = getch();
        switch (c) {
            case KEY_UP:
                sel = (sel > 0) ? sel - 1 : entry_count - 1;
                break;
            case KEY_DOWN:
                sel = (sel < entry_count - 1) ? sel + 1 : 0;
                break;
            case 10: // ENTER
                if (entry_data_offset[sel] >= 0) {
                    size_t view_len = entry_data_len[sel];
                    hex_viewer_from_map(map, mapped_file_size, (off_t)entry_data_offset[sel], view_len);
                } else {
                    mvprintw(LINES - 2, 0, "No se pudo determinar offset de datos para este archivo. Presiona cualquier tecla...");
                    getch();
                }
                break;
            case 'd':
            case 'D':
                if (entry_data_offset[sel] >= 0 && entry_data_len[sel] > 0) {
                    descargar_archivo(map, (off_t)entry_data_offset[sel], entry_data_len[sel], entry_name[sel]);
                } else {
                    mvprintw(LINES - 2, 0, "No se puede descargar: offset o tamaño de datos no disponible. Presiona una tecla...");
                    getch();
                }
                break;
            case 'a':
            case 'A':
                clear();
                mvprintw(0, 0, "Atributos del archivo: %s", entry_name[sel]);
                mvprintw(2, 0, "Tipo: %s", entry_type[sel]);
                mvprintw(3, 0, "Tamaño real: %" PRIu64 " bytes", entry_real_size[sel]);
                mvprintw(4, 0, "Creado: %s", entry_date_created[sel]);
                mvprintw(5, 0, "Modificado: %s", entry_date_modified[sel]);
                mvprintw(6, 0, "Atributos: %s", entry_attributes[sel]);
                mvprintw(8, 0, "Presiona cualquier tecla para continuar...");
                refresh();
                getch();
                break;
            default:
                break;
        }

    } while (c != 'q' && c != 'Q');

    mvprintw(LINES - 1, 0, "Presione cualquier tecla para volver...");
    refresh();
    getch();
}

int main(int argc, char const *argv[]) {
    int particion_seleccionada = 1;
    if(argc != 2){
        printf("se usa %s \n", argv[0]);
        return (-1);
    }
    char *map = mapFile((char *)argv[1]);
    if (map == NULL) {
        return -1;
    }
    int c;
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    do{
        mostrar_particiones(map, particion_seleccionada);
        c = getch();
        switch (c) {
            case KEY_UP:
                particion_seleccionada = (particion_seleccionada > 0) ? particion_seleccionada - 1 : 3;
                break;
            case KEY_DOWN:
                particion_seleccionada = (particion_seleccionada < 3) ? particion_seleccionada + 1 : 0;
                break;
            case 10: // Enter
                const unsigned char *p_entry = get_partition_entry_ptr((unsigned char *)map, particion_seleccionada);
                if(*(unsigned int *)&p_entry[PART_NUM_SECTORS_OFFSET] == 0) {
                    mvprintw(12, 0, "Particion %d esta VACIA.", particion_seleccionada + 1);
                } else {
                    mvprintw(12, 0, "Mostrando detalles de la particion %d...", particion_seleccionada + 1);
                    detalles_particion((unsigned char *)map, particion_seleccionada);
                    unsigned int lba_inicio = *(unsigned int *)&p_entry[PART_START_LBA_OFFSET];
                    recorrer_mft((unsigned char *)map, lba_inicio);
                }
                break;
            default:
                break;
        }
    } while (c != 'q' && c != 'Q');

    endwin();
    return 0;
}
