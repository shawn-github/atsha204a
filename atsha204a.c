#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/async.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/input/mt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/adc.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include "atsha204a.h"

struct atsha204a_info_t{
    int    write_mode;
    int    read_mode;
};

struct atsha204a_info_t atsha204a_info;
struct i2c_client * i2c_connect_clients = NULL;

static unsigned char atsha204a_key[atsha204a_key_len]={
    0x13,0x26,0x57,0x64,0x06,0x13,0x71,0x46,
    0x62,0x79,0x31,0x31,0x68,0x02,0x02,0x86
};
static unsigned char encrypt_data[atsha204a_data_len]={0x0};

#ifdef  REGISTER_CDEV
struct atsha204a_dev {
    struct cdev         cdev;
    dev_t               devno;
    struct class*       class;
};

static struct atsha204a_dev* atsha204a_devp = NULL;

#endif
struct atmel_atsha204a {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	u8 *touch_data;
	u8 device_id;
	int irq;
	int irq_pin;
	int rst_pin;
	int rst_val;
	struct work_struct	resume_work;
};

static int iic_read_bytes(struct i2c_client *client, uint8_t *buf, uint16_t len)
{
    struct i2c_msg msgs[2];                                                                                                                                              
    int ret=-1;                                                                                                                                                          

    msgs[0].flags = !I2C_M_RD;                                                                                                                                           
    msgs[0].addr = client->addr;                                                                                                                                         
    msgs[0].len = 1;
    msgs[0].buf = buf;

    msgs[1].flags = I2C_M_RD;
    msgs[1].addr = client->addr;                                                                                                                                         
    msgs[1].len = len-1;
    msgs[1].buf = buf+1;

    ret=i2c_transfer(client->adapter, msgs, 2);
    return ret;
}

static int iic_write_bytes(struct i2c_client *client, uint8_t *data, uint16_t len)                                                                                       
{                                                                                                                                                                        
    struct i2c_msg msg;                                                                                                                                                  
    int ret=-1;                                                                                                                                                          

    msg.flags = !I2C_M_RD;
    msg.addr = client->addr;
    msg.len = len;
    msg.buf = data;
    ret=i2c_transfer(client->adapter, &msg,1);                                                                                                                           
    return ret;                                                                                                                                                          
}

