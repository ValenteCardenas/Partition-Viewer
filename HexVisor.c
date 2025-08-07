#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <curses.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define LINES_PER_PAGE 25
#define BYTES_PER_LINE 16

/* Variables globales */
int fd; // Descriptor de archivo
char *map; // Mapeo de memoria
long file_size; // Tamaño del archivo
long current_offset = 0; // Offset actual

/* Prototipos de funciones */
void display_page(long offset);
void handle_navigation();
long get_input_offset();

/* línea de datos hexadecimales */
char *format_line(char *base, long offset) {
    static char line[100];
    int pos = 0;
    
    // Dirección/offset
    pos += sprintf(line, "%08lx ", offset);
    
    // Datos hexadecimales (16 bytes por línea)
    for(int i = 0; i < BYTES_PER_LINE; i++) {
        if(offset + i < file_size) {
            pos += sprintf(line + pos, "%02x ", (unsigned char)base[offset + i]);
        } else {
            pos += sprintf(line + pos, "   "); // Espacios para archivos no múltiplos de 16
        }
        
        // Separador cada 4 bytes
        if((i + 1) % 4 == 0) {
            pos += sprintf(line + pos, " ");
        }
    }
    
    // Representación ASCII
    pos += sprintf(line + pos, "|");
    for(int i = 0; i < BYTES_PER_LINE; i++) {
        if(offset + i < file_size) {
            unsigned char c = base[offset + i];
            pos += sprintf(line + pos, "%c", isprint(c) ? c : '.');
        } else {
            pos += sprintf(line + pos, " ");
        }
    }
    pos += sprintf(line + pos, "|");
    
    return line;
}

/* Mapea el archivo a memoria */
char *map_file(char *file_path) {
    // Abre el archivo
    fd = open(file_path, O_RDONLY);
    if(fd == -1) {
        perror("Error abriendo el archivo");
        return NULL;
    }
    
    // tamaño del archivo
    struct stat st;
    fstat(fd, &st);
    file_size = st.st_size;
    
    // Mapea el archivo a memoria
    map = mmap(0, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if(map == MAP_FAILED) {
        close(fd);
        perror("Error mapeando el archivo");
        return NULL;
    }
    
    return map;
}

/* Muestra una página de datos (25 líneas) */
void display_page(long offset) {
    clear();
    
    
    if(offset < 0) offset = 0;
    
    
    if(offset >= file_size) offset = file_size - (file_size % BYTES_PER_LINE);
    
    current_offset = offset;
    
    
    mvprintw(0, 0, "Visor Hexadecimal - Archivo: %ld bytes - Offset: 0x%08lx", file_size, offset);
    mvprintw(1, 0, "Controles: Flechas[Navegar] Ctrl+<[Inicio] Ctrl+>[Fin] Ctrl+G[Ir a] Ctrl+X[Salir]");
    mvhline(2, 0, ACS_HLINE, COLS);
    
    // Mostrar líneas de datos
    for(int i = 0; i < LINES_PER_PAGE; i++) {
        long line_offset = offset + (i * BYTES_PER_LINE);
        if(line_offset < file_size) {
            char *line = format_line(map, line_offset);
            mvprintw(3 + i, 0, "%s", line);
        }
    }
    
    refresh();
}

/* Maneja la navegación por el archivo */
void handle_navigation() {
    int c;
    while((c = getch()) != 24) { // 24 = Ctrl+X para salir
        switch(c) {
            case KEY_UP:
                current_offset -= BYTES_PER_LINE;
                break;
            case KEY_DOWN:
                current_offset += BYTES_PER_LINE;
                break;
            case KEY_LEFT:
                current_offset -= BYTES_PER_LINE * LINES_PER_PAGE;
                break;
            case KEY_RIGHT:
                current_offset += BYTES_PER_LINE * LINES_PER_PAGE;
                break;
            case 1: // Ctrl+A (Inicio)
                current_offset = 0;
                break;
            case 5: // Ctrl+E (Fin)
                current_offset = file_size - (file_size % BYTES_PER_LINE);
                
                if(current_offset > file_size - (LINES_PER_PAGE * BYTES_PER_LINE)) {
                    current_offset = file_size - (LINES_PER_PAGE * BYTES_PER_LINE);
                }
                if(current_offset < 0) current_offset = 0;
                break;
            case 7: // Ctrl+G (Ir a posición)
                current_offset = get_input_offset();
                break;
            default:
                continue;
        }
        
        display_page(current_offset);
    }
}

/* Solicita al usuario una posición a la que saltar */
long get_input_offset() {
    echo();
    curs_set(1);
    
    char input[20];
    mvprintw(LINES - 1, 0, "Ir a offset (hex): ");
    getstr(input);
    
    noecho();
    curs_set(0);
    
    // Convertir entrada hexadecimal a long
    long offset;
    sscanf(input, "%lx", &offset);
    
    
    if(offset < 0) offset = 0;
    if(offset >= file_size) offset = file_size - 1;
    
    // Ajustar a múltiplo de BYTES_PER_LINE
    offset = offset - (offset % BYTES_PER_LINE);
    
    return offset;
}

/* Función principal */
int main(int argc, char const *argv[]) {
    // Verificar argumentos
    if(argc != 2) {
        printf("Uso: %s <archivo>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Inicializar ncurses
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    
    // Mapear el archivo
    if(map_file((char *)argv[1]) == NULL) {
        endwin();
        return EXIT_FAILURE;
    }
    
    // Mostrar primera página
    display_page(0);
    
    // Manejar navegación
    handle_navigation();
    
    // Limpiar
    if(munmap(map, file_size) == -1) {
        perror("Error al desmapear");
    }
    close(fd);
    endwin();
    
    return EXIT_SUCCESS;
}
