#include <linux/fs.h>		/* everything... */
#include <linux/ioctl.h> 	/* needed for the _IOW etc stuff used later */
#include <linux/kernel.h>	/* printk() */
#include <asm/uaccess.h>	/* copy_*_user */
#include <linux/slab.h>		/* kmalloc */
#include <linux/proc_fs.h>	/* proc */
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/init.h>   
#include <linux/module.h>   
#include <linux/cdev.h>   
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/delay.h>

#define ZHANG_CDEV_DEVICE_NAME	"zhang_cdev"
struct class *zhang_cdev_class;		/* for automatically creating device nodes */

/* Use 'k' as magic number */
#define ZHANG_CDEV_IOC_MAGIC  'k'
/*	ZHANG_CDEV_IOCTLRESET:		reset circular queue
	ZHANG_CDEV_IOCTLFIXFRONT:	fix front 
	ZHANG_CDEV_IOCTLFIXREAR:	fix rear
	ZHANG_CDEV_IOCTLRESIZE:		change capacity
*/
#define ZHANG_CDEV_IOCTLRESET		_IO(ZHANG_CDEV_IOC_MAGIC,   0)
#define ZHANG_CDEV_IOCTLFIXFRONT	_IO(ZHANG_CDEV_IOC_MAGIC,   1)
#define ZHANG_CDEV_IOCTLFIXREAR		_IO(ZHANG_CDEV_IOC_MAGIC,   2)
#define ZHANG_CDEV_IOCTLRESIZE		_IO(ZHANG_CDEV_IOC_MAGIC,   3)

#define ZHANG_CDEV_IOC_MAXNR		3

/*
	parameters which can be set at load time
*/
int cq_capacity = 1000;			/* capacity of circular queue */
int zhang_cdev_major = 0;		/* dynamic major by default */

module_param(cq_capacity, int, S_IRUGO);
module_param(zhang_cdev_major, int, S_IRUGO);

struct zhang_cdev_cqset {
	char * z_cqueue;
	int z_front;
	int z_rear;			/* rear is empty */
};

struct zhang_cdev {
	struct zhang_cdev_cqset *z_data;  	/* pointer to front of circular queue */
	wait_queue_head_t z_rd_block_queue;	/* block queue for read */
	struct semaphore z_sem;     		/* mutual exclusion semaphore */
	struct cdev z_cdev;	  				/* Char device structure */
};

struct zhang_cdev *zhang_cdev_devices;	/* allocated in scull_init_module */

/* proc operation */
int zhang_cdev_proc(struct file *file, char *buf, size_t count, loff_t *eof) {	
	sprintf(buf, "front : %d, rear : %d, capacity : %d\n", zhang_cdev_devices->z_data->z_front, zhang_cdev_devices->z_data->z_rear, cq_capacity);

	count = 44;
	
	//mdelay(5000);
	
	*eof = 1;
	return count;
}

struct file_operations zhang_cdev_proc_fops = {
	.owner = THIS_MODULE,
	.read  = zhang_cdev_proc, 
};

/* open and close */
int zhang_cdev_open(struct inode *inode, struct file *filp)
{
	struct zhang_cdev *dev;
	printk("zhang_cdev_open begin\n");

	dev = container_of(inode->i_cdev, struct zhang_cdev, z_cdev);
	filp->private_data = dev;
	
	printk("zhang_cdev_open end\n");
	return 0;          /* success */
}

int zhang_cdev_release (struct inode *node, struct file *file)  
{  
	printk("zhang_cdev_release begin\n");
	
	printk("zhang_cdev_release end\n");
	return 0;  
}  

