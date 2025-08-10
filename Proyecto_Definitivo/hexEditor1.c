// hexEditor1.c
#include "hexEditor.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

// Crea una linea de dump (offset en hex, 16 bytes hex, ascii)
static void make_line_from_map(unsigned char *map, long map_size, long abs_offset, char *out, size_t outsz) {
    // abs_offset puede estar fuera de rango: manejamos truncado
    if (abs_offset < 0 || abs_offset >= map_size) {
        snprintf(out, outsz, "%08lx  -- fuera de rango --\n", (unsigned long)abs_offset);
        return;
    }

    int printed = 0;
    printed += snprintf(out + printed, outsz - printed, "%08lx ", (unsigned long)abs_offset);

    // imprime 16 bytes en hex (si hay menos, rellena)
    for (int i = 0; i < 16; i++) {
        long p = abs_offset + i;
        if (p < map_size) {
            printed += snprintf(out + printed, outsz - printed, "%02x ", map[p]);
        } else {
            printed += snprintf(out + printed, outsz - printed, "   ");
        }
    }

    printed += snprintf(out + printed, outsz - printed, " ");

    for (int i = 0; i < 16; i++) {
        long p = abs_offset + i;
        if (p < map_size) {
            unsigned char c = map[p];
            out[printed++] = (isprint(c) ? c : '.');
        } else {
            out[printed++] = ' ';
        }
    }
    out[printed++] = '\n';
    out[printed] = '\0';
}

