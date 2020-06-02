#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

//Configuramos unas macros para la licencia 
#define DRIVER_AUTHOR "Angel Lopez Arias"
#define DRIVER_DESC   "An assoofs sample"

//Voy a configurar algunas variables para hacer printf con algun color, para que las trazas queden mas visuales

#define R_C "\x1b[0m"		//Reset color
#define R 	"\x1b[31m"		//Red
#define G   "\x1b[32m"		//Green
#define Y	"\x1b[33m"		//Yellow
#define B   "\x1b[34m"		//Blue

//Configuramos unos mutex para proteger el acceso al superbloque y al almacen de inodos
static DEFINE_MUTEX(assoofs_sb_lock);
static DEFINE_MUTEX(assoofs_inodes_block_lock);

//Vamos a configurar una chache de inodos como variable global
static struct kmem_cache *assoofs_inode_cache;

/* ++++++++++++++++++++++++++++++++++++++++++++ /
 *       DECLARACION VARIABLES                 *
/ ++++++++++++++++++++++++++++++++++++++++++++ */
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search);

/* =========================================================== *
 *  OPERACIONES SOBRE FICHEROS DEL SO    
 * =========================================================== */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

/* =========================================================== *
 *  OPERACION SOBRE FICHEROS --> READ    
 * =========================================================== */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    
    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *     DECLARACION DE VARIABLES                * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
	char *buffer;
	int nbytes;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Read request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */	

	//obtenemos la informacion persistente del nodo
    inode_info = filp->f_path.dentry->d_inode->i_private; 

    //Para comprobar si hemos o no llegado al final del fichero
    if (*ppos >= inode_info->file_size){
    	printk(KERN_INFO R "READ: EOF reached\n" R_C);
    	return 0;   
    }

    //Leemos del disco la información que nos han pedido
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
    //copiamos en buf el contenido del fichero que hemos leido
	buffer = (char *)bh->b_data;

	// Hay que comparar len con el tamanio del fichero por si llegamos al final del fichero
	nbytes = min((size_t) inode_info->file_size, len); 
	printk(KERN_INFO Y "TEXT READ: %s\n" R_C, buffer);
	copy_to_user(buf, buffer, nbytes);

	*ppos += nbytes;		//incrementamos ppos
	brelse(bh);				//liberamos memoria del bufferhead
	printk(KERN_INFO Y "BYTES READ: %d\n" R_C, nbytes);		//imprimios una traza de lectura
	return nbytes;			//devolvemos el numero de bytes leidos
}

/* =========================================================== *
 *  OPERACION SOBRE FICHEROS --> WRITE   
 * =========================================================== */
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    
    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *     DECLARACION DE VARIABLES                * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
	char *buffer;
	struct super_block *sb;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Write request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//mediante el file flip accedemos a la informacion del superbloque
	sb = filp->f_path.dentry->d_inode->i_sb;

	//obtenemos la informacion persistente del nodo
    inode_info = filp->f_path.dentry->d_inode->i_private;

    //leemos informacion del superbloque
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);

    //Copiamos en disco la información que nos han dado
    buffer = (char *)bh->b_data;
    buffer += *ppos;
	if(copy_from_user(buffer, buf, len)){
		brelse(bh);
		printk(KERN_ERR "Error copying file contents from the userspace buffer to the kernel");
		return -1;
	}

	printk(KERN_INFO Y "TEXT WRITTEN: %s\n" R_C, buffer);
	printk(KERN_INFO Y "BYTES WRITTEN: %ld\n" R_C, len);		//imprimios una traza de lectura

	*ppos+=len; //actualizamos el puntero ppos

	mark_buffer_dirty(bh);		//LO MARCAMOS COMO SUCIO
	sync_dirty_buffer(bh);		//SINCRONIZAMOS
	brelse(bh);					//liberamos memoria del bufferhead

	//-----------------------  MUTEX DEL ALMACEN DE INODOS  -------------------------//
    mutex_lock_interruptible(&assoofs_inodes_block_lock);

	inode_info->file_size = *ppos;				//actualizamos la informacion del tamaño
	assoofs_save_inode_info(sb, inode_info);	//guardamos la informacion del inodo en disco
	
	mutex_unlock(&assoofs_inodes_block_lock);

	return len;									//devolvemos el numero de bytes que hemos escrit0
}

/* =========================================================== *
 *  OPERACIONES SOBRE DIRECTORIOS   
 * =========================================================== */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