/* Data management: read and write */
ssize_t zhang_cdev_read(struct file *file, char __user *buff, size_t count, loff_t *offp)  
{
	struct zhang_cdev * dev;
	int begin, end;
	printk("zhang_cdev_read begin\n");
	dev = (struct zhang_cdev*)file->private_data;

	wait_event_interruptible(zhang_cdev_devices->z_rd_block_queue, 
							(zhang_cdev_devices->z_data->z_front != zhang_cdev_devices->z_data->z_rear)
							&&(*offp != zhang_cdev_devices->z_data->z_rear));
	
	/* apply for semaphore */
	down(&zhang_cdev_devices->z_sem);
	begin = zhang_cdev_devices->z_data->z_front;
	end = zhang_cdev_devices->z_data->z_rear;
	printk("read : begin = %d\n", begin);
	printk("read : end = %d\n", end);
	
	/*	insufficient data : 
		1. begin < end && end - begin < count, actual data not enough
		2. end < begin && capacity-begin < count, stop at actual end of queue
	*/
	if((begin < end) 
		&& ( (end - begin - *offp) < count)) { 
		count = end - begin - *offp;
	}
	else if((end < begin)
		&& ( (cq_capacity - begin - *offp) < count)){
		count = cq_capacity - begin - *offp;
	}

	if(copy_to_user(buff, dev->z_data->z_cqueue + begin + *offp, count) != 0) {
		printk("copy_to_user error\n");
		count = 0;
		goto END;
	}
END:
	/* release semaphore */
	//mdelay(5000);
	up(&zhang_cdev_devices->z_sem);
	printk("zhang_cdev_read end\n");
	return count;  
}  

ssize_t zhang_cdev_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)  
{
	struct zhang_cdev * dev;
	int tmp = 0;
	int begin, end;
	/* apply for semaphore */
	down(&zhang_cdev_devices->z_sem);
	printk("zhang_cdev_write begin\n");
	dev = (struct zhang_cdev*)file->private_data;
	begin = zhang_cdev_devices->z_data->z_front;
	end = zhang_cdev_devices->z_data->z_rear;

	
	/*	1. end - begin == capacity, nothing to do
		2. end == capacity, stop at actual end of queue, adjust end
		3. capacity - end < count, count = capacity-end, stop at actual end of queue, adjust end
		4. end < begin && begin-end < count, count = begin-end 
	*/
	if((end - begin) == cq_capacity) { 
		printk("write : device full\n");
		count = 0;
		goto Full;
	}
	if(end == cq_capacity){
		end = 0;
		printk("write : end of device\n");
		count = 0;
		goto Full;
	}
	if((cq_capacity - end) < count) {
		count = cq_capacity - end;
	}
	else if((end < begin) && (begin - end) < count) {
		count = begin - end;
	}

	if(copy_from_user(dev->z_data->z_cqueue + begin + *offp, buff, count)) {
		printk("copy_from_user error\n");
		count = 0;
	}
	if(begin + *offp + count > end) {
		tmp = count - *offp;
		while(end + tmp > cq_capacity) {
			tmp -= cq_capacity;
		}
		end += tmp;
		printk("write : *offp = %lld\n", *offp);
	}

	wake_up_interruptible(&zhang_cdev_devices->z_rd_block_queue);
	
Full:
	zhang_cdev_devices->z_data->z_rear = end;
	/* release semaphore */
	//mdelay(5000);
	up(&zhang_cdev_devices->z_sem);
	printk("zhang_cdev_write end\n");
	return count;  
}   

