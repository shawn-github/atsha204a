#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include "atsha204a.h"

#define USE_TIMER_POLL

#ifdef USE_TIMER_POLL
struct timer_list atmel_timer;	
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

static u32 atmel_atsha204a_iic_read(struct i2c_client *client, u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		msleep(5);
	}

	return i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg)) == ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}

static u32 atmel_atsha204a_iic_write(struct i2c_client *client, const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

#ifdef USE_TIMER_POLL
static void atmel_atsha204a_timer_func(void)
{
    struct atmel_atsha204a *p = NULL;
    u8 buf[32];
    printk("atsha204a timer out\n");

    //iic_read_bytes(p->client,0x84,buf,8);

    mod_timer(&atmel_timer,jiffies + HZ/1); //set timer HZ
    return;
}
#endif


static int  atmel_atsha204a_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct atmel_atsha204a *p;

    printk("ATSHA204A Enter %s\n", __func__);
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

#ifdef USE_TIMER_POLL
    init_timer(&atmel_timer);
    atmel_timer.expires = jiffies +2; 
    atmel_timer.function = (void*)atmel_atsha204a_timer_func;
    add_timer(&atmel_timer);
#endif
    
    printk("[ATSHA204A] End %s\n", __func__);
    return 0;

}

static int  atmel_atsha204a_remove(struct i2c_client *client)
{
    struct atmel_atsha204a *p = i2c_get_clientdata(client);

    printk("==atmel_atsha204a_remove=\n");
#ifdef USE_TIMER_POLL
	del_timer(&atmel_timer);
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
