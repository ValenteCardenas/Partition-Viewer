#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>

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

int main(int argc, char const *argv[])
{
    if(argc != 2){
        printf("se usa %s \n", argv[0]);
        return (-1);
    }
    char *map = mapFile((char *)argv[1]);
    printf("La primera párticion tiene: \n");

    int inicio = 0x1BE;
    for(int i =0; i<16; i++){
        printf("%0x ", (unsigned char)map[inicio+i]);
    }
    printf("\n");

    inicio = 0x1C6;
    int iniP = *(int *)&map[inicio];

    printf("La particion inicia %0x\n", iniP);

    long part = iniP*512;

    printf("tiene ID: %s \n", &map[part+3]);
    short bytespersector = *(short *)&map[part+11];
    unsigned char sectorpercluster = *(unsigned char *)&map[part+13];

    printf("El sector tiene %d bytes y el cluster %d sectores\n", bytespersector, sectorpercluster);
}

