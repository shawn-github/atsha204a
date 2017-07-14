#ifndef __ATSHA204A_H_
#define __ATSHA204A_H_

#include <linux/i2c.h>
#include <linux/input.h>

#define ATSHA204A_I2C_NAME  "atsha204a"
#define DEVICE_NAME         "dm2016"   
#define REGISTER_CDEV

#define atsha204a_data_len  8
#define atsha204a_key_len   16
#define write_key_mode      0x10
#define write_random_mode   0x11
#define write_sdmc_mode     0x12
#define read_password_mode  0x20
#define read_encrypt_mode   0x21
#define ap_hw_key           0x5a

#define atsha204a_print(...)    do {printk("[ATSHA204A] "__VA_ARGS__);} while(0)

#endif
