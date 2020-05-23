#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

//DECLARACIONES GLOBALES DE FUNCIONES SIN DEFINICIONÇ
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);

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

/* =========================================================== *
 *  Diferentes operaciones que se realizan sobre inodos  
 * =========================================================== */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

/* =========================================================== *
 *  Estructura necesaria para manejar inodos   
 * =========================================================== */
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

/* =========================================================== *
 *  Necesario para devolver inodos 
 * =========================================================== */
static struct inode *assoofs_get_inode(struct super_block *sb, int ino){
	
	//DECLARAMOS ESTRUCTURAS Y VARIABLES A USAR
	struct assoofs_inode_info *inode_info;
	struct inode *inode;

	//PASO 1, RECOLECTAMOS INFORMACION PERSISTENTE DEL INODO
	inode_info = assoofs_get_inode_info(sb, ino);

	//ASIGNAMOS PARAMETROS AL INODO, EL CUAL HEMOS CREADO
	inode = new_inode(sb);

	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;

	//DEPENDIENDO DEL TIPO DE ARCHIVO QUE SEA SE LE ASIGNAN UNAS OPERACIONES U OTRAS
	if (S_ISDIR(inode_info->mode))
		inode->i_fop = &assoofs_dir_operations;
	else if (S_ISREG(inode_info->mode))
		inode->i_fop = &assoofs_file_operations;
	else
		printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");

	//SETEAMOS EL TIEPO Y DEVOLVEMOS EL INODO
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_private = inode_info;
	return inode;
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
   printk(KERN_INFO "Lookup request\n");
   // return NULL;

	struct assoofs_inode_info *parent_info = parent_inode->i_private;		//SACAMOS LA INFORMACION PERSISTENTE
	struct super_block *sb = parent_inode->i_sb;							//SACAMOS EL SUPERBLOQUE
	struct buffer_head *bh;													//PARA LEER LA INFO DEL BLOQUE PADRE
	bh = sb_bread(sb, parent_info->data_block_number);
	int i;						//EN ESTE CASO EL BLOQUE DONDE TIENE LA INFO EL RAIZ

	printk(KERN_INFO "Lookup in: ino=%llu, b=%llu\n", parent_info->inode_no, parent_info->data_block_number);

	//voy a recorrerlos records dentro del directorio
	struct assoofs_dir_record_entry *record;
	record = (struct assoofs_dir_record_entry *)bh->b_data;
	for (i=0; i < parent_info->dir_children_count; i++) {
		printk(KERN_INFO "Have file: '%s' (ino=%llu)\n", record->filename, record->inode_no);
		if (!strcmp(record->filename, child_dentry->d_name.name)) {		//COMPROBAR INFO DEL FICHERO CON EL QUE NOS PASAN (0 SI SON IGUALES, !=0 SI NO SON IGUALES)
			struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Función auxiliar que obtine la información de un inodo a partir de su número de inodo.
			inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);	//OBTENER LA INFO DE ESE INODO
			d_add(child_dentry, inode);		//GUARDAR LA INFO EN MEMORIA DEL FICHERO
			return NULL;
		}
		record++;
	}

	printk(KERN_ERR "No inode found for the filename [%s]\n", child_dentry->d_name.name);
	return NULL;
}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode){

	//DECLARACION DE VARIABLES Y ESTRUCTURAS
    uint64_t inodes_count;
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_info;
	struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;

    inodes_count = assoofs_sb->inodes_count;   //obtenemos el count desde inode_info

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);		//Leer de disco el bloque con el almacen de inodos

    inode_info = (struct assoofs_inode_info *)bh->b_data;		//RECORRER ENTERO PARA APUNTAR AL FINAL
	inode_info += assoofs_sb->inodes_count;
	memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

	mark_buffer_dirty(bh);		//LO MARCAMOS COMO SUCIO
	sync_dirty_buffer(bh);		//SINCRONIZAMOS
	brelse(bh);					//liberamos memoria del bufferhead

	assoofs_sb->inodes_count++;		//
	assoofs_save_sb_info(sb);
}

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block){

	struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;		//OBTENEMOS LA INFORMACION PERSISTENTE DEL SUPERBLOQUE

	int i;					//RECORREMOS EL MAPA DE BITS EN BUSCA DE UN BLOQUE LIBRE (BIT = 1)
	for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
		if (assoofs_sb->free_blocks & (1 << i)){
			break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posición del primer bloque libre
		}else if(i == ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED -1){
			return -1; //Esto es cuando estamos evaluando el ultimo bit y esta lleno, pues devolvemos un -1 para notificar que no hay ninguno libre
		}

	//LA I DEBE SER SIEMPRE MENOR QUE EL NUMERO MAXIMO DE ARCHIVOS EN EL SISTEMA
	*block = i; // Escribimos el valor de i en la dirección de memoria indicada como segundo argumento en la función

	assoofs_sb->free_blocks &= ~(1 << i);
	assoofs_save_sb_info(sb);
	return 0;
}

