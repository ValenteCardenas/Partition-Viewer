#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <curses.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* Variable global para mejor legibilidad */
int fd; // Archivo a leer


char *hazLinea(char *base, int dir) {
	char linea[100]; // La linea es mas pequeña
	int o=0;
	// Muestra 16 caracteres por cada linea
	o += sprintf(linea,"%08x ",dir); // offset en hexadecimal
	for(int i=0; i < 4; i++) {
		unsigned char a,b,c,d;
		a = base[dir+4*i+0];
		b = base[dir+4*i+1];
		c = base[dir+4*i+2];
		d = base[dir+4*i+3];
		o += sprintf(&linea[o],"%02x %02x %02x %02x ", a, b, c, d);
	}
	for(int i=0; i < 16; i++) {
		if (isprint(base[dir+i])) {
			o += sprintf(&linea[o],"%c",base[dir+i]);
		}
		else {
			o += sprintf(&linea[o],".");
		}
	}
	sprintf(&linea[o],"\n");

	return(strdup(linea));
}

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
  int ch,i=0;
  nodelay(stdscr, TRUE);
  while((ch = getch()) == ERR); /* Espera activa */
  ungetch(ch);
  while((ch = getch()) != ERR) {
    chars[i++]=ch;
  }
  /* convierte a numero con todo lo leido */
  int res=0;
  for(int j=0;j<i;j++) {
    res <<=8;
    res |= chars[j];
  }
  return res;
}

void imp_pan(char *map, int offset) {
  for(int i = 0; i < 25; i++) {
    char *l = hazLinea(&map[offset], offset + i*16);
    move(i, 0);
    addstr(l);
  }
  refresh();
}


int edita(char *filename) {
	
    /* Limpia pantalla */
    clear();

    /* Lee archivo */
    char *map = mapFile(filename);
    if (map == NULL) {
      exit(EXIT_FAILURE);
      }
    
    imp_pan(map, 0); // Muestra el mapa inicial
    

    int c = getch();
    char info[80];
    int x,y,px;
    x=y=0;
    int offset = 0;
	
    while (c != 24) { // Termina con CTRL-X
	    switch (c) {
		    case KEY_LEFT:
          x--;
			    break;
        case KEY_RIGHT:
          x++;
          break;
        case KEY_DOWN:
          y++;
          if (y > 24) {
            offset += 16; // Desplaza la pantalla
            imp_pan(map, offset);
          }
          break;
        case KEY_UP:
          y--;
          if (y < 0) {
            y = 0;
            offset -= 16; // Desplaza la pantalla
            imp_pan(map, offset);
          }
          break;
        //aHORA NECESITAMOS EL CASO CON CRTL-< PARA IR AL PRINCIPIO
        case 60: // CTRL-<
          x = 0;
          y = 0;
          offset = 0; // Vuelve al principio
          imp_pan(map, offset);
          break;
        case 62: // CTRL-> PARA IR AL FINAL
          x = 16;
          y = 24;
          offset = (lseek(fd, 0, SEEK_END) / 16) * 16; 
          imp_pan(map, offset);
          break;
        //Ahora el caso para ir a cualquier parte del archivo
        case 71: 
          move(25, 0);
          addstr("Offset: ");
          clrtoeol();

          char input[20];
          echo();
          getstr(input);
          noecho();
          
          int nuevo_offset = strtol(input, NULL, 0);
          if (nuevo_offset < 0) {
            nuevo_offset = 0;
          }
          if (nuevo_offset > lseek(fd, 0, SEEK_END)) {
            nuevo_offset = lseek(fd, 0, SEEK_END);
          }
          
          offset = nuevo_offset - (nuevo_offset % 16); // Alinea al inicio de la linea
          x = (nuevo_offset % 16); 
          y = (nuevo_offset / 16) % 25; 
          mvprintw(25, 0, "Offset: %d", offset);
          imp_pan(map, offset);
          break;
	    }
      sprintf(info, "Char: %d",c);
      move(26, 0);
      addstr(info);
      clrtoeol();

      if(x <=16)
        px = x * 3; // 16 caracteres por linea, cada uno ocupa 3 espacios
      else
        px = 48 + (x - 16);
      
      move(y,px+9);
	    c = getch();
    }

    if (munmap(map, fd) == -1) {
      perror("Error al desmapear");
    }
    close(fd);
    
   return 0;

}

int main(int argc, char const *argv[])
{
	initscr();
	raw(); 
	keypad(stdscr, TRUE);	/* Para obtener F1,F2,.. */
	noecho();

    /* El archivo se da como parametro */
    if (argc != 2) {
        printf("Se usa %s <archivo> \n", argv[0]);
        return(-1);
    }

    edita((char *)argv[1]);

    endwin();
    
    return 0;
}