// vista hex navegable: asume ncurses ya inicializado.
void hex_viewer_from_map(unsigned char *map, long map_size, off_t start_offset, size_t view_length) {
    if (!map) return;

    // Ajustes iniciales
    long offset = (start_offset < 0) ? 0 : (long)start_offset;
    if (offset >= map_size) offset = map_size>0 ? map_size-1 : 0;

    long end_offset;
    if (view_length == 0) end_offset = map_size;
    else {
        end_offset = offset + (long)view_length;
        if (end_offset > map_size) end_offset = map_size;
    }

    int screen_lines = LINES - 3; // reservamos 2 líneas para info/status
    if (screen_lines < 5) screen_lines = 5;

    long top_offset = offset - (offset % 16); // línea superior alineada a 16
    int cur_line = 0;
    int cur_col = offset % 16;

    // Dibujar inicial
    clear();
    char linebuf[256];
    for (int i = 0; i < screen_lines; i++) {
        long line_off = top_offset + i * 16;
        if (line_off >= end_offset) {
            move(i, 0);
            clrtoeol();
        } else {
            make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
            mvaddstr(i, 0, linebuf);
        }
    }

    // Posicionar cursor visual
    int cursor_x = 9 + (cur_col * 3); // 8 hex chars + space -> 9 offset
    int cursor_y = cur_line;
    move(cursor_y, cursor_x);
    refresh();

    int ch;
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    // loop de interacción
    while (1) {
        // barra de estado inferior
        mvprintw(LINES - 2, 0, "Offset: 0x%08lx  (%ld)  Top: 0x%08lx  End: 0x%08lx  q=salir, ENTER=mostrar offset", (unsigned long)(top_offset + cur_line*16 + cur_col), (long)(top_offset + cur_line*16 + cur_col), (unsigned long)top_offset, (unsigned long)end_offset);
        clrtoeol();
        refresh();

        ch = getch();
        if (ch == 'q' || ch == 'Q' || ch == 24) { // q o Ctrl-X sale
            break;
        } else if (ch == KEY_LEFT) {
            if (cur_col > 0) cur_col--;
            else {
                // si vamos al anterior byte
                if ( (top_offset + cur_line*16) > 0) {
                    if (cur_line > 0) cur_line--;
                    else {
                        top_offset -= 16;
                        if (top_offset < 0) top_offset = 0;
                        // redraw
                        for (int i = 0; i < screen_lines; i++) {
                            long line_off = top_offset + i * 16;
                            if (line_off >= end_offset) {
                                move(i, 0); clrtoeol();
                            } else {
                                make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
                                mvaddstr(i,0,linebuf);
                            }
                        }
                    }
                    cur_col = 15;
                }
            }
        } else if (ch == KEY_RIGHT) {
            long abspos = top_offset + cur_line*16 + cur_col;
            if (abspos + 1 < end_offset) {
                if (cur_col < 15) cur_col++;
                else {
                    // desplazamos una linea abajo
                    if (cur_line < screen_lines - 1) cur_line++;
                    else {
                        top_offset += 16;
                        if (top_offset + screen_lines*16 > end_offset) {
                            // clamp
                            if (top_offset + screen_lines*16 > end_offset) {
                                // nothing
                            }
                        }
                        // redraw
                        for (int i = 0; i < screen_lines; i++) {
                            long line_off = top_offset + i * 16;
                            if (line_off >= end_offset) {
                                move(i, 0); clrtoeol();
                            } else {
                                make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
                                mvaddstr(i,0,linebuf);
                            }
                        }
                    }
                    cur_col = 0;
                }
            }
        } else if (ch == KEY_UP) {
            if (cur_line > 0) cur_line--;
            else {
                if (top_offset >= 16) {
                    top_offset -= 16;
                    // redraw
                    for (int i = 0; i < screen_lines; i++) {
                        long line_off = top_offset + i * 16;
                        if (line_off >= end_offset) {
                            move(i, 0); clrtoeol();
                        } else {
                            make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
                            mvaddstr(i,0,linebuf);
                        }
                    }
                }
            }
        } else if (ch == KEY_DOWN) {
            if (cur_line < screen_lines - 1 && (top_offset + (cur_line+1)*16) < end_offset) {
                cur_line++;
            } else {
                // scroll down
                if (top_offset + screen_lines*16 < end_offset) {
                    top_offset += 16;
                    // redraw
                    for (int i = 0; i < screen_lines; i++) {
                        long line_off = top_offset + i * 16;
                        if (line_off >= end_offset) {
                            move(i, 0); clrtoeol();
                        } else {
                            make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
                            mvaddstr(i,0,linebuf);
                        }
                    }
                }
            }
        } else if (ch == 10 || ch == KEY_ENTER) {
            long cur_abs = top_offset + cur_line*16 + cur_col;
            mvprintw(LINES - 1, 0, "Offset seleccionado: 0x%08lx  (%ld)  (ENTER para continuar, q para salir)", (unsigned long)cur_abs, (long)cur_abs);
            clrtoeol();
            refresh();
            // Espera una tecla para continuar mostrando (no cierra automáticamente)
            int k = getch();
            if (k == 'q' || k == 'Q') break;
        } else if (ch == 'g' || ch == 'G') { // goto: pide offset decimal/hex
            echo();
            mvprintw(LINES - 1, 0, "Goto offset (dec o 0xhex): ");
            clrtoeol();
            char input[64];
            getnstr(input, sizeof(input)-1);
            noecho();
            long newoff = strtol(input, NULL, 0);
            if (newoff < 0) newoff = 0;
            if (newoff >= map_size) newoff = map_size - 1;
            top_offset = newoff - (newoff % 16);
            cur_line = 0;
            cur_col = newoff % 16;
            // redraw
            for (int i = 0; i < screen_lines; i++) {
                long line_off = top_offset + i * 16;
                if (line_off >= end_offset) {
                    move(i, 0); clrtoeol();
                } else {
                    make_line_from_map(map, map_size, line_off, linebuf, sizeof(linebuf));
                    mvaddstr(i,0,linebuf);
                }
            }
        }

        // actualizar cursor visual
        cursor_x = 9 + (cur_col * 3);
        cursor_y = cur_line;
        if (cursor_y >= screen_lines) cursor_y = screen_lines - 1;
        move(cursor_y, cursor_x);
        refresh();
    }

    // Al salir, limpia la parte de la pantalla que usó
    for (int i = 0; i < LINES; i++) {
        move(i, 0);
        clrtoeol();
    }
    refresh();
}