void assoofs_save_sb_info(struct super_block *vsb){
	
	//DECLARAMOS LAS VARIABLES NECESARIAS PARA TRABAJAR
	struct buffer_head *bh;				//LEEMOS DEL DISCO
	struct assoofs_super_block *sb = vsb->s_fs_info; // Información persistente del superbloque en memoria
	bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);		//COGEMOS DE DONDE ESTA GUARDADO EN MEMORIA
	bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria

	//Escribir en disco
	mark_buffer_dirty(bh);		//PONEMOS EL BIT A SUCIO
	sync_dirty_buffer(bh);		//FORZAMOS LA SINCRONIZACION. Todos los cambios que esten en dirty, se trasladaran a disco
	brelse(bh);					//liberamos memoria del bufferhead
}

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    //return 0;

    //DECLARAMOS LAS ESTRUCTURAS Y LAS VARIABLES A USAR EN LA FUNCION
    struct inode *inode;
    struct super_block *sb;
    uint64_t count;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
	struct assoofs_dir_record_entry *dir_contents;
	struct buffer_head *bh;

    sb = dir->i_sb;			//OBTENEMOS UN PUNTERO AL SUPERBLOQUE DESDE DIR
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count;
    						//OBTENGO EL NUMERO DE INODOS DE LA INFORMACION PERSISTENTE DEL SUPERBLOQUE

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
    	printk(KERN_ERR "There are too much inodes in the filesystem. Erase some of them to create one more\n");
    	return -1;
    }

    inode = new_inode(sb);
    //inode->i_ino = (count + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);
    						//ASIGNO UN NUMERO AL NUEVO INODO A PARTIR DE COUNT
    inode->i_ino = count+1;

    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    //una vez asignado esto, vamos a almacenar el campo i_private del nodo, que contendrá datos persistentes que habrá que llevar a disco
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number); 		//Para asignarle un bloque vacío
    inode_info->mode = mode;
    inode_info->file_size = 0;
    inode->i_private = inode_info;

    inode->i_fop=&assoofs_file_operations;

    assoofs_add_inode_info(sb, inode_info);							//Para guardar la informacion persistente del nuevo nodo en disco

    //AHORA PASO 2
    //MODIFICAR EL CONTENIDO DEL DIRECTORIO PADRE AÑADIENDO UNA ENTRADA PARA EL NUEVO ARCHIVO O DIRECTORIO
	parent_inode_info = dir->i_private;
										//cogemos la informacion del directorio padre, que alguna funcion ya lo habra seteado
	bh = sb_bread(sb, parent_inode_info->data_block_number);
										//leemos del disco la parte de losdatos y la metemos en dir_contents. 
	dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
										//APUNTA AL PRINCIPIO DEL DIRECTORIO PADRE
	dir_contents += parent_inode_info->dir_children_count;
										//Para avanzar vamos sumando cosas. Cuando le sumamos el num de elementos avanzamos al final del directorio padre
	dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.
										//Colocamos lo que hemos hecho antes con inode info
	strcpy(dir_contents->filename, dentry->d_name.name);
										//Tenemos que copiar el nombre del fichero
	
	//Escribir en disco
	mark_buffer_dirty(bh);		//PONEMOS EL BIT A SUCIO
	sync_dirty_buffer(bh);		//FORZAMOS LA SINCRONIZACION. Todos los cambios que esten en dirty, se trasladaran a disco
	brelse(bh);					//liberamos memoria del bufferhead

	parent_inode_info->dir_children_count++;			//AUMENTAMOS EN UNO ELCONTADOR DE HIJOS DEL PADRE
	assoofs_save_inode_info(sb, parent_inode_info);		//CON ESTA FUNCION PASAMOS A DISCO LA INFORMACION DEL PADRE

	return 0;	//PARA INDICAR QUE TODO HA SALIDO BIEN
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    return 0;
}