/* =========================================================== *
 *  OPERACION DIRECTORIO --> ITERATE   
 * =========================================================== */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    
    /* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
    struct inode *inode;
	struct super_block *sb;
	struct assoofs_inode_info *inode_info;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;
	int i;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Iterate request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//Antes de nada comprobamos si el directorio ya se encuentra en la cache para evitar bucles infinitos
	if (ctx->pos){
		printk(KERN_INFO R "The directory already is at the cache\n" R_C);
		return 0;
	} 

	//Sacamos del descriptor de archivos todo lo que necesitamos:
	inode = filp->f_path.dentry->d_inode;		//el inodo
	sb = inode->i_sb;							//el superbloque
	inode_info = inode->i_private;				//la informacion del inodo

	if ((!S_ISDIR(inode_info->mode))){
		printk(KERN_ERR "The file was supposed to be a directory, but it is not\n");
		return -1;  //Si por algun casual el modo del indodo no es de directorio, nos salimos
	}

	bh = sb_bread(sb, inode_info->data_block_number);			//leemos en disco
	record = (struct assoofs_dir_record_entry *)bh->b_data;		//el contenido que queriamos estaba en data y hay que castearlo

	printk(KERN_INFO "Directory: reading all the dir_record_entries\n");

	for (i = 0; i < inode_info->dir_children_count; i++) {		//recorremos todos los hijos que tiene el directorio
		
		//----------------------------  PARTE NECESARIA PARA QUE EL REMOVE FUNCIONE BIEN ---------------------------------------- //
		if(record->state_flag == ALIVE){
			printk(KERN_INFO Y "FILE: Name: '%s' Inode_Number: %llu State_Flag: " G "ALIVE\n" R_C, record->filename, record->inode_no);
			dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);		//Inicializando el contexto con los datos del directorio
		}else{
			printk(KERN_INFO Y "FILE: Name: '%s' Inode_Number: %llu State_Flag: " R "REMOVED\n" R_C, record->filename, record->inode_no);
			i--;			//con esto conseguimos que si hay algun nodo que no esta vivo, y deberia estar muerto
							//que no lo muestre y que no cuente esa iteracion como si fuera uno de sus hijos
		}
		ctx->pos += sizeof(struct assoofs_dir_record_entry);										//Incrementamos el valor del puntero pos, se inicializa con 0, pero lo voy a aumentar tanto como ocupe un record entry
		record++;																				//Aumento el valor del puntero record
	}

	//Liberamos la memoria del bufferhead
	brelse(bh);

	//Si todo ha ido bien salimos y devolvemos un cero
	return 0;
}

/* =========================================================== *
 *  OPERACIONES SOBRE INODOS 
 * =========================================================== */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int assoofs_remove(struct inode *dir, struct dentry *dentry);
static int assoofs_move(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int num);

/* =========================================================== *
 *  OPERACIONES DE INODOS (INODE_OPS)   
 * =========================================================== */
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
    .unlink = assoofs_remove,
    .rename = assoofs_move,
};

/* =========================================================== *
 *  BORRADO DE UN ARCHIVO    
 * =========================================================== */
/* 
 * El procedimiento que voy a seguir para eliminar archivos es:
 * 
 * 		Contamos con que ya nos pasan el inodo como parametro el inodo y la dentry del inodo
 * 
 * 		Para borrar un archivo hay que:
 *			- Borrar el contador del superbloque
 * 			- Borrar el contador del padre y su dir record entry
 *			- Borrar el inodo y actualizar el mapa de bits
 *
 * 		Vamos a ir paso a paso especificando que hacemos para no liarnos
 * 
 */