/* simple seek */
static loff_t zhang_cdev_llseek(struct file *filp, loff_t off, int whence) {
	loff_t newpos = 0;
	int begin, end;
	begin = zhang_cdev_devices->z_data->z_front;
	end = zhang_cdev_devices->z_data->z_rear;
	printk("zhang_cdev_llseek begin\n");

	if( begin == end) return 0;
	switch(whence) {
	  case 0: /* SEEK_SET */
		if(begin < end) {
			newpos = off < end ? off : end;
		}
		else {
			newpos = off < end ? off : ( (off > begin && off < cq_capacity) ? off : cq_capacity);
		}
		break;

	  case 1: /* SEEK_CUR */
		if(begin < end) {
			newpos = (filp->f_pos + off) < end ? (filp->f_pos + off) : end;
		}
		else {
			newpos = (filp->f_pos + off) < end ? (filp->f_pos + off) : 
			( ( (filp->f_pos + off) > begin && (filp->f_pos + off) < cq_capacity) ? (filp->f_pos + off) : cq_capacity);
		}
		break;

	  case 2: /* SEEK_END */
		if(off > 0) break;
		if(begin < end) {
			newpos = end + off > begin ? end + off : begin;
		}
		else {
			newpos = end + off > 0 ? -off : 0;
		}
		newpos = end + off;
		break;

	  default: /* can't happen */
		printk("llseek error\n");
		return -EINVAL;
	}
	if (newpos < 0) {
		printk("lseek error\n");
		return -EINVAL;
	}
	
	filp->f_pos = newpos;
	printk("zhang_cdev_llseek end\n");
	return newpos;
}

/* the ioctl() implementation */
static long zhang_cdev_ioctl(struct file * filp, unsigned int cmd, unsigned long arg)  
{
	struct zhang_cdev * dev;
	int begin = 0, end = 0;
	printk("zhang_cdev_ioctl begin\n");
	/* apply for semaphore */
	down(&zhang_cdev_devices->z_sem);
	dev = (struct zhang_cdev*)filp->private_data;
	
	begin = zhang_cdev_devices->z_data->z_front;
	end = zhang_cdev_devices->z_data->z_rear;

	if(_IOC_TYPE(cmd) != ZHANG_CDEV_IOC_MAGIC)	return -ENOTTY;
	if(_IOC_NR(cmd) > ZHANG_CDEV_IOC_MAXNR)	return	-ENOTTY;

	switch(cmd) {
	case ZHANG_CDEV_IOCTLRESET:
		begin = 0;
		end = 0;
		break;
	case ZHANG_CDEV_IOCTLFIXFRONT:
		if(begin < end) {
			begin = (int)arg < end ? (int)arg : end;
		}
		else {
			begin = (int)arg < end ? (int)arg : ( ((int)arg > begin && (int)arg < cq_capacity) ? (int)arg : cq_capacity);
		}
		break;
	case ZHANG_CDEV_IOCTLFIXREAR:
		if(begin < end) {
			end = (int)arg < end ? (int)arg : end;
		}
		else {
			end = (int)arg < end ? (int)arg : ( ((int)arg > begin && (int)arg < cq_capacity) ? (int)arg : cq_capacity);
		}
		break;
	case ZHANG_CDEV_IOCTLRESIZE:
		cq_capacity = (int)arg;
		break;
	default:
		return -ENOTTY;
	}
	
	zhang_cdev_devices->z_data->z_front = begin;
	zhang_cdev_devices->z_data->z_rear = end;
	/* release semaphore */
	up(&zhang_cdev_devices->z_sem);
	printk("zhang_cdev_ioctl end\n");
	return 0;  
}  

/* define all external interfaces of the equipment */
struct file_operations zhang_cdev_fops = {
	.owner = THIS_MODULE,
	.open  = zhang_cdev_open,  
	.release = zhang_cdev_release,
	.read  = zhang_cdev_read, 
	.write = zhang_cdev_write,  
	.unlocked_ioctl = zhang_cdev_ioctl,
	.llseek = zhang_cdev_llseek,
};

/* subfunction, call by zhang_cdev_exit */
void zhang_cdev_cleanup(void) {
	printk("zhang_cdev_cleanup begin\n");
	
	remove_proc_entry(ZHANG_CDEV_DEVICE_NAME, NULL);
	device_destroy(zhang_cdev_class, MKDEV(zhang_cdev_major, 0));
	class_destroy(zhang_cdev_class);
	cdev_del(&zhang_cdev_devices->z_cdev);
	
	kfree(zhang_cdev_devices->z_data->z_cqueue);
	kfree(zhang_cdev_devices->z_data);
	kfree(zhang_cdev_devices);
	
	unregister_chrdev_region(MKDEV(zhang_cdev_major, 0), 1);
	
	printk("zhang_cdev_cleanup end\n");
}

