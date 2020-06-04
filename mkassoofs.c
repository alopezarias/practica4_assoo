#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "assoofs.h"

#define WELCOMEFILE_DATABLOCK_NUMBER (ASSOOFS_LAST_RESERVED_BLOCK + 1)
#define WELCOMEFILE_INODE_NUMBER (ASSOOFS_LAST_RESERVED_INODE + 1)

/**************************************************************
* Escribir en el superbloque
* Recibe el descriptor de fichero del directorio donde va
* a estar nuestro sistema de archivos
***************************************************************/

static int write_superblock(int fd) {
    struct assoofs_super_block_info sb = {
        .version = 1,                               //Versión
        .magic = ASSOOFS_MAGIC,                     //Número mágico
        .block_size = ASSOOFS_DEFAULT_BLOCK_SIZE,   //Tamaño de bloque
        .inodes_count = WELCOMEFILE_INODE_NUMBER,   //Ya sé que parto de 2 inodos (root y welcome)
        .free_blocks = (~0) & ~(15),                //Inicialización del mapa de bits (vídeo)
    };
    ssize_t ret;

    /**************************************************************
    * Función escribir superbloque (primeros 4096 bytes: 40 con los
    * campos definidos y 4056 de relleno)
    ***************************************************************/

    ret = write(fd, &sb, sizeof(sb));               
    if (ret != ASSOOFS_DEFAULT_BLOCK_SIZE) {
        printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
        return -1;
    }

    printf("Super block written succesfully.\n");
    return 0;
}

/**************************************************************
* Escribimos el inodo raíz (root)
***************************************************************/

static int write_root_inode(int fd) {
    ssize_t ret;

    struct assoofs_inode_info root_inode;

    root_inode.mode = S_IFDIR;                                      //Modo: directorio
    root_inode.inode_no = ASSOOFS_ROOTDIR_INODE_NUMBER;             //Número de inodo
    root_inode.data_block_number = ASSOOFS_ROOTDIR_BLOCK_NUMBER;    //Número de bloque
    root_inode.dir_children_count = 1;                              //Número de archivos que vamos a meter dentro

    /**************************************************************
    * Escribir el inodo raíz en memoria
    ***************************************************************/

    ret = write(fd, &root_inode, sizeof(root_inode));

    if (ret != sizeof(root_inode)) {
        printf("The inode store was not written properly.\n");
        return -1;
    }

    printf("root directory inode written succesfully.\n");
    return 0;
}

/**************************************************************
* Escribir el inodo de bienvenida en memoria
* A la función le pasamos el file descriptor y la struct del
* inodo del archivo
***************************************************************/

static int write_welcome_inode(int fd, const struct assoofs_inode_info *i) {
    off_t nbytes;
    ssize_t ret;

    /**************************************************************
    * Función escribir inodo de bienvenida
    ***************************************************************/

    ret = write(fd, i, sizeof(*i));
    if (ret != sizeof(*i)) {
        printf("The welcomefile inode was not written properly.\n");
        return -1;
    }
    printf("welcomefile inode written succesfully.\n");

    /**************************************************************
    * Meter espacio en blanco, para que no quede nada después del
    * archivo. Rellenamos como el superbloque
    ***************************************************************/

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - (sizeof(*i) * 2);     //Tam bloque menos lo que ocupan 2 inodos (raíz) y bienvenida
    ret = lseek(fd, nbytes, SEEK_CUR);                          //Avanzar con un puntero
    if (ret == (off_t)-1) {
        printf("The padding bytes are not written properly.\n");
        return -1;
    }

    printf("inode store padding bytes (after two inodes) written sucessfully.\n");
    return 0;
}

/**************************************************************
* Escribo una entrada de directorio, una pareja, duupla, nombre
* de fichero y directorio
***************************************************************/

int write_dirent(int fd, const struct assoofs_dir_record_entry *record) {
    ssize_t nbytes = sizeof(*record), ret;

    //Escribimos la entrada del directorio

    ret = write(fd, record, nbytes);
    if (ret != nbytes) {
        printf("Writing the rootdirectory datablock (name+inode_no pair for welcomefile) has failed.\n");
        return -1;
    }
    printf("root directory datablocks (name+inode_no pair for welcomefile) written succesfully.\n");

    //Rellenamos igual que en el anterior

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - sizeof(*record);
    ret = lseek(fd, nbytes, SEEK_CUR);
    if (ret == (off_t)-1) {
        printf("Writing the padding for rootdirectory children datablock has failed.\n");
        return -1;
    }
    printf("Padding after the rootdirectory children written succesfully.\n");
    return 0;
}

/**************************************************************
* Escribir lo que es la información del fichero dentro del 
* bloque
***************************************************************/

int write_block(int fd, char *block, size_t len) {
    ssize_t ret;

    //Escribimos

    ret = write(fd, block, len);
    if (ret != len) {
        printf("Writing file body has failed.\n");
        return -1;
    }
    printf("block has been written succesfully.\n");
    return 0;
}

int main(int argc, char *argv[])
{

/**************************************************************
* Para que cuando se formatee nos cree un archivo de 
* bienvenida, creamos lo siguiente
***************************************************************/

    int fd;
    ssize_t ret;

/**************************************************************
* Texto que va a contenter el archivo que vamos a situar al
* principio del File System
***************************************************************/

    char welcomefile_body[] = "Hola mundo, os saludo desde un sistema de ficheros ASSOOFS.\n";


/**************************************************************
* Creamos el inodo de bienvenida con los siguientes 
* parámetros
***************************************************************/

    struct assoofs_inode_info welcome = {
        .mode = S_IFREG,                                        //Para que sea un fichero regular
        .inode_no = WELCOMEFILE_INODE_NUMBER,                   //Numero de inodo (último inodo reservado + 1)
        .data_block_number = WELCOMEFILE_DATABLOCK_NUMBER,      //Numero de bloque (último bloque reservado + 1)
        .file_size = sizeof(welcomefile_body),                  //Campo file size, declaración estática
    };

/**************************************************************
* Creamos una entrada de directorio, para decir que el fichero
* está dentro de un directorio
***************************************************************/

    struct assoofs_dir_record_entry record = {
        .filename = "README.txt",
        .inode_no = WELCOMEFILE_INODE_NUMBER,
    };

/**************************************************************
* EL PROGRAMA NECESITA RECIBIR DOS ARGUMENTOS OBLIGATORIAMENTE:
*    ./programa & DIRECTORIO
*
* Si no recibe esos dos parametros, el programa no funcionna
***************************************************************/

    if (argc != 2) {
        printf("Usage: mkassoofs <device>\n");
        return -1;
    }

/**************************************************************
* EL PROGRAMA INTENTA ABRIR EL DIRECTORIO COMO SI FUERA UN FICH
*
* Si no lo consigue, nos salta un error
***************************************************************/

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("Error opening the device");
        return -1;
    }

// Cuando ya tenemos todo lo de arriba va a ejecutar una serie
//  de funciones. Si no consigue ejecutar alguno de los pasos
//  lo intentará más veces.

    ret = 1;
    do {
        if (write_superblock(fd))
            break;

        if (write_root_inode(fd))
            break;

        if (write_welcome_inode(fd, &welcome))
            break;

        if (write_dirent(fd, &record))
            break;

        if (write_block(fd, welcomefile_body, welcome.file_size))
            break;

        ret = 0;
    } while (0);

    close(fd);
    return ret;
} 