#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/types.h>       
#include <linux/fs.h>         
#include <linux/init.h>       
#include <linux/mm.h>        
#include <linux/cdev.h>  
#include <linux/sched.h>    
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include "atsha204a.h"


#ifdef PRINT_POINT_INFO 
#define print_point_info(fmt, args...)         
#else
#define print_point_info(fmt, args...)
#endif

struct atsha204a_data {
    struct i2c_client *client;
};
static void* __iomem gpio_addr = NULL;
static int gpio_reset_hdle = 0;
struct atsha204a_dev_t{           
    struct cdev cdev;  
    struct device *dev; 			 /* ptr to class device struct		   */
    struct class  *class;			 /* class for auto create device node  */
    int    major; 
    int    minor;
};  
struct atsha204a_info_t{           
    int    write_mode; 
    int    read_mode;
}; 

struct atsha204a_dev_t atsha204a_dev;
struct atsha204a_info_t atsha204a_info;
/* Addresses to scan */
union{
    unsigned short dirty_addr_buf[2];
    const unsigned short normal_i2c[2];
}u_i2c_addr = {{0x00},};

#define atsha204a_data_len 8
#define atsha204a_key_len 16
#define write_key_mode 0x10
#define write_random_mode 0x11
#define write_sdmc_mode 0x12
#define read_password_mode 0x20
#define read_encrypt_mode 0x21
#define ap_hw_key 0x5a

#define ATSHA204ADEBUG

static __u32 twi_id = 1;

struct i2c_client * i2c_connect_client = NULL;
static unsigned char atsha204a_key[atsha204a_key_len]={
    0x13,0x26,0x57,0x64,0x06,0x13,0x71,0x46,
    0x62,0x79,0x31,0x31,0x68,0x02,0x02,0x86
};
static unsigned char atsha204a_key1[atsha204a_key_len]={
    0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10
};
static unsigned char encrypt_data[atsha204a_data_len]={0x0};

static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, uint16_t len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	//发送写地址
	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;		//data address
	msgs[0].buf = buf;
	//接收数据
	msgs[1].flags = I2C_M_RD;//读消息
	msgs[1].addr = client->addr;
	msgs[1].len = len-1;
	msgs[1].buf = buf+1;
	
	ret=i2c_transfer(client->adapter, msgs, 2);
	return ret;
}
//Function as i2c_master_send, and return 1 if operation is successful. 
static int i2c_write_bytes(struct i2c_client *client, uint8_t *data, uint16_t len)
{
	struct i2c_msg msg;
	int ret=-1;
	
	msg.flags = !I2C_M_RD;//写消息
	msg.addr = client->addr;
	msg.len = len;
	msg.buf = data; 	
	
	ret=i2c_transfer(client->adapter, &msg,1);
	return ret;
}

static bool atsha204a_i2c_test(struct i2c_client * client)
{
	int ret=0, retry;
	unsigned char test_data[2]={0x0,0x02};
	for(retry=0;retry<5;retry++)
	{
		ret =i2c_write_bytes(client, test_data, sizeof(test_data)/sizeof(test_data[0]));	//Test I2C connection.
		if (ret > 0)
		{
			pr_info("i2c test ok\n");
			break;
		}
		msleep(25);
	}
	return ret==1 ? true : false;
}
static int atsha204a_i2c_lxltest(struct i2c_client * client , unsigned char addr)
{
	int ret=0, retry;
	unsigned char test_data[2]={0x75,0x02};
	test_data[0] = addr ;
	for(retry=0;retry<5;retry++)
	{
		ret =i2c_write_bytes(client, test_data, sizeof(test_data)/sizeof(test_data[0]));	//Test I2C connection.
		if (ret > 0)
		{
			pr_info("i2c test ok\n");
			break;
		}
		msleep(25);
	}
	return ret;
}
static int atsha204a_remove(struct i2c_client *client)
{
	struct atsha204a_data *ts = i2c_get_clientdata(client);
	i2c_set_clientdata(client, NULL);
	kfree(ts);
	return 0;
}

