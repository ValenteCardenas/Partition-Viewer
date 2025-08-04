#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <wchar.h>
#include <locale.h>

// Estructuras NTFS mejoradas
#pragma pack(push, 1)
typedef struct {
    uint8_t szSignature[4];    // "FILE"
    uint16_t wFixupOffset;
    uint16_t wFixupSize;
    uint64_t n64LogSeqNumber;
    uint16_t wSequence;
    uint16_t wHardLinks;
    uint16_t wAttribOffset;
    uint16_t wFlags;
    uint32_t dwRecLength;
    uint32_t dwAllLength;
    uint64_t n64BaseMftRec;
    uint16_t wNextAttribID;
    uint16_t wFixupPattern;
    uint32_t dwMftRecNumber;
} NTFS_MFT_FILE;

typedef struct {
    uint32_t dwType;
    uint32_t dwFullLength;
    uint8_t uchNonResFlag;
    uint8_t uchNameLength;
    uint16_t wNameOffset;
    uint16_t wFlags;
    uint16_t wID;
    union {
        struct {
            uint32_t dwLength;
            uint16_t wAttrOffset;
            uint8_t uchIndexedTag;
            uint8_t uchPadding;
        } Resident;
        struct {
            uint64_t n64StartVCN;
            uint64_t n64EndVCN;
            uint16_t wDatarunOffset;
            uint16_t wCompressionSize;
            uint8_t uchPadding[4];
            uint64_t n64AllocSize;
            uint64_t n64RealSize;
            uint64_t n64StreamSize;
        } NonResident;
    } Attr;
} NTFS_ATTRIBUTE;

typedef struct {
    uint64_t dwMftParentDir;
    uint64_t n64Create;
    uint64_t n64Modify;
    uint64_t n64ModFile;
    uint64_t n64Access;
    uint64_t n64Allocated;
    uint64_t n64RealSize;
    uint32_t dwFlags;
    uint32_t dwFhgReparsTag;
    uint8_t chFileNameLength;
    uint8_t chFileNameType;
    uint16_t wFilename[255];
} ATTR_FILENAME;

typedef struct {
    uint32_t dwFATAttributes;
    uint64_t n64CreateTime;
    uint64_t n64ModifyTime;
    uint64_t n64MftChangeTime;
    uint64_t n64AccessTime;
    uint32_t dwFilePermissions;
} ATTR_STANDARD;
#pragma pack(pop)

// Constantes
#define MBR_PARTITION_TABLE_OFFSET 0x1BE
#define MBR_SIGNATURE_OFFSET 0x1FE
#define PART_TYPE_OFFSET 0x04
#define PART_START_LBA_OFFSET 0x08
#define PART_NUM_SECTORS_OFFSET 0x0C
#define NTFS_MFT_ENTRY_SIZE 1024
#define MAX_ENTRIES 100

// Variables globales
int fd;
char *disk_map;
size_t disk_size;

// Prototipos de funciones
char *map_disk_image(const char *path);
void unmap_disk_image();
void mostrar_particiones(char *map, int selected);
void detalles_particion(unsigned char *map, int index);
void recorrer_mft(unsigned char *map, unsigned int lba_inicio);
uint32_t read_uint32_le(const unsigned char *data);
uint64_t read_uint64_le(const unsigned char *data);
uint16_t read_uint16_le(const unsigned char *data);
void unicode_to_ascii(const uint16_t *unicode, char *ascii, int len);
void determinar_tipo_archivo(const char *nombre, char *tipo);

// Función principal
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    if (argc != 2) {
        printf("Uso: %s <imagen_disco>\n", argv[0]);
        return 1;
    }

    disk_map = map_disk_image(argv[1]);
    if (!disk_map) return 1;

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int selected_partition = 0;
    int ch;

    do {
        mostrar_particiones(disk_map, selected_partition);
        ch = getch();
        
        switch(ch) {
            case KEY_UP:
                selected_partition = (selected_partition > 0) ? selected_partition - 1 : 3;
                break;
            case KEY_DOWN:
                selected_partition = (selected_partition < 3) ? selected_partition + 1 : 0;
                break;
            case 10: // Enter
                {
                    unsigned char *p_entry = (unsigned char *)disk_map + MBR_PARTITION_TABLE_OFFSET + 
                                           (selected_partition * 16);
                    uint32_t num_sectors = read_uint32_le(p_entry + PART_NUM_SECTORS_OFFSET);
                    
                    if(num_sectors == 0) {
                        mvprintw(12, 0, "Particion %d esta VACIA.", selected_partition + 1);
                        refresh();
                        getch();
                    } else {
                        detalles_particion((unsigned char *)disk_map, selected_partition);
                        uint32_t lba_inicio = read_uint32_le(p_entry + PART_START_LBA_OFFSET);
                        recorrer_mft((unsigned char *)disk_map, lba_inicio);
                    }
                }
                break;
        }
    } while(ch != 'q' && ch != 'Q');

    endwin();
    unmap_disk_image();
    return 0;
}