static int assoofs_remove(struct inode *dir, struct dentry *dentry) {

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct super_block *sb;
	struct assoofs_super_block_info *super_info;
	struct inode *inode;
	struct assoofs_inode_info *parent_inode_info, *inode_info;
	struct assoofs_dir_record_entry *record;
	struct buffer_head *bh;
	int i;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Remove file request\n" R_C); 

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	inode = dentry->d_inode;		//cogemos el nodo del dentry
	inode_info = inode->i_private;	//cogemos la informacion del inodo del campo i_private
	inode_info->state_flag == REMOVED;
	printk(KERN_INFO "Remove: inode to remove: %llu\n", inode_info->data_block_number);		//informamos con una traza de que inodo vamos a eliminar

	//Extraemos el superbloque del nodo del padre
	printk(KERN_INFO "Remove: obtaining the superblock\n");
	sb = dir->i_sb;
	
	//Decrementamos el contador de inodos del superbloque
	printk(KERN_INFO "Remove: decreasing superblock inodes_count\n");
	super_info = sb->s_fs_info;
	super_info->inodes_count--;
	//super_info->inodes_removed_count++;

	//Antes de guardar el superbloque actualizamos el mapa de bits
	printk(KERN_INFO "Remove: changing the bitmap\n");
	printk(KERN_INFO "BITMAP ORIGINAL: %llu\n", super_info->free_blocks);
	super_info->free_blocks |= (1 << inode_info->data_block_number);
	printk(KERN_INFO "BITMAP CHANGED: %llu\n", super_info->free_blocks);

	//guardamos la informacion persistente en el superbloque
	printk(KERN_INFO "Remove: saving sb\n");
	assoofs_save_sb_info(sb);

	//decrementamos el contador del padre
	parent_inode_info = dir->i_private;
	printk(KERN_INFO "Remove: reducing the parent's children\n");
	printk(KERN_INFO "CHILDREN: %llu\n", parent_inode_info->dir_children_count);
	parent_inode_info->dir_children_count--;
	printk(KERN_INFO "NEW CHILDREN: %llu\n", parent_inode_info->dir_children_count);
	printk(KERN_INFO "Remove: saving the parent_inode_info to memory\n");
	assoofs_save_inode_info(sb, parent_inode_info);
	assoofs_save_inode_info(sb, inode_info);   //para guardar el atributo del inode_info->state_flag a REMOVED

	//actualizamos el campo de tiempo del inodo
	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);

	//modificar la record entry para que aparezca como eliminado
	bh = sb_bread(sb, parent_inode_info->data_block_number);//PARA LEER LA INFO DEL BLOQUE PADRE

	printk(KERN_INFO "SB_BREAD in: ino=%llu, b=%llu\n", parent_inode_info->inode_no, parent_inode_info->data_block_number);

	//voy a recorrerlos records dentro del directorio
	record = (struct assoofs_dir_record_entry *)bh->b_data;

	for (i=0; i < parent_inode_info->dir_children_count; i++) {

		//----------------------------  PARTE NECESARIA PARA QUE EL REMOVE FUNCIONE BIEN ---------------------------------------- //
		if(record->state_flag == ALIVE){

			printk(KERN_INFO G "ALIVE FILE: '%s' (ino=%llu)\n" R_C, record->filename, record->inode_no);

			if(record->inode_no == dentry->d_inode->i_ino){
				record->state_flag = REMOVED;
				printk(KERN_INFO B "NEW REMOVED FILE: '%s' (ino=%llu)\n" R_C, record->filename, record->inode_no);
				break;
			}
		}else{
			printk(KERN_INFO R "ALREADY REMOVED: '%s' (ino=%llu)\n" R_C, record->filename, record->inode_no);
			i--;			//con esto conseguimos que si hay algun nodo que no esta vivo, y deberia estar muerto
							//que no lo muestre y que no cuente esa iteracion como si fuera uno de sus hijos
		}
		record++;
	}

	d_drop(dentry);
	brelse(bh);
    
	return 0;	//PARA INDICAR QUE TODO HA SALIDO BIEN
}

/* =========================================================== *
 *  MOVIMIENTO DE UN ARCHIVO DE SITIO
 * =========================================================== */
/* 
 * El metodo para mover a un archivo de sitio consiste en lo siguiente
 * 
 * 		- Buscar el archivo en el almacen de inodos
 * 		- Copiar su dir_record_entry dentro del record del nuevo directorio
 *			- Decrementar los hijos del anterior directorio y aumentar los del siguiente
 * 		- Aplicar remove sobre el anterior enlace al archivo
 * 
 * 		No va a hacer falta crear un nuevo nodo, ni decrementar el superbloque
 * 
 */
static int assoofs_move(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int num){

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Move inode request\n" R_C);

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
	printk(KERN_INFO G "OLD inode inode: %ld\n" R_C, old_dir->i_ino);
	printk(KERN_INFO G "OLD Dentry inode: %ld\n" R_C, old_dentry->d_inode->i_ino);
	printk(KERN_INFO G "NEW inode inode: %ld\n" R_C, new_dir->i_ino);
	printk(KERN_INFO G "NEW Dentry inode: %ld\n" R_C, new_dentry->d_inode->i_ino);
	printk(KERN_INFO G "Number: %d\n" R_C, num);
	return 0;
}

/* =========================================================== *
 *  CONSECUCIÓN DE LOS INODOS QUE NECESITAMOS
 * =========================================================== */
static struct inode *assoofs_get_inode(struct super_block *sb, int ino){

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct assoofs_inode_info *inode_info;
	struct inode *inode;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Get inode request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//PASO 1, recolectamos la información persistente del nodo
	inode_info = assoofs_get_inode_info(sb, ino);

	//Creamos el inodo
	inode = new_inode(sb);

	//Asignamos parametros al inodo que hemos creado
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;

	//DEPENDIENDO DEL TIPO DE ARCHIVO QUE SEA SE LE ASIGNAN UNAS OPERACIONES U OTRAS
	if (S_ISDIR(inode_info->mode)){
		inode->i_fop = &assoofs_dir_operations;
		printk(KERN_INFO "Is a directory\n");
	}else if (S_ISREG(inode_info->mode)){
		inode->i_fop = &assoofs_file_operations;
		printk(KERN_INFO "Is a file\n");
	}else{
		printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");
	}

	//SETEAMOS EL TIEPO Y DEVOLVEMOS EL INODO
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_private = inode_info;
	return inode;
}