static int atsha204a_i2c_write(struct i2c_client * client , unsigned char addr)
{
    int ret=0, retry;
    unsigned char test_data[2]={0x75,0x02};
    test_data[0] = addr ;
    for(retry=0;retry<5;retry++){
        ret =iic_write_bytes(client, test_data, sizeof(test_data)/sizeof(test_data[0]));    //Test I2C connection.
        if (ret > 0)
        {   
            pr_info("i2c test ok\n");
            break;
        }   
        msleep(25);
    }
    return ret;
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


static int atsha204a_open (struct inode *inode, struct file *filp)
{
    printk("open /dev/atsha204a ok!\n");
    return 0;
}


static ssize_t atsha204a_read (struct file *filp, const char __user *buff, 
        size_t count, loff_t *offp)
{
    int ret=0,i;
    unsigned char read_buf[9]={0x0};
    unsigned char addr = 0 ;	
    int k = 0 ;
    unsigned char middle_buf[25]={0x0};


    if( 9 < count ){
        printk("Err :read atsha204a data len=%d > 9\n", count);
        return -1;
    }
    for( k=0;k<count;k++){
        printk("read buff[%d] = 0x%x \n", k , buff[k] );
    }
    memset(middle_buf,0x0,sizeof(middle_buf));
    if(copy_from_user(middle_buf,buff,count)){
        printk("atsha204a_read copy data from user err\n");
        goto read_err;
    }
    addr = buff[0]&0xff;
    for( k=0;k<count;k++){
        printk("read middle_buf[%d] = 0x%x \n", k , middle_buf[k] );
    }

    atsha204a_info.read_mode = read_password_mode ;
    switch(atsha204a_info.read_mode)
    {
        case read_encrypt_mode:
            if(copy_to_user(buff, encrypt_data, count)){
                printk("copy encrypt data to user err\n");
                goto read_err;
            }
            break;

        case read_password_mode:
            read_buf[0]=addr;
            ret=iic_read_bytes(i2c_connect_clients,read_buf , count );
            if(ret>0){
                for(i=0;i<count;i++)
                    printk("atsha204a_read read_buf[%d]= 0x%x\n",i,read_buf[i]);
            }
            else{
                printk("read_password_mode  read atsha204a err\n");
                goto read_err;
            }
            if( copy_to_user(buff+1, read_buf+1, count-1)){
                printk("copy to user err\n");
                goto read_err;
            }

            for( k=0;k<count;k++){
                printk("atsha204a-read after copy  buff[%d] = 0x%x \n", k , buff[k] );
            }

            break;
        default:
            goto read_err;
            break;
    }

    return 0;
read_err:
    return -1;
}


static ssize_t atsha204a_write (struct file *filp, const char __user *buf,
        size_t count, loff_t *offp)
{
    int ret=0,i=0;
    unsigned char addr = 0 ;	

    unsigned char middle_buf[25]={0x0};
    unsigned char receive_buf[8]={0x0};
    unsigned char Tmpwrite_buf[2]={0x90 , 0};

    memset(middle_buf,0x0,sizeof(middle_buf));

    if(copy_from_user(middle_buf,buf,count)){
        printk("copy data from user err\n");
        goto write_err;
    }
    addr = middle_buf[0]&0xff;
    if(0x75 == addr ){
        return atsha204a_i2c_write(i2c_connect_clients , 0x75);
    }

    atsha204a_info.write_mode = write_sdmc_mode;
    switch(atsha204a_info.write_mode)//cmd
    {
        case write_key_mode:
            if(count!=atsha204a_key_len+1)
                goto write_err;

            memset(atsha204a_key,0x0,sizeof(atsha204a_key));
            for(i=0;i<atsha204a_key_len;i++)
                atsha204a_key[i]=middle_buf[i+1];
            apkey_to_hwkey(atsha204a_key);
            break;

        case  write_random_mode:
            if(count!=atsha204a_data_len+1)
                goto write_err;
            for(i=0;i<atsha204a_data_len;i++)
                receive_buf[i]=middle_buf[i+1];
            for(i=0;i<atsha204a_data_len;i++)
                printk("rceeive data%d=%x\n",i,receive_buf[i]);
            if(ret){
                printk("chang data err\n");
                memset(encrypt_data,0x0,atsha204a_data_len);
                goto write_err;
            }
            else{
                for(i=0;i<atsha204a_data_len;i++){
                    printk("sdmc data%d=0x%x\n",i,receive_buf[i]);
                    encrypt_data[i]=receive_buf[i];
                }
            }
            break;
        case write_sdmc_mode:
            if(0x90 == addr){
                middle_buf[0] = 0x90 ;
                ret=iic_write_bytes(i2c_connect_clients, middle_buf, 9);
                if(ret!=1){
                    goto write_err;
                }
                return 0;
            }
            for(i=0;i<(count-1);i++){
                Tmpwrite_buf[0] = addr + i ;
                Tmpwrite_buf[1] = middle_buf[i+1];
                ret=iic_write_bytes(i2c_connect_clients, Tmpwrite_buf, 2);
                if(ret!=1){
                    printk("write_sdmc_mode :write atsha204a err\n");	
                    goto write_err;
                }
                msleep(15);
            }
            break;

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


static const struct file_operations atsha204a_fops = {
    .owner   = THIS_MODULE,
    .open    = atsha204a_open,
    .write   = atsha204a_write,
    .read    = atsha204a_read,
};


static int  atmel_atsha204a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct atmel_atsha204a *p;
#ifdef  REGISTER_CDEV
    int error;
    dev_t devno;
#endif


    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "I2C functionality not supported\n");
        return -ENODEV;
    }

    p = kzalloc(sizeof(*p), GFP_KERNEL);
    if (!p)
        return -ENOMEM;

    p->client = client;
    i2c_set_clientdata(client, p);
    p->device_id = 0;

#ifdef  REGISTER_CDEV
    error = alloc_chrdev_region(&devno, 0u, 1u, DEVICE_NAME);
    if (error) {
        atsha204a_print("ERROR: alloc_chrdev_regin failed.\n");
        return error;
    }

    atsha204a_devp = kmalloc(sizeof(struct atsha204a_dev), GFP_KERNEL);
    if (NULL == atsha204a_devp) {
        atsha204a_print("ERROR: kmallloc failed.\n");
        error = -ENOMEM;
        goto error_unregister_chrdev_region;
    }
    memset(atsha204a_devp, 0, sizeof(struct atsha204a_dev));

    cdev_init(&atsha204a_devp->cdev, &atsha204a_fops);
    atsha204a_devp->cdev.owner = THIS_MODULE;
    error = cdev_add(&atsha204a_devp->cdev, devno, 1u);
    if (error) {
        atsha204a_print("ERROR: cdev_add failed.\n");
        goto error_kfree;
    }

    memcpy(&atsha204a_devp->devno, &devno, sizeof(dev_t));

    atsha204a_devp->class = class_create(THIS_MODULE, DEVICE_NAME);
    if(IS_ERR(atsha204a_devp->class)) {
        atsha204a_print("ERROR: class_create failed.\n");
        error = -EFAULT;
        goto error_cdev_del;
    }
    device_create(atsha204a_devp->class, NULL, devno, NULL, DEVICE_NAME);
    atsha204a_print("register chardev %s success.\n", DEVICE_NAME);

#endif
    return 0;


#ifdef  REGISTER_CDEV
error_cdev_del:
    cdev_del(&atsha204a_devp->cdev);
error_kfree:
    kfree(atsha204a_devp);
    atsha204a_devp = NULL;
error_unregister_chrdev_region:
    unregister_chrdev_region(devno, 1u);
#endif
    return error;
}