/* =========================================================== *
 *  Operaciones sobre el superbloque   
 * =========================================================== */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/* =========================================================== *
 *  Información acerca de los inodos   
 * =========================================================== */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){

	//declaramos las estructuras y las variables a usar
	struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
	struct assoofs_inode_info *inode_info = NULL;
	struct assoofs_inode_info *buffer = NULL;
	int i;
	struct buffer_head *bh;

	//ACCEDEMOS A DISCO PARA LEER EL BLOQUE QUE CONTIENE EL ALMACEN DE INODOS
	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_info = (struct assoofs_inode_info *)bh->b_data;

	//RECORREMOS EL ALMACÉN DE INODOS EN BUSCA DEL inode_no
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
    
	//Declaración de las variables y elementos a usar
	struct inode *root_inode;									//AQUÍ VAMOS A GUARDAR EL INODO DEL ROOT
	struct buffer_head *bh; 									//Aquí tendremos toda la información de un bloque
    struct assoofs_super_block_info *assoofs_sb;				//Puntero al superbloque (info) 

    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *       LEER BLOQUES DE DISCO              * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);			//Llamada a sb_bread, superbloque block read (superbloque, numero de bloque del superbloque)
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; //Sacar el contenido del bloque (b_data)(Campo binario) (Meto en assoofs_sb la info del superbloque)
    			//Hacemos el cast para que se identifiquen los campos de info del superbloque	
    			//data es un void *				
    //brelse(bh);

    // 2.- Comprobar los parámetros del superbloque

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *         COMPROBAR PARAMETROS             * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    printk(KERN_INFO "The magic number obtained in disk is %llu\n", assoofs_sb->magic);
    //if(unlikely(assoofs_sb->magic != ASSOOFS_MAGIC)){
     if(assoofs_sb->magic != ASSOOFS_MAGIC){
    	printk(KERN_INFO "The filesystem that you want to mount is not assoofs. MAGIC_NUMBER mismatch.\n");
    	brelse(bh);
    	return -1;
    	//return -EPERM;
    }

    printk(KERN_INFO "The block size obtained in disk is %lld\n", assoofs_sb->block_size);
    //if(unlikely(assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)){
    if(assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE){
    	printk(KERN_INFO "assoofs seems to be formated using a wrong block size. BLOCK_SIZE mismatch.\n");
    	brelse(bh);
    	return -1;
    	//return -EPERM;
    }

    printk(KERN_INFO "Recognised assoofs filesystem. (MAGIC_NUMBER = %llu & BLOCK_SIZE = %lld)\n", assoofs_sb->magic, assoofs_sb->block_size);

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *    ASIGNAR PARAMETROS Y OPERACIONES      * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    sb->s_magic = ASSOOFS_MAGIC; 					//ASIGNAMOS EL NUMERO MAGICO AL NUEVO SUPERBLOQUE
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;	//ASIGNAMOS EL TAMAÑO DE BLOQUE
    sb->s_op = &assoofs_sops;						//ASIGNAMOS LAS OPERACIONES AL SUPERBLOQUE
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    
    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *    CREAR EL INODO RAIZ Y ASIGN. PARAM    * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */
    
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

    if(!sb->s_root){
    	brelse(bh);
    	return -1;
    	//return -ENOMEM;
    }

    brelse(bh);
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