/* =========================================================== *
 *  BUSQUEDA DE UN ARCHIVO EN UN DIRECTORIO    
 * =========================================================== */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct assoofs_inode_info *parent_info;		
	struct super_block *sb;							
	struct buffer_head *bh;	
	struct assoofs_dir_record_entry *record;
	struct inode *inode;												
	int i;						//EN ESTE CASO EL BLOQUE DONDE TIENE LA INFO EL RAIZ

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Lookup request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	parent_info = parent_inode->i_private;		//SACAMOS LA INFORMACION PERSISTENTE
	sb = parent_inode->i_sb;					//SACAMOS EL SUPERBLOQUE
	bh = sb_bread(sb, parent_info->data_block_number);//PARA LEER LA INFO DEL BLOQUE PADRE

	printk(KERN_INFO "Lookup in: ino=%llu, b=%llu\n", parent_info->inode_no, parent_info->data_block_number);

	//voy a recorrerlos records dentro del directorio
	record = (struct assoofs_dir_record_entry *)bh->b_data;
	for (i=0; i < parent_info->dir_children_count; i++) {

		//----------------------------  PARTE NECESARIA PARA QUE EL REMOVE FUNCIONE BIEN ---------------------------------------- //
		if(record->state_flag == ALIVE){
			printk(KERN_INFO "Have file: '%s' (ino=%llu)\n", record->filename, record->inode_no);
			if (!strcmp(record->filename, child_dentry->d_name.name)) {		//COMPROBAR INFO DEL FICHERO CON EL QUE NOS PASAN (0 SI SON IGUALES, !=0 SI NO SON IGUALES)
				inode = assoofs_get_inode(sb, record->inode_no); //assoofs_dir_record_entry Función auxiliar que obtine la información de un inodo a partir de su número de inodo.
				inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);	//OBTENER LA INFO DE ESE INODO
				d_add(child_dentry, inode);		//GUARDAR LA INFO EN MEMORIA DEL FICHERO
				return NULL;
			}
		}else{
			printk(KERN_INFO R "Have file removed: '%s' (ino=%llu)\n" R_C, record->filename, record->inode_no);
			i--;			//con esto conseguimos que si hay algun nodo que no esta vivo, y deberia estar muerto
							//que no lo muestre y que no cuente esa iteracion como si fuera uno de sus hijos
		}
		record++;
	}

	printk(KERN_ERR "No inode found for the filename [%s]\n", child_dentry->d_name.name);
	return NULL;
}

/* =========================================================== *
 *  BÚSQUEDA DE INFORMACIÓN DEL INODO    
 * =========================================================== */
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search){
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	uint64_t count = 0;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Search inode info request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//BUSCAMOS EL ALMACEN DE INODOS HASTA ENCONTRAR LOS DATOS DEL INDOO SEARCH
	while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count) {
		//--------------  NECESARIO PARA EL REMOVE ---------------//
		if(start->state_flag == REMOVED){
			count--;
		}
		count++;
		start++;
	}

	if (start->inode_no == search->inode_no){
		printk(KERN_INFO "Node_information (ino_no: %llu) found\n", start->inode_no);
		return start;  //si es el nodo que estabamos buscando lo devolvemos
	}
	else{
		printk(KERN_INFO "Node_information not found\n");
		return NULL; //si no, devolvemos null en senial de que no lo hemos encontrado
	}
}

/* =========================================================== *
 *  GUARDADO DE LA INFORMACION DE UN INODO    
 * =========================================================== */
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info){
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_pos;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Save inode info request\n" R_C);

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//ACCEDEMOS A DISCO PARA LEER EL BLOQUE QUE CONTIENE EL ALMACEN DE INODOS
	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	//--------------------------  MUTEX DEL SUPER BLOQUE  ---------------------------//
    mutex_lock_interruptible(&assoofs_sb_lock);

	inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);  //BUSCAMOS LA POSICION DE UN NODO EN CONCRETO

	memcpy(inode_pos, inode_info, sizeof(*inode_pos));    //METEMOS LA INFORMACION EN LA INFORMACION DEL INODO
	mark_buffer_dirty(bh);		//LO MARCAMOS COMO SUCIO
	sync_dirty_buffer(bh);		//SINCRONIZAMOS
	printk(KERN_INFO "Node_Info saved correctly\n");
	brelse(bh);					//liberamos memoria del bufferhead

	mutex_unlock(&assoofs_sb_lock);

	return 0;
}