static int  atmel_atsha204a_remove(struct i2c_client *client)
{
    struct atmel_atsha204a *p = i2c_get_clientdata(client);

    printk("==atmel_atsha204a_remove=\n");
#ifdef REGISTER_CDEV
    device_destroy(atsha204a_devp->class, atsha204a_devp->devno);
    class_destroy (atsha204a_devp->class);
    cdev_del(&atsha204a_devp->cdev);
    unregister_chrdev_region(atsha204a_devp->devno, 1u);
    kfree(atsha204a_devp);
    atsha204a_devp = NULL;
#endif

    kfree(p);

    return 0;
}

static const struct i2c_device_id atmel_atsha204a_id[] = {
	{ATSHA204A_I2C_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, atmel_atsha204a_id);

static struct of_device_id goodix_ts_dt_ids[] = {
	{ .compatible = "atmel,atsha204a" },
	{ }
};


static struct i2c_driver atmel_atsha204a_driver = {
	.driver = {
		.name = ATSHA204A_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goodix_ts_dt_ids),
	},
	.probe		= atmel_atsha204a_probe,
	.remove		= atmel_atsha204a_remove,
	.id_table	= atmel_atsha204a_id,
};

static int __init atmel_atsha204a_init(void)
{
    int ret=0;
	printk(KERN_ERR "atmel_atsha204a_init\n");	
	ret = i2c_add_driver(&atmel_atsha204a_driver);
	
	return ret;
}
static void __exit atmel_atsha204a_exit(void)
{
	printk("atmel_atsha204a_exit\n");
	i2c_del_driver(&atmel_atsha204a_driver);
	return;
}

module_init(atmel_atsha204a_init);
module_exit(atmel_atsha204a_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel");
MODULE_AUTHOR("shawn_cool@hotmail.com");
MODULE_DESCRIPTION("atsha204a controller driver");