// Implementación de funciones

char *map_disk_image(const char *path) {
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Error al abrir imagen de disco");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st)) {
        perror("Error al obtener tamaño del archivo");
        close(fd);
        return NULL;
    }
    disk_size = st.st_size;

    char *map = mmap(NULL, disk_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("Error al mapear imagen de disco");
        close(fd);
        return NULL;
    }

    if (*(uint16_t*)(map + MBR_SIGNATURE_OFFSET) != 0xAA55) {
        fprintf(stderr, "MBR no válido\n");
        munmap(map, disk_size);
        close(fd);
        return NULL;
    }

    return map;
}

void unmap_disk_image() {
    if (disk_map) {
        munmap(disk_map, disk_size);
        close(fd);
    }
}

void mostrar_particiones(char *map, int selected) {
    clear();
    
    mvprintw(0, 0, "Particiones del disco (seleccione con flechas)");
    mvprintw(2, 0, "Particion | Tipo | Inicio LBA | Sectores | Tamaño (MB)");
    
    for (int i = 0; i < 4; i++) {
        unsigned char *p_entry = (unsigned char *)map + MBR_PARTITION_TABLE_OFFSET + (i * 16);
        uint32_t num_sectors = read_uint32_le(p_entry + PART_NUM_SECTORS_OFFSET);
        
        if (i == selected) attron(A_REVERSE);
        
        if (num_sectors == 0) {
            mvprintw(4 + i, 0, "%9d | VACIA", i + 1);
        } else {
            float size_mb = (num_sectors * 512.0) / (1024 * 1024);
            mvprintw(4 + i, 0, "%9d | 0x%02X | %10u | %8u | %.2f",
                     i + 1,
                     p_entry[PART_TYPE_OFFSET],
                     read_uint32_le(p_entry + PART_START_LBA_OFFSET),
                     num_sectors,
                     size_mb);
        }
        
        if (i == selected) attroff(A_REVERSE);
    }
    
    mvprintw(10, 0, "Flechas: Seleccionar | Enter: Detalles | q: Salir");
    refresh();
}

void detalles_particion(unsigned char *map, int index) {
    clear();
    unsigned char *p_entry = (unsigned char *)map + MBR_PARTITION_TABLE_OFFSET + (index * 16);
    uint32_t lba_inicio = read_uint32_le(p_entry + PART_START_LBA_OFFSET);
    unsigned char *boot_sector = map + (lba_inicio * 512);

    char oem_id[9];
    memcpy(oem_id, &boot_sector[0x03], 8);
    oem_id[8] = '\0';

    uint16_t bytes_per_sector = read_uint16_le(boot_sector + 0x0B);
    uint8_t sectors_per_cluster = boot_sector[0x0D];
    uint16_t reserved_sectors = read_uint16_le(boot_sector + 0x0E);
    uint64_t mft_cluster = read_uint64_le(boot_sector + 0x30);
    uint64_t mft_mirr_cluster = read_uint64_le(boot_sector + 0x38);
    uint32_t clusters_per_mft = *(int8_t *)(boot_sector + 0x40);
    if (clusters_per_mft < 0) {
        clusters_per_mft = 1 << (-clusters_per_mft);
    }

    mvprintw(0, 0, "--- Detalles de la Partición %d (NTFS) ---", index + 1);
    mvprintw(2, 0, "OEM ID: %s", oem_id);
    mvprintw(3, 0, "Bytes por sector: %u", bytes_per_sector);
    mvprintw(4, 0, "Sectores por cluster: %u", sectors_per_cluster);
    mvprintw(5, 0, "Tamaño cluster: %u bytes", bytes_per_sector * sectors_per_cluster);
    mvprintw(6, 0, "Cluster MFT: %" PRIu64 " (0x%" PRIx64 ")", mft_cluster, mft_cluster);
    mvprintw(7, 0, "Cluster MFT Mirror: %" PRIu64 " (0x%" PRIx64 ")", mft_mirr_cluster, mft_mirr_cluster);
    mvprintw(8, 0, "Clusters por registro MFT: %u", clusters_per_mft);
    mvprintw(10, 0, "Presione cualquier tecla para ver el MFT...");
    refresh();
    getch();
}

void determinar_tipo_archivo(const char *nombre, char *tipo) {
    const char *ext = strrchr(nombre, '.');
    if (!ext) {
        strcpy(tipo, "Archivo");
        return;
    }
    
    ext++; // Saltar el punto
    
    if (strcasecmp(ext, "txt") == 0) strcpy(tipo, "Texto");
    else if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) strcpy(tipo, "HTML");
    else if (strcasecmp(ext, "exe") == 0 || strcasecmp(ext, "dll") == 0) strcpy(tipo, "Ejecutable");
    else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) strcpy(tipo, "Imagen JPEG");
    else if (strcasecmp(ext, "png") == 0) strcpy(tipo, "Imagen PNG");
    else if (strcasecmp(ext, "pdf") == 0) strcpy(tipo, "PDF");
    else if (strcasecmp(ext, "doc") == 0 || strcasecmp(ext, "docx") == 0) strcpy(tipo, "Documento Word");
    else if (strcasecmp(ext, "xls") == 0 || strcasecmp(ext, "xlsx") == 0) strcpy(tipo, "Hoja de cálculo");
    else strcpy(tipo, "Archivo");
}