/* =========================================================== *
 *  ADICION DE INFORMACION A UN NODO   
 * =========================================================== */
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode){
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
    uint64_t inodes_count;
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_info;
	struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
	int i;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Add inode info request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

    inodes_count = assoofs_sb->inodes_count;   //obtenemos el count desde inode_info

    //-----------------------  MUTEX DEL ALMACEN DE INODOS  -------------------------//
    mutex_lock_interruptible(&assoofs_inodes_block_lock);

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);		//Leer de disco el bloque con el almacen de inodos

    inode_info = (struct assoofs_inode_info *)bh->b_data;		//RECORRER ENTERO PARA APUNTAR AL FINAL
	
	//--------------------------  MUTEX DEL SUPER BLOQUE  ---------------------------//
    mutex_lock_interruptible(&assoofs_sb_lock);

	//inode_info += assoofs_sb->inodes_count;
    //-------------------------------  NECESARIO PARA EL REMOVE -------------------------//
	for(i=0; i < assoofs_sb->inodes_count; i++){ //para cuando guardemos la informacion del nuevo inodo en el sitio correcto
		if(inode_info->state_flag == REMOVED){
			i--;
		}
		inode_info ++;
	}

	memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

	mark_buffer_dirty(bh);		//LO MARCAMOS COMO SUCIO
	sync_dirty_buffer(bh);		//SINCRONIZAMOS
	printk(KERN_INFO "Node_Info added correctly\n");
	brelse(bh);					//liberamos memoria del bufferhead

	assoofs_sb->inodes_count++;		
	assoofs_save_sb_info(sb);

	mutex_unlock(&assoofs_sb_lock);
	mutex_unlock(&assoofs_inodes_block_lock);
}

/* =========================================================== *
 *  CONSECUCION DE UN BLOQUE LIBRE EN EL SUPERBLOQUE    
 * =========================================================== */
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block){
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct assoofs_super_block_info *assoofs_sb;		
	int i;					

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Get a free block request\n" R_C);

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//--------------------------  MUTEX DEL SUPER BLOQUE  ---------------------------//
    mutex_lock_interruptible(&assoofs_sb_lock);

	assoofs_sb = sb->s_fs_info;		//OBTENEMOS LA INFORMACION PERSISTENTE DEL SUPERBLOQUE

	//RECORREMOS EL MAPA DE BITS EN BUSCA DE UN BLOQUE LIBRE (BIT = 1)
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

	mutex_unlock(&assoofs_sb_lock);

	return 0;
}

/* =========================================================== *
 *  GUARDADO DE INFORMACION EN EL SUPERBLOQUE    
 * =========================================================== */
void assoofs_save_sb_info(struct super_block *vsb){
	
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct buffer_head *bh;				//LEEMOS DEL DISCO
	struct assoofs_super_block *sb;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Save sb info request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	sb = vsb->s_fs_info; // Información persistente del superbloque en memoria

	bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);		//COGEMOS DE DONDE ESTA GUARDADO EN MEMORIA
	bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria

	//Escribir en disco
	mark_buffer_dirty(bh);		//PONEMOS EL BIT A SUCIO
	sync_dirty_buffer(bh);		//FORZAMOS LA SINCRONIZACION. Todos los cambios que esten en dirty, se trasladaran a disco
	printk(KERN_INFO "Super_Block_Info saved correctly\n");
	brelse(bh);					//liberamos memoria del bufferhead
}