static int atsha204a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct atsha204a_data *ts;
	int ret = 0,err=0;
	int test_index;
	unsigned char write_buff[8]={0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
	unsigned char write_buff1[9]={0x90};//写地址
	unsigned char read_buff[9]={0x90};//读地址
	pr_info("==========atsha204a Probe========\n");
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "System need I2C function.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	ts->client = client;
	i2c_connect_client = client;
	i2c_set_clientdata(client, ts);

	atsha204a_info.write_mode=write_sdmc_mode;
	atsha204a_info.read_mode=read_password_mode;

	return 0;
	//=====================================
err_input_dev_alloc_failed:
    i2c_set_clientdata(client, NULL);
exit_ioremap_failed:
    kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static const struct i2c_device_id atsha204a_id[] = {
	{ ATSHA204A_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver atsha204a_i2c_driver = {
	.class =   I2C_CLASS_HWMON,
	.probe		= atsha204a_probe,
	.remove		= atsha204a_remove,
	.id_table	= atsha204a_id,
	.driver = {
		.name	= ATSHA204A_I2C_NAME,
		.owner = THIS_MODULE,
	},
	.address_list	= u_i2c_addr.normal_i2c,
};

static int atsha204a_open(struct inode *inode, struct file *file)
{
	printk("atsha204a open\n");
    return 0;
}

char apkey_to_hwkey(unsigned char *apkey)
{
	int index=0;
	for(index=0;index<atsha204a_key_len;index++)
	{
		//printk("apkey_%d=0x%x\n",index,*(apkey+index));
		(*(apkey+index))=(*(apkey+index))^ap_hw_key;
		//printk("hwkey_%d=0x%x\n",index,*(apkey+index));
	}
}

static int atsha204a_read(struct file *filp,  char __user *buff, size_t count, loff_t *offp)
{
	int ret=0,i;
	unsigned char read_buf[9]={0x0};//第一位为芯片地址
	unsigned char send_buf[8]={0x0};
	unsigned char addr = 0 ;	
	int k = 0 ;
	unsigned char middle_buf[25]={0x0};

	
	printk("read atsha204a data len=%d\n", count);
	if( 9 < count )
	{
		printk("Err :read atsha204a data len=%d > 9\n", count);
		return ;
	}
	for( k=0;k<count;k++)
	{
		printk("read buff[%d] = 0x%x \n", k , buff[k] );
	}
	memset(middle_buf,0x0,sizeof(middle_buf));
	if(copy_from_user(middle_buf,buff,count))
	{
		printk("atsha204a_read copy data from user err\n");
		goto read_err;
	}
	//addr = middle_buf[0]&0xff;
	addr = buff[0]&0xff;
	for( k=0;k<count;k++)
	{
		printk("read middle_buf[%d] = 0x%x \n", k , middle_buf[k] );
	}
	printk("read atsha204a addr = %d \n", addr );
	
	
	atsha204a_info.read_mode = read_password_mode ;
	switch(atsha204a_info.read_mode)
	{
		case read_encrypt_mode:
		{
			if( copy_to_user(buff, encrypt_data, count))
			{
				printk("copy encrypt data to user err\n");
				goto read_err;
			}
			printk("read encrypt data ok\n");
			break;
		}
		case read_password_mode:
        {
            read_buf[0]=addr;
            ret=i2c_read_bytes(i2c_connect_client,read_buf , count );
            if(ret>0)
            {
                for(i=0;i<count;i++)
                    printk("atsha204a_read read_buf[%d]= 0x%x\n",i,read_buf[i]);
            }
			else
			{
				printk("read_password_mode  read atsha204a err\n");
				goto read_err;
			}
			//for(i=0;i<8;i++)
			//	send_buf[i]=read_buf[i];
			//if( copy_to_user(buff+1, send_buf, count))
			if( copy_to_user(buff+1, read_buf+1, count-1))
			{
				printk("copy to user err\n");
				goto read_err;
			}

			for( k=0;k<count;k++)
			{
				printk("atsha204a-read after copy  buff[%d] = 0x%x \n", k , buff[k] );
			}

			printk("read atsha204a ok\n");
			break;
		}
		default:
			goto read_err;
			break;
	}
	return 0;
read_err:
	return -1;
}
static ssize_t atsha204a_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp)
{
	int ret=0,i=0;
	unsigned char addr = 0 ;	
	
	unsigned char middle_buf[25]={0x0};
	unsigned char receive_buf[8]={0x0};
	unsigned char write_buf[9]={0x90};
	unsigned char Tmpwrite_buf[2]={0x90 , 0};

	printk("atsha204a_write atsha204a data len=%d\n",count);
	for(i=0;i<count;i++)
		printk("atsha204a_write data buf[%d]=0x%x\n",i,buf[i]);
	memset(middle_buf,0x0,sizeof(middle_buf));
	


	if(copy_from_user(middle_buf,buf,count))
	{
		printk("copy data from user err\n");
		goto write_err;
	}
	#ifdef ATSHA204ADEBUG
	for(i=0;i<count;i++)
		printk("atsha204a_write data%d=0x%x\n",i,middle_buf[i]);
	#endif	

	addr	  =  middle_buf[0]&0xff;
	if(0x75 == addr )
	{
		//Test write error
		#ifdef ATSHA204ADEBUG
		printk("atsha204a_i2c_lxltest\n");
		#endif
		return atsha204a_i2c_lxltest(i2c_connect_client , 0x75);
	}


	//atsha204a_info.write_mode=middle_buf[0]&0xff;
		
	
	printk("atsha204a_write   addr = %d realcount = %d\n", addr , count );
	
	atsha204a_info.write_mode = write_sdmc_mode ;
	switch(atsha204a_info.write_mode)//cmd
	{
		case write_key_mode:
		{
			if(count!=atsha204a_key_len+1)
				goto write_err;
			memset(atsha204a_key,0x0,sizeof(atsha204a_key));
			for(i=0;i<atsha204a_key_len;i++)
				atsha204a_key[i]=middle_buf[i+1];
			apkey_to_hwkey(atsha204a_key);
			for(i=0;i<atsha204a_key_len;i++)
				printk("key%d=%x\n",i,atsha204a_key[i]);
			break;
		}
		case  write_random_mode:
		{		
			if(count!=atsha204a_data_len+1)
				goto write_err;
			for(i=0;i<atsha204a_data_len;i++)
				receive_buf[i]=middle_buf[i+1];
			for(i=0;i<atsha204a_data_len;i++)
				printk("rceeive data%d=%x\n",i,receive_buf[i]);
			if(ret!=0)
			{
				printk("chang data err\n");
				memset(encrypt_data,0x0,atsha204a_data_len);
				goto write_err;
			}
			else
			{
				for(i=0;i<atsha204a_data_len;i++)
				{
					printk("sdmc data%d=0x%x\n",i,receive_buf[i]);
					encrypt_data[i]=receive_buf[i];
				}
			}
			printk("write atsha204a ok\n");
			break;
		}
		case write_sdmc_mode:
		{
			if(0x90 == addr )
			{
				//encrypt data write multiplebytes
				middle_buf[0] = 0x90 ;
				ret=i2c_write_bytes(i2c_connect_client, middle_buf, 9);
				if(ret!=1)
				{
					printk("write atsha204a err\n");	
					goto write_err;
				}
				return 0;
			}
			for(i=0;i<(count-1);i++)
			{
				//write_buf[i+1]=middle_buf[i+1];
				//Tmpwrite_buf[0] = 0x0 + i ;
				Tmpwrite_buf[0] = addr + i ;
				Tmpwrite_buf[1] = middle_buf[i+1];
				ret=i2c_write_bytes(i2c_connect_client, Tmpwrite_buf, 2);
				if(ret!=1)
				{
					printk("write_sdmc_mode :write atsha204a err\n");	
					goto write_err;
				}
				msleep(15);
			}

			printk("write sdmc mode\n");
			break;
		}
		case read_encrypt_mode:
			atsha204a_info.read_mode=read_encrypt_mode;
			printk("set read encrypt\n");
			break;
		case read_password_mode:
			atsha204a_info.read_mode=read_password_mode;
			printk("set read password\n");
			break;
		default:
			goto write_err;
			break;
	}
	return 0;
write_err:
	return -1;
}

static int atsha204a_ioctl(struct file *filp, unsigned int cmd, unsigned long data)
{
	int ret=0;
	printk("atsha204a cmd=%d,data=%d\n",cmd,data);
	return ret;
}

static int atsha204a_release(struct inode *inode, struct file *file)
{
	printk("atsha204a release\n");
    return 0;
}
static struct file_operations dev_fops = {
	.owner  =   THIS_MODULE,   
	.open   =   atsha204a_open,
	.read   =   atsha204a_read,
	.write  =   atsha204a_write,
	.unlocked_ioctl =atsha204a_ioctl,
    .release  =  atsha204a_release, 
};


static int __init atsha204a_init(void)
{
    int err,devno;
    dev_t dev = 0;

    pr_info("========atsha204a_init=========\n");

    //register chardev
    result = alloc_chrdev_region(&dev,0,1,DEVICE_NAME);
    atsha204a_dev.major = MAJOR(dev);
    atsha204a_dev.minor = MINOR(dev);
    if(err){
        printk("alloc chardev region fail!\n");
        return err; 
    }

    cdev_init(&atsha204a_dev.cdev, &dev_fops);
    atsha204a_dev.cdev.owner = THIS_MODULE;
    atsha204a_dev.cdev.ops   = &dev_fops;
    err = cdev_add(&atsha204a_dev.cdev, dev, 1);  
    if(err){
        printk("add cdev fail\n");
        goto unregister_chrdev_region;
    }	

    //make node	
    devno = MKDEV(atsha204a_dev.major, atsha204a_dev.minor);
    atsha204a_dev.class = class_create(THIS_MODULE, "atsha204a");
    atsha204a_dev.dev   = device_create(atsha204a_dev.class, NULL, devno, NULL, "atsha204a");

    //add i2c driver	
    err=i2c_add_driver(&atsha204a_i2c_driver);
    if(err){
        printk("add i2c driver fail\n");
        goto unregister_chrdev_region;
    }

unregister_chrdev_region:
    unregister_chrdev_region(dev, 1);
    return -EFAULT;
}

static void __exit atsha204a_exit(void)
{
	printk("atsha204a exit\n");
	dev_t devno = MKDEV(atsha204a_dev.major, 0);  
	unregister_chrdev_region(devno, 1);
	/* Destroy char device */
	cdev_del(&atsha204a_dev.cdev); 					   
	device_destroy(&atsha204a_dev.class, devno);
	class_destroy(&atsha204a_dev.class);
	i2c_del_driver(&atsha204a_i2c_driver);
}
module_init(atsha204a_init);
module_exit(atsha204a_exit);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("ATSHA204A Driver");
MODULE_AUTHOR("luozhenghai <benson@ococci.com>");
