/*
Name: Omar Elgebaly, Aviral Sinha
Class: Operating Systems II - Fall 2017
Simple Block Driver Implementation
*/
/*
Sources consulted: http://blog.superpat.com/2010/05/04/a-simple-block-driver-for-linux-kernel-2-6-31/
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h> 
#include <linux/fs.h>    
#include <linux/errno.h>  
#include <linux/types.h>  
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/crypto.h> 

#define KERNEL_SECTOR_SIZE 512 //The kernel expects to be dealing with devices that implement 512-byte sectors

MODULE_LICENSE("Dual MIT/GPL");
static char *Version = "1.4";

/* BEGIN GLOBALS */
static struct crypto_cipher *crp; //crypto API struct 
static int major_num = 0; //major number the device will be using
module_param(major_num, int, 0);
static int drive_size = 1024; //size of the drive
module_param(drive_size, int, 0);
static int block_device_size = 512; //size of block device is 512 bytes
module_param(block_device_size, int, 0);
static char* key = "password"; //AES encryption key
module_param(key, charp, 0);
/* END GLOBALS */

//The device request queue
static struct request_queue *queue;

//Our internal device representing ebd0
static struct ebd_dev {
	
	unsigned long size; //Device size in sectors
	spinlock_t lock; //Mutual exclusion
	u8 *data; //the data array
	struct gendisk *gd; //the gendisk sstructure
	
} Device;

//helper function to print hex data at address
static void print_hex(u8 *ptr, unsigned int len) {   

	int i;
	for (i = 0 ; i < len ; i++){
		printk("%02x ", ptr[i]);
	}
	printk("\n");
	
}

//I/O Handler
static void ebd_transfer(struct ebd_dev *dev, sector_t sector, unsigned long nsect, char *buffer, int write) {

	u8 *hex_str, *hex_disk, *hex_buf;
	unsigned long offset = sector*block_device_size;
	unsigned long nbytes = nsect*block_device_size;
	unsigned int i;
 
	hex_disk = dev->data + offset;
	hex_buf = buffer;
	printk("ebd: encryption key: %s\n",key);

	if ((offset + nbytes) > dev->size) {
		printk (KERN_NOTICE "ebd: Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
		} 
	if (write) {
		printk("ebd: Begin write/encryption\n");
        //encrypts one block at a time until new data size 
		for(i = 0; i < nbytes; i += crypto_cipher_blocksize(crp)){
			crypto_cipher_encrypt_one(crp, hex_disk + i, hex_buf + i);
		}

		printk("ebd: printing hex data at original address\n");
		hex_str = buffer;
		print_hex(hex_str,15);

		printk("ebd: printing encrypted hex data\n");
		hex_str = dev->data + offset;
		hex_str = dev->data + offset;
		print_hex(hex_str,15);

	} 
	else {
		printk("ebd: Begin read/decryption\n");

		//decrypts one block at a time to full read data size
		for(i = 0; i < nbytes; i += crypto_cipher_blocksize(crp)){
			crypto_cipher_decrypt_one(crp, hex_buf + i,hex_disk + i);
		}

		printk("ebd: printing original hex data\n");
		hex_str = dev->data + offset;
		print_hex(hex_str,15);

		printk("ebd: printing decrypted hex data\n");
		hex_str = buffer;
		print_hex(hex_str,15);
	}
}

static void ebd_encrypt_request(struct request_queue *q) {
	struct request *rq;
	rq = blk_fetch_request(q); 
	while (rq!= NULL) {
		
		if (rq== NULL || (rq->cmd_type != REQ_TYPE_FS)) {
			printk (KERN_NOTICE "Skip non-CMD request\n");
			__blk_end_request_all(rq, -EIO);
			continue;
		}

		ebd_transfer(&Device, blk_rq_pos(rq), blk_rq_cur_sectors(rq),
			bio_data(rq->bio), rq_data_dir(rq));

		if ( ! __blk_end_request_cur(rq, 0) ) {
			rq= blk_fetch_request(q);
		}
	}
}

//get geometry. Since we have no real geometry, this is made up
int ebd_getgeo(struct block_device * block_device, struct hd_geometry * geo) {
	long size;   
	size = Device.size * (block_device_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4; //set the start of data at sector 4
	return 0;
}

//device operations structure
static struct block_device_operations ebd_dev_operations = {
		.owner  = THIS_MODULE,
		.getgeo = ebd_getgeo
};

//initialize block device
static int __init ebd_init_device(void) {

	//initialize internal device
	Device.size = drive_size * block_device_size;
	spin_lock_init(&Device.lock); //controls access to request queue
	Device.data = vmalloc(Device.size);
	if (Device.data == NULL)
		return -ENOMEM;
	
	//allocation of request queue
	queue = blk_init_queue(ebd_encrypt_request, &Device.lock);
	if (queue == NULL)
		goto out;
	blk_queue_block_device_size(queue, block_device_size);
	
	//registration
	major_num = register_blkdev(major_num, "ebd"); //register block driver with the kernel, ebd
	if (major_num < 0) {
		printk(KERN_WARNING "ebd: unable to get major number\n");
		goto out;
	}

    //allocate AES encryption cipher
	crp = crypto_alloc_cipher("aes",0,0);
	if(!crp){
		printk(KERN_WARNING "ebd: UNABLE TO ALLOCATE CRYPTO.\n");
		goto out;
	}

	printk("ebd: encryption key > %s\n",key);
	crypto_cipher_setkey(crp,key,strlen(key));

	//allocate, initialize, and install the gendisk structure 
	Device.gd = alloc_disk(16);
	if (!Device.gd)
		goto out_unregister;
	Device.gd->major = major_num;
	Device.gd->first_minor = 0;
	Device.gd->fops = &ebd_dev_operations;
	Device.gd->private_data = &Device;
	strcpy(Device.gd->disk_name, "ebd0");
	set_capacity(Device.gd, drive_size);
	Device.gd->queue = queue;
	add_disk(Device.gd);

	printk("ebd: block device initialized.\n");

	return 0;

out_unregister:
	unregister_blkdev(major_num, "ebd"); //unregister device
out:
	vfree(Device.data);
	return -ENOMEM;
}

static void __exit ebd_exit_dev(void)
{	
	crypto_free_cipher(crp); //free crypto
	del_gendisk(Device.gd); //free disk
	put_disk(Device.gd);
	unregister_blkdev(major_num, "ebd"); //unregister the block driver
	blk_cleanup_queue(queue);

	printk("ebd: block device deallocated\n");
}

module_init(ebd_init_device);
module_exit(ebd_exit_dev);