/* =========================================================== *
 *  CREACION DE UN ARCHIVO    
 * =========================================================== */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
    struct inode *inode;
    struct super_block *sb;
    uint64_t count;
    //uint64_t removed;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
	struct assoofs_dir_record_entry *dir_contents;
	struct buffer_head *bh;
	int i;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "New file request\n" R_C); 

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

    sb = dir->i_sb;			//OBTENEMOS UN PUNTERO AL SUPERBLOQUE DESDE DIR

    //-----------------------  MUTEX DEL ALMACEN DE INODOS  -------------------------//
    mutex_lock_interruptible(&assoofs_inodes_block_lock);

    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count;
    						//OBTENGO EL NUMERO DE INODOS DE LA INFORMACION PERSISTENTE DEL SUPERBLOQUE
    //necesario para el remove
    //removed = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_removed_count;
    						

    mutex_unlock(&assoofs_inodes_block_lock);

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
    	printk(KERN_ERR "There are too much inodes in the filesystem. Erase some of them to create one more\n");
    	return -1;
    }

    printk(KERN_INFO "Node created correctly\n");

    inode = new_inode(sb);
    //inode->i_ino = (count + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);
    						//ASIGNO UN NUMERO AL NUEVO INODO A PARTIR DE COUNT

    //ESTA ES LA MANERA SIN CACHE DE INODOS
    //inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    	//ESTA ES LA MANERA CON CACHE DE INODOS
    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);	

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);  //Para asignarle un bloque vacío

    inode->i_ino = inode_info->data_block_number-1;//count+1+removed;

    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    //una vez asignado esto, vamos a almacenar el campo i_private del nodo, que contendrá datos persistentes que habrá que llevar a disco
    
    	
    printk(KERN_INFO "Space in cache reserved correctly\n");

    inode_info->inode_no = inode->i_ino;		
    inode_info->mode = mode;
    inode_info->file_size = 0;
    inode_info->state_flag = ALIVE;
    inode->i_private = inode_info;
    inode->i_fop=&assoofs_file_operations;
    inode_init_owner(inode, dir, mode);
    d_add(dentry, inode);

    
    assoofs_add_inode_info(sb, inode_info);							//Para guardar la informacion persistente del nuevo nodo en disco

    //AHORA PASO 2
    //MODIFICAR EL CONTENIDO DEL DIRECTORIO PADRE AÑADIENDO UNA ENTRADA PARA EL NUEVO ARCHIVO O DIRECTORIO
	parent_inode_info = dir->i_private;
										//cogemos la informacion del directorio padre, que alguna funcion ya lo habra seteado
	bh = sb_bread(sb, parent_inode_info->data_block_number);
										//leemos del disco la parte de losdatos y la metemos en dir_contents. 
	dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
										//APUNTA AL PRINCIPIO DEL DIRECTORIO PADRE +=

	//--------------------------------   REMOVE NECESARIO  ----------------------------//
	for(i=0; i < parent_inode_info->dir_children_count; i++){
		if(dir_contents->state_flag == REMOVED){
			i--;
		}
		dir_contents++;
	}
	//dir_contents += parent_inode_info->dir_children_count

	//dir_contents += parent_inode_info->dir_children_count;
										//Para avanzar vamos sumando cosas. Cuando le sumamos el num de elementos avanzamos al final del directorio padre
	dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.
										//Colocamos lo que hemos hecho antes con inode info
	strcpy(dir_contents->filename, dentry->d_name.name);
										//Tenemos que copiar el nombre del fichero
	
	//----------------------------  PARTE NECESARIA PARA QUE EL REMOVE FUNCIONE BIEN ---------------------------------------- //
	dir_contents->state_flag = ALIVE;

	//Escribir en disco
	mark_buffer_dirty(bh);		//PONEMOS EL BIT A SUCIO
	sync_dirty_buffer(bh);		//FORZAMOS LA SINCRONIZACION. Todos los cambios que esten en dirty, se trasladaran a disco
	printk(KERN_INFO "Node created and stored correctly\n");
	brelse(bh);					//liberamos memoria del bufferhead

	//-----------------------  MUTEX DEL ALMACEN DE INODOS  -------------------------//
    mutex_lock_interruptible(&assoofs_inodes_block_lock);

	parent_inode_info->dir_children_count++;			//AUMENTAMOS EN UNO ELCONTADOR DE HIJOS DEL PADRE
	assoofs_save_inode_info(sb, parent_inode_info);		//CON ESTA FUNCION PASAMOS A DISCO LA INFORMACION DEL PADRE

	mutex_unlock(&assoofs_inodes_block_lock);

	return 0;	//PARA INDICAR QUE TODO HA SALIDO BIEN
}

/* =========================================================== *
 *  CREACION DE UN DIRECTORIO    
 * =========================================================== */