void recorrer_mft(unsigned char *map, unsigned int lba_inicio) {
    clear();
    unsigned char *boot_sector = map + (lba_inicio * 512);
    
    uint16_t bytes_per_sector = read_uint16_le(boot_sector + 0x0B);
    uint8_t sectors_per_cluster = boot_sector[0x0D];
    uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
    uint64_t mft_cluster = read_uint64_le(boot_sector + 0x30);
    unsigned char *mft = map + (lba_inicio * 512) + (mft_cluster * cluster_size);
    
    mvprintw(0, 0, "--- Entradas del MFT ---");
    mvprintw(1, 0, "Num | Nombre                | Tipo       | Atributos");
    mvprintw(2, 0, "----+-----------------------+------------+------------------");
    
    int fila = 3;
    for (int i = 0; i < MAX_ENTRIES && fila < LINES - 2; i++) {
        NTFS_MFT_FILE *mft_file = (NTFS_MFT_FILE *)(mft + (i * NTFS_MFT_ENTRY_SIZE));
        
        if (memcmp(mft_file->szSignature, "FILE", 4) != 0) continue;
        
        char nombre[256] = "(sin nombre)";
        char tipo[16] = "Archivo";
        char atributos[64] = "";
        int tiene_nombre_valido = 0;
        int es_directorio = 0;
        
        NTFS_ATTRIBUTE *attr = (NTFS_ATTRIBUTE *)((char *)mft_file + mft_file->wAttribOffset);
        while ((char *)attr < (char *)mft_file + mft_file->dwRecLength && 
               attr->dwType != 0xFFFFFFFF) {
            
            if (attr->dwType == 0x30) { // $FILE_NAME
                if (attr->uchNonResFlag == 0) {
                    ATTR_FILENAME *fn = (ATTR_FILENAME *)((char *)attr + attr->Attr.Resident.wAttrOffset);
                    unicode_to_ascii(fn->wFilename, nombre, fn->chFileNameLength);
                    tiene_nombre_valido = 1;
                    
                    // Verificar si es directorio (bit 4 en dwFlags)
                    if (fn->dwFlags & 0x10000000) {
                        es_directorio = 1;
                    }
                }
            }
            else if (attr->dwType == 0x10) { // $STANDARD_INFORMATION
                if (attr->uchNonResFlag == 0) {
                    ATTR_STANDARD *std_info = (ATTR_STANDARD *)((char *)attr + attr->Attr.Resident.wAttrOffset);
                    uint32_t flags = std_info->dwFATAttributes;
                    
                    if (flags & 0x01) strcat(atributos, "RO ");
                    if (flags & 0x02) strcat(atributos, "Oculto ");
                    if (flags & 0x04) strcat(atributos, "Sistema ");
                    if (flags & 0x20) strcat(atributos, "Archive ");
                }
            }
            
            attr = (NTFS_ATTRIBUTE *)((char *)attr + attr->dwFullLength);
        }
        
        // Determinar el tipo
        if (es_directorio) {
    strcpy(tipo, "Directorio");
} else {
    determinar_tipo_archivo(nombre, tipo);
}
        
        if (tiene_nombre_valido || nombre[0] == '$') {
            // Acortar nombres muy largos para mejor visualización
            char nombre_display[22];
            strncpy(nombre_display, nombre, 20);
            nombre_display[20] = '\0';
            if (strlen(nombre) > 20) {
                nombre_display[18] = '.';
                nombre_display[19] = '.';
                nombre_display[20] = '\0';
            }
            
            mvprintw(fila++, 0, "%3d | %-21s | %-10s | %s", 
                    i, nombre_display, tipo, atributos);
        }
    }
    
    mvprintw(LINES - 1, 0, "Presione cualquier tecla para volver...");
    refresh();
    getch();
}

void unicode_to_ascii(const uint16_t *unicode, char *ascii, int len) {
    for (int i = 0; i < len && i < 255; i++) {
        ascii[i] = (unicode[i] < 128) ? (char)unicode[i] : '?';
    }
    ascii[len] = '\0';
}

uint32_t read_uint32_le(const unsigned char *data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

uint64_t read_uint64_le(const unsigned char *data) {
    return (uint64_t)data[0] | ((uint64_t)data[1] << 8) | 
           ((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) | 
           ((uint64_t)data[6] << 48) | ((uint64_t)data[7] << 56);
}

uint16_t read_uint16_le(const unsigned char *data) {
    return data[0] | (data[1] << 8);
}
