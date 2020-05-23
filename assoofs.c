#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    return 0;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Write request\n");
    return 0;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    printk(KERN_INFO "Iterate request\n");
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");
    return NULL;
}


static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    return 0;
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/* =========================================================== *
 *  Información acerca de los inodos   
 * =========================================================== */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){

	//ACCEDEMOS A DISCO PARA LEER EL BLOQUE QUE CONTIENE EL ALMACEN DE INODOS
	struct assoofs_inode_info *inode_info = NULL;
	struct buffer_head *bh;

	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_info = (struct assoofs_inode_info *)bh->b_data;

	//RECORREMOS EL ALMACÉN DE INODOS EN BUSCA DEL inode_no
	struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
	struct assoofs_inode_info *buffer = NULL;
	int i;
	for(i = 0; i < afs_sb->inodes_count; i++){
		if(inode_info->inode_no == inode_no){  //he encontrado el nodo por el que me preguntan
			buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);   //RESERVO MEMORIA EN EL KERNEL
			memcpy(buffer, inode_info, sizeof(*buffer));					   //COPIO EN BUFFER EL CONTENIDO DEL INODO 
			break;
		}
		inode_info++;  //sigo buscando en los inodos
	}

	//LIBERAR RECURSOS Y DEVOLVER LA INFORMACIÓN DEL INODO SI ESTABA EN EL ALMACÉN
	brelse(bh);			//LIBERAR EL FUFFER HEAD
	return buffer;		//DEVOLVER LA INFORMACIÓN DEL INODO QUE BUSCAMOS
						//SI NO LO ENCUENTRA DEVUELVE BUFFER = NULL
}

/* =========================================================== *
 *  INICIALIZACIÓN DEL SUPERBLOQUE    
 * =========================================================== */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *       LEER BLOQUES DE DISCO              * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    struct buffer_head *bh; 									//Aquí tendremos toda la información de un bloque
    struct assoofs_super_block_info *assoofs_sb;				//Puntero al superbloque (info) 
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);			//Llamada a sb_bread, superbloque block read (superbloque, numero de bloque del superbloque)
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; //Sacar el contenido del bloque (b_data)(Campo binario) (Meto en assoofs_sb la info del superbloque)
    			//Hacemos el cast para que se identifiquen los campos de info del superbloque					
    brelse(bh);

    // 2.- Comprobar los parámetros del superbloque

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *         COMPROBAR PARAMETROS             * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    if(assoofs_sb->magic == 0x20200406){
    	printk(KERN_INFO "CORRECT -> assoofs magic number is correct (0x20200406)\n");
    }else{
    	printk(KERN_INFO "WARNING -> assoofs magic number do not correspond to (0x20200406)\n");
    	return -1;
    }

    if(assoofs_sb->block_size == 4096){
    	printk(KERN_INFO "CORRECT -> assoofs block size is correct (4096)\n");
    }else{
    	printk(KERN_INFO "WARNING -> assoofs block size does not correspond to (4096)\n");
    	return -1;
    }

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *    ASIGNAR PARAMETROS Y OPERACIONES      * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    sb->s_magic = ASSOOFS_MAGIC; 						//ASIGNAMOS EL NUMERO MAGICO AL NUEVO SUPERBLOQUE
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;	//ASIGNAMOS EL TAMAÑO DE BLOQUE
    sb->s_op = &assoofs_sops;						//ASIGNAMOS LAS OPERACIONES AL SUPERBLOQUE
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    
    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *    CREAR EL INODO RAIZ Y ASIGN. PARAM    * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */

    struct inode *root_inode;			//AQUÍ VAMOS A GUARDAR EL INODO DEL ROOT
    root_inode = new_inode(sb);			//Inicializamos el nodo con parametros del supoerbloque
    inode_init_owner(root_inode, NULL, S_IFDIR); //Con esto seteamos propietario y permisos
    				//inodo, inodo padre, tipo de inodo (IFREG para ficheros regulares)

    //Ahora pasamos a asignarle informacion necesaria a nuestro inodo
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;		//NUMERO DE INODO DEL ROOT
    root_inode->i_sb = sb;									//PUNTERO AL SUPERBLOQUE
    root_inode->i_op = &assoofs_inode_ops;					//DIRECCION VARIABLE DE INODE OPERATIONS
    root_inode->i_fop = &assoofs_dir_operations;			//DIRECCION DE OPERACINOES DE DIRECTORIOS
    											//EN CASO DE SER FICHERO HAY OTRA FUNCION
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode);	//FECHAS Y HORA
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);	//INFORMACIÓN PERSISTENTE EN ELNODO

    //GUARDAMOS EL INODO EN EL ARBOL DE INODOS (ESPECIAL YA QUE ES EL ROOT)
    sb->s_root = d_make_root(root_inode);
    return 0;
}

/* =========================================================== *
 *  Montaje de dispositivos assoofs     
 * =========================================================== */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    printk(KERN_INFO "assoofs_mount request\n");
    struct dentry *ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);        //Función que monta el dispositivo
                                                                    //Esta función es la que se encargará de llenar nuestro superbloque con la información correspondiente
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    return ret;
}

/* =========================================================== *
 *  assoofs file system type     
 * =========================================================== */
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,                            //EL MODULO MISMO
    .name    = "assoofs",                              //IMPORTANTE: NOMBRE FILE SYSTEM
    .mount   = assoofs_mount,                          //CUANDO SE MONTE QUE HAGA ESTO
    .kill_sb = kill_litter_super,                      //CUANDO SE DESMONTE QUE HAGA ESTO
};

/**************************************************************
* FUNCION QUE SE EJECUTA CUANDO SE CARGA EL MODULO EN KERNEL
***************************************************************/

static int __init assoofs_init(void) {
    printk(KERN_INFO "assoofs_init request\n");
    int ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}

/**************************************************************
* FUNCION QUE SE EJECUTA CUANDO SE DESMONTA EL MODULO EN KERNEL
***************************************************************/

static void __exit assoofs_exit(void) {
    printk(KERN_INFO "assoofs_exit request\n");
    int ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}

module_init(assoofs_init);
module_exit(assoofs_exit);