static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *i=0
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
    struct inode *inode;
    struct super_block *sb;
    uint64_t count;
    //uint64_t removed;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
	struct assoofs_dir_record_entry *dir_contents;
	struct buffer_head *bh;
	int i;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "New directory request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

    sb = dir->i_sb;			//OBTENEMOS UN PUNTERO AL SUPERBLOQUE DESDE DIR
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count;
    						//OBTENGO EL NUMERO DE INODOS DE LA INFORMACION PERSISTENTE DEL SUPERBLOQUE
    //necesario para el remove
    //removed = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_removed_count;

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
    	printk(KERN_ERR "There are too much inodes in the filesystem. Erase some of them to create one more\n");
    	return -1;
    }

    inode = new_inode(sb);
    printk(KERN_INFO "Node created correctly\n");

    //ESTA ES LA MANERA SIN CACHE DE INODOS
    //inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    	//ESTA ES LA MANERA CON CACHE DE INODOS
    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);  //Para asignarle un bloque vacío

    printk(KERN_INFO "Space in cache reserved correctly\n");

    
    //inode->i_ino = (count + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);
    						//ASIGNO UN NUMERO AL NUEVO INODO A PARTIR DE COUNT
    inode->i_ino = inode_info->data_block_number-1;//count+1+removed;

    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    //una vez asignado esto, vamos a almacenar el campo i_private del nodo, que contendrá datos persistentes que habrá que llevar a disco
    
    inode_info->inode_no = inode->i_ino;		
    //inode_info->mode = mode;
    inode_info->file_size = 0;
    inode_info->state_flag = ALIVE;
    inode->i_private = inode_info;
    inode->i_fop=&assoofs_dir_operations;
	inode_info->dir_children_count = 0;
	inode_info->mode = S_IFDIR | mode;


    //inode->i_fop=&assoofs_file_operations;
    inode_init_owner(inode, dir, inode_info->mode);
    d_add(dentry, inode);

    assoofs_add_inode_info(sb, inode_info);							//Para guardar la informacion persistente del nuevo nodo en disco

    //AHORA PASO 2
    //MODIFICAR EL CONTENIDO DEL DIRECTORIO PADRE AÑADIENDO UNA ENTRADA PARA EL NUEVO ARCHIVO O DIRECTORIO
	parent_inode_info = dir->i_private;
										//cogemos la informacion del directorio padre, que alguna funcion ya lo habra seteado
	bh = sb_bread(sb, parent_inode_info->data_block_number);
										//leemos del disco la parte de losdatos y la metemos en dir_contents. 
	dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
										//APUNTA AL PRINCIPIO DEL DIRECTORIO PADRE
	
	//--------------------------------   REMOVE NECESARIO  ----------------------------//
	for(i=0; i < parent_inode_info->dir_children_count; i++){
		if(dir_contents->state_flag == REMOVED){
			i--;
		}
		dir_contents++;
	}
	//dir_contents += parent_inode_info->dir_children_count

	//dir_contents += parent_inode_info->dir_children_count;
										//Para avanzar vamos sumando cosas. Cuando le sumamos el num de elementos avanzamos al final del directorio padre
	dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.
										//Colocamos lo que hemos hecho antes con inode info
	strcpy(dir_contents->filename, dentry->d_name.name);
										//Tenemos que copiar el nombre del fichero
	
	//----------------------------  PARTE NECESARIA PARA QUE EL REMOVE FUNCIONE BIEN ---------------------------------------- //
	dir_contents->state_flag = ALIVE;

	//Escribir en disco
	mark_buffer_dirty(bh);		//PONEMOS EL BIT A SUCIO
	sync_dirty_buffer(bh);		//FORZAMOS LA SINCRONIZACION. Todos los cambios que esten en dirty, se trasladaran a disco
	printk(KERN_INFO "Node created and stored correctly\n");
	brelse(bh);					//liberamos memoria del bufferhead

	parent_inode_info->dir_children_count++;			//AUMENTAMOS EN UNO ELCONTADOR DE HIJOS DEL PADRE
	assoofs_save_inode_info(sb, parent_inode_info);		//CON ESTA FUNCION PASAMOS A DISCO LA INFORMACION DEL PADRE

	return 0;	//PARA INDICAR QUE TODO HA SALIDO BIEN
}