/* subfunction, call by zhang_cdev_init */
static void zhang_cdev_setup(struct cdev *dev, int minor, struct file_operations *fops) {
	int err, devno;
	printk("zhang_cdev_setup begin\n");
	
	devno = MKDEV(zhang_cdev_major, minor);
	cdev_init(dev, fops);
	dev->owner = THIS_MODULE;
	dev->ops = fops;
	err = cdev_add(dev, devno, 1);
	if(err) {
		printk("Error %d adding char_dev %d\n", err, minor);  
	}
	
	printk("zhang_cdev_setup end\n");
}

/* initialize device */
static int zhang_cdev_init(void) {
	int ret = 0;
	dev_t dev = 0;
	printk("zhang_cdev_init begin\n");
	
	/*	Get a range of minor numbers to work with, asking for a dynamic
		major unless directed otherwise at load time.
	*/
	if(zhang_cdev_major) {
		dev = MKDEV(zhang_cdev_major, 0);
		ret = register_chrdev_region(dev, 1, ZHANG_CDEV_DEVICE_NAME);
	}
	else {
		ret = alloc_chrdev_region(&dev, 0, 1, ZHANG_CDEV_DEVICE_NAME);
		zhang_cdev_major = MAJOR(dev);
	}	
	if(ret < 0) {
		printk("zhang_cdev region fail\n");
		goto fail;
	}
	
	/* allocate the devices */
	zhang_cdev_devices = kmalloc(sizeof(struct zhang_cdev), GFP_KERNEL);
	if(!zhang_cdev_devices) {
		ret = -ENOMEM;
		goto fail;
	}
	memset(zhang_cdev_devices, 0, sizeof(struct zhang_cdev));
	
	/* Initialize  device */
	zhang_cdev_devices->z_data = kmalloc(sizeof(struct zhang_cdev_cqset), GFP_KERNEL);
	memset(zhang_cdev_devices->z_data, 0, sizeof(struct zhang_cdev_cqset));
	zhang_cdev_devices->z_data->z_cqueue = kmalloc(cq_capacity * sizeof(int), GFP_KERNEL);
	memset(zhang_cdev_devices->z_data->z_cqueue, 0, cq_capacity * sizeof(int));
	zhang_cdev_devices->z_data->z_front = 0;
	zhang_cdev_devices->z_data->z_rear = 0;
	sema_init(&zhang_cdev_devices->z_sem, 1);
	zhang_cdev_setup(&zhang_cdev_devices->z_cdev, 0, &zhang_cdev_fops);
	init_waitqueue_head(&zhang_cdev_devices->z_rd_block_queue);
	
	/* create device nodes */
	zhang_cdev_class = class_create(THIS_MODULE, ZHANG_CDEV_DEVICE_NAME);
	if (IS_ERR(zhang_cdev_class)) {  
		printk("Err: failed in creating zhang_cdev class\n");  
		return 0;  
	}
	device_create(zhang_cdev_class, NULL, dev, NULL, ZHANG_CDEV_DEVICE_NAME); 
	
	proc_create(ZHANG_CDEV_DEVICE_NAME, 0, NULL, &zhang_cdev_proc_fops);
	
	return 0;	/* succeed */
	
fail:
	zhang_cdev_cleanup();
	return ret;
	
	printk("zhang_cdev_init end\n");
}

/* log off device */
static void zhang_cdev_exit(void) {
	printk("zhang_cdev_exit begin\n");
	
	zhang_cdev_cleanup();
	
	printk("zhang_cdev_exit end\n");
}

module_init(zhang_cdev_init);	/* initialization interface of module */
module_exit(zhang_cdev_exit);	/* unregister interface of module */

MODULE_AUTHOR("zhang");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("simple char devices of zhang");  