/* =========================================================== *
 *  OPERACIONES SOBRE EL SUPERBLOQUE  
 * =========================================================== */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/* =========================================================== *
 *  CONSECUCION DE INFORMACION DE LOS INODOS   
 * =========================================================== */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
	struct assoofs_inode_info *inode_info = NULL;
	struct assoofs_inode_info *buffer = NULL;
	int i;
	struct buffer_head *bh;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
	printk(KERN_INFO B "Get inode info request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

	//ACCEDEMOS A DISCO PARA LEER EL BLOQUE QUE CONTIENE EL ALMACEN DE INODOS
	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_info = (struct assoofs_inode_info *)bh->b_data;

	//RECORREMOS EL ALMACÉN DE INODOS EN BUSCA DEL inode_no
	for(i = 0; i < afs_sb->inodes_count; i++){
		if(inode_info->inode_no == inode_no){  //he encontrado el nodo por el que me preguntan
			
			printk(KERN_INFO "Node found\n");
				//ESTA ES LA MANERA SIN CACHE DE INODOS
    		//buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    			//ESTA ES LA MANERA CON CACHE DE INODOS
   			buffer = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);	   //RESERVO MEMORIA EN EL KERNEL

			memcpy(buffer, inode_info, sizeof(*buffer));					   //COPIO EN BUFFER EL CONTENIDO DEL INODO 
			break;
		}

		//-----------------------------  NECESARIO PARA EL REMOVE -------------- //
		if(inode_info->state_flag == REMOVED){
			i--;
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
   
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	struct inode *root_inode;									//AQUÍ VAMOS A GUARDAR EL INODO DEL ROOT
	struct buffer_head *bh; 									//Aquí tendremos toda la información de un bloque
    struct assoofs_super_block_info *assoofs_sb;				//Puntero al superbloque (info) 

    //IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
    printk(KERN_INFO B "Fill_super request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */

    // 1.- Leer la información persistente del superbloque del dispositivo de bloques  

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *       LEER BLOQUES DE DISCO              * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    printk(KERN_INFO "Reading the blocks in the disk\n");
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);			//Llamada a sb_bread, superbloque block read (superbloque, numero de bloque del superbloque)
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; //Sacar el contenido del bloque (b_data)(Campo binario) (Meto en assoofs_sb la info del superbloque)
    			//Hacemos el cast para que se identifiquen los campos de info del superbloque	
    			//data es un void *				
    //brelse(bh);

    // 2.- Comprobar los parámetros del superbloque

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *         COMPROBAR PARAMETROS             * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 
    printk(KERN_INFO "Checking parameters\n");
    printk(KERN_INFO "The magic number obtained in disk is %llu\n", assoofs_sb->magic);
    //if(unlikely(assoofs_sb->magic != ASSOOFS_MAGIC)){
     if(assoofs_sb->magic != ASSOOFS_MAGIC){
    	printk(KERN_ERR "The filesystem that you want to mount is not assoofs. MAGIC_NUMBER mismatch.\n");
    	brelse(bh);
    	return -1;
    	//return -EPERM;
    }

    printk(KERN_INFO "The block size obtained in disk is %lld\n", assoofs_sb->block_size);
    //if(unlikely(assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)){
    if(assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE){
    	printk(KERN_ERR "assoofs seems to be formated using a wrong block size. BLOCK_SIZE mismatch.\n");
    	brelse(bh);
    	return -1;
    	//return -EPERM;
    }

    printk(KERN_INFO B "Recognised assoofs filesystem. (MAGIC_NUMBER = %llu & BLOCK_SIZE = %lld)\n", assoofs_sb->magic, assoofs_sb->block_size);

    //informacion necesaria para el remove
    //assoofs_sb->inodes_removed_count = 0;

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.

    	/* ++++++++++++++++++++++++++++++++++++++++++++ /
    	   *    ASIGNAR PARAMETROS Y OPERACIONES      * /
    	/ ++++++++++++++++++++++++++++++++++++++++++++ */ 

    sb->s_magic = ASSOOFS_MAGIC; 					//ASIGNAMOS EL NUMERO MAGICO AL NUEVO SUPERBLOQUE
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;	//ASIGNAMOS EL TAMAÑO DE BLOQUE
    sb->s_op = &assoofs_sops;						//ASIGNAMOS LAS OPERACIONES AL SUPERBLOQUE
    sb->s_fs_info = assoofs_sb;
    printk(KERN_INFO "Assigned parameters and operations\n");

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
    printk(KERN_INFO "Created the root inode with all the parameters and the README.txt file\n");

    if(!sb->s_root){
    	brelse(bh);
    	return -1;
    	//return -ENOMEM;
    }

    printk(KERN_INFO G "Super_Block prepared to work\n");

    brelse(bh);
    return 0;
}

/* =========================================================== *
 *  MONTAJE DE DISPOSITIVOS ASSOOFS     
 * =========================================================== */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */	
	struct dentry *ret;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
    printk(KERN_INFO B "Mount request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);        //Función que monta el dispositivo
                                                                    //Esta función es la que se encargará de llenar nuestro superbloque con la información correspondiente
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    return ret;
}

/* =========================================================== *
 *  ATRIBUTOS DEL SO ASSOOFS     
 * =========================================================== */
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,                            //EL MODULO MISMO
    .name    = "assoofs",                              //IMPORTANTE: NOMBRE FILE SYSTEM
    .mount   = assoofs_mount,                          //CUANDO SE MONTE QUE HAGA ESTO
    .kill_sb = kill_litter_super,                      //CUANDO SE DESMONTE QUE HAGA ESTO
};

/* =========================================================== *
 *  CARGA DE MODULO EN EL KERNEL    
 * =========================================================== */
static int __init assoofs_init(void) {
    
	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	int ret;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
    printk(KERN_INFO B "Init request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
    ret = register_filesystem(&assoofs_type);
    
    //configuramos la cache inicializandola como sigue
    assoofs_inode_cache = kmem_cache_create("assoofs_inode_cache", sizeof(struct assoofs_inode_info), 0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD), NULL);

    // Control de errores a partir del valor de ret
    return ret;
}

/* =========================================================== *
 *  DESCARGA DE MODULO EN EL KERNEL    
 * =========================================================== */
static void __exit assoofs_exit(void) {

	/* ++++++++++++++++++++++++++++++++++++++++++++ /
	 *       DECLARACION VARIABLES                 *
	/ ++++++++++++++++++++++++++++++++++++++++++++ */
	int ret;

	//IMPRESION DE LA TRAZA CORRESPONDIENTE AL USO DE ESTA FUNCION
    printk(KERN_INFO B "Exit request\n" R_C);

    /* ++++++++++++++++++++++++++++++++++++++++++++ /
     *      PROCECEMOS CON EL DESARROLLO           * 
    / ++++++++++++++++++++++++++++++++++++++++++++ */
    
    //procedemos a liberar la cache cuando desmontamos el modulo
    kmem_cache_destroy(assoofs_inode_cache);

    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
}

module_init(assoofs_init);
module_exit(assoofs_exit);

//Terminamos de configurar las macros para hacer el modulo de licencia abierta
MODULE_LICENSE("GPL");			//Licencia de codigo abierto
MODULE_AUTHOR(DRIVER_AUTHOR);	//Autor
MODULE_DESCRIPTION(DRIVER_DESC);//Descripcion breve