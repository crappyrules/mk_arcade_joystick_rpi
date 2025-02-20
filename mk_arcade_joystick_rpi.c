/*
 *  Arcade Joystick Driver for RaspberryPi
 *
 *  Copyright (c) 2014 Matthieu Proucelle
 *
 *  Based on the gamecon driver by Vojtech Pavlik, and Markus Hiienkari
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <linux/ioport.h>
#include <asm/io.h>

MODULE_AUTHOR("Matthieu Proucelle");
MODULE_DESCRIPTION("GPIO Arcade Joystick Driver");
MODULE_LICENSE("GPL");

#define MK_MAX_DEVICES 2

#ifdef RPI2
#define PERI_BASE 0x3F000000
#else
#define PERI_BASE 0x20000000
#endif

#define GPIO_BASE (PERI_BASE + 0x200000) /* GPIO controller */

#define INP_GPIO(g) *(gpio + ((g) / 10)) &= ~(7 << (((g) % 10) * 3))
#define OUT_GPIO(g) *(gpio + ((g) / 10)) |= (1 << (((g) % 10) * 3))
#define GPIO_READ(g) *(gpio + 13) >> g & 1

#define SET_GPIO_ALT(g, a) *(gpio + (((g) / 10))) |= (((a) <= 3 ? (a) + 4 : (a) == 4 ? 3 : 2) << (((g) % 10) * 3))

#define GPIO_SET *(gpio + 7)
#define GPIO_CLR *(gpio + 10)

#define SPI1_BASE (PERI_BASE + 0x204000)

/*
 * Defines for SPI peripheral 
 */

#define SPI_CS_TXD (1 << 18)
#define SPI_CS_RXD (1 << 17)
#define SPI_CS_DONE (1 << 16)
#define SPI_CS_TA (1 << 7)
#define SPI_CS_CLEAR_RX (1 << 5)
#define SPI_CS_CLEAR_TX (1 << 4)
#define SPI_CS_CPOL (1 << 3)

static volatile unsigned *gpio;
static volatile unsigned *spi1;

struct mk_config
{
    int args[MK_MAX_DEVICES];
    unsigned int nargs;
};

static struct mk_config mk_cfg __initdata;

module_param_array_named(map, mk_cfg.args, int, &(mk_cfg.nargs), 0);
MODULE_PARM_DESC(map, "Enable or disable GPIO Arcade Joystick");

struct gpio_config
{
    int mk_arcade_gpio_maps_custom[12];
    unsigned int nargs;
};

static struct gpio_config gpio_cfg __initdata;

module_param_array_named(gpio, gpio_cfg.mk_arcade_gpio_maps_custom, int, &(gpio_cfg.nargs), 0);
MODULE_PARM_DESC(gpio, "Numbers of custom GPIO for Arcade Joystick");

struct spi_config
{
    int spi_lines[4];
    unsigned int nargs;
};

static struct spi_config spi_cfg __initdata;

module_param_array_named(spi, spi_cfg.spi_lines, int, &(spi_cfg.nargs), 0);
MODULE_PARM_DESC(spi, "Numbers of custom SPI Lines for MCP3008");

static char SPI_MISO_LINE = 16;
static char SPI_MOSI_LINE = 26;
static char SPI_CLK_LINE = 20;
static char SPI_CS_LINE = 21;

enum mk_type
{
    MK_NONE = 0,
    MK_ARCADE_GPIO,
    MK_ARCADE_GPIO_BPLUS,
    MK_ARCADE_GPIO_CUSTOM,
    MK_MAX
};

#define MK_REFRESH_TIME HZ / 100

struct mk_pad
{
    struct input_dev *dev;
    enum mk_type type;
    char phys[32];
    int gpio_maps[12];
};

struct mk_nin_gpio
{
    unsigned pad_id;
    unsigned cmd_setinputs;
    unsigned cmd_setoutputs;
    unsigned valid_bits;
    unsigned request;
    unsigned request_len;
    unsigned response_len;
    unsigned response_bufsize;
};

struct mk
{
    struct mk_pad pads[MK_MAX_DEVICES];
    struct timer_list timer;
    int pad_count[MK_MAX];
    int used;
    struct mutex mutex;
};

struct mk_subdev
{
    unsigned int idx;
};

static struct mk *mk_base;

static const int mk_max_arcade_buttons = 12;

// Map of the gpios :                     up, down, left, right, start, select, a,  b,  tr, y,  x,  tl
static const int mk_arcade_gpio_maps[] = {4, 17, 27, 22, 10, 9, 25, 24, 23, 18, 15, 14};
// 2nd joystick on the b+ GPIOS                 up, down, left, right, start, select, a,  b,  tr, y,  x,  tl
static const int mk_arcade_gpio_maps_bplus[] = {11, 5, 6, 13, 19, 26, 21, 20, 16, 12, 7, 8};

static const short mk_arcade_gpio_btn[] = {
    BTN_START, BTN_SELECT, BTN_EAST, BTN_SOUTH, BTN_TR, BTN_WEST, BTN_NORTH, BTN_TL};

static const char *mk_names[] = {
    NULL, "GPIO Controller 1", "GPIO Controller 2", "GPIO Controller 1", "GPIO Controller 1"};

/* GPIO UTILS */
static void setGpioPullUps(int pullUps)
{
    *(gpio + 37) = 0x02;
    udelay(10);
    *(gpio + 38) = pullUps;
    udelay(10);
    *(gpio + 37) = 0x00;
    *(gpio + 38) = 0x00;
}

static void setGpioAsInput(int gpioNum)
{
    INP_GPIO(gpioNum);
}

static int getPullUpMask(int gpioMap[])
{
    int mask = 0x0000000;
    int i;
    for (i = 0; i < 12; i++)
    {
        if (gpioMap[i] != -1)
        { // to avoid unused pins
            int pin_mask = 1 << gpioMap[i];
            mask = mask | pin_mask;
        }
    }
    return mask;
}

/* SPI UTILS */
static void spi_init(void)
{

    INP_GPIO(SPI_MISO_LINE);

    INP_GPIO(SPI_MOSI_LINE);
    OUT_GPIO(SPI_MOSI_LINE);
    INP_GPIO(SPI_CLK_LINE);
    OUT_GPIO(SPI_CLK_LINE);
    INP_GPIO(SPI_CS_LINE);
    OUT_GPIO(SPI_CS_LINE);

    GPIO_CLR = 1 << SPI_CLK_LINE;
    GPIO_CLR = 1 << SPI_MISO_LINE;
    GPIO_CLR = 1 << SPI_MOSI_LINE;
    GPIO_SET = 1 << SPI_CS_LINE;
}

// Function to read a number of bytes into a  buffer from the FIFO of the I2C controller

static void spi_transfer(char *tbuf, char *rbuf, unsigned short len)
{
    GPIO_CLR = 1 << SPI_CS_LINE;

    int i;
    for (i = 0; i < len; i++)
    {
        udelay(100);
        int j;
        for (j = 7; j >= 0; j--)
        {
            if (tbuf[i] >> j & 1)
                GPIO_SET = 1 << SPI_MOSI_LINE;
            else
                GPIO_CLR = 1 << SPI_MOSI_LINE;

            udelay(100);
            GPIO_SET = 1 << SPI_CLK_LINE;

            if (GPIO_READ(SPI_MISO_LINE))
                rbuf[i] |= 1 << j;
            else
                rbuf[i] &= ~(1 << j);

            udelay(100);
            GPIO_CLR = 1 << SPI_CLK_LINE;
        }
    }
    GPIO_SET = 1 << SPI_CS_LINE;
}

static void mk_mcp3008_read_packet(struct mk_pad *pad, unsigned short *data)
{
    char send_data[] = {0x01, 0x80, 0x00};
    char receive_data[] = {0x00, 0x00, 0x00};

    spi_transfer(send_data, receive_data, 3);

    int rx = (receive_data[1] << 8 | receive_data[2]) & 0x3FF;

    send_data[1] = 0x90;
    spi_transfer(send_data, receive_data, 3);

    int ry = (receive_data[1] << 8 | receive_data[2]) & 0x3FF;

    send_data[1] = 0xa0;
    spi_transfer(send_data, receive_data, 3);

    int x = (receive_data[1] << 8 | receive_data[2]) & 0x3FF;

    send_data[1] = 0xb0;
    spi_transfer(send_data, receive_data, 3);

    int y = (receive_data[1] << 8 | receive_data[2]) & 0x3FF;

    //printk("X: %d Y: %d\n", x, y);

    data[mk_max_arcade_buttons + 1] = 1023 - ry;
    data[mk_max_arcade_buttons + 2] = rx;
    data[mk_max_arcade_buttons + 3] = 1023 - y;
    data[mk_max_arcade_buttons + 4] = x;
}

static void mk_gpio_read_packet(struct mk_pad *pad, unsigned short *data)
{
    int i;

    for (i = 0; i < mk_max_arcade_buttons; i++)
    {
        if (pad->gpio_maps[i] != -1)
        { // to avoid unused buttons
            int read = GPIO_READ(pad->gpio_maps[i]);
            if (read == 0)
                data[i] = 1;
            else
                data[i] = 0;
        }
        else
            data[i] = 0;
    }
}

static void mk_input_report(struct mk_pad *pad, unsigned short *data)
{
    struct input_dev *dev = pad->dev;
    int j;
    if(!data[0] - !data[1] < 0)
    {
        input_report_key(dev, BTN_DPAD_UP, 0);
        input_report_key(dev, BTN_DPAD_DOWN, 1);
    }
    else if(!data[0] - !data[1] > 0)
    {
        input_report_key(dev, BTN_DPAD_UP, 1);
        input_report_key(dev, BTN_DPAD_DOWN, 0);
    }
    else
    {
        input_report_key(dev, BTN_DPAD_UP, 0);
        input_report_key(dev, BTN_DPAD_DOWN, 0);
    }
    if(!data[2] - !data[3] < 0)
    {
        input_report_key(dev, BTN_DPAD_LEFT, 0);
        input_report_key(dev, BTN_DPAD_RIGHT, 1);
    }
    else if(!data[2] - !data[3] > 0)
    {
        input_report_key(dev, BTN_DPAD_LEFT, 1);
        input_report_key(dev, BTN_DPAD_RIGHT, 0);
    }
    else
    {
        input_report_key(dev, BTN_DPAD_LEFT, 0);
        input_report_key(dev, BTN_DPAD_RIGHT, 0);
    }
    input_report_abs(dev, ABS_HAT0Y, !data[0] - !data[1]);
    input_report_abs(dev, ABS_HAT0X, !data[2] - !data[3]);
    //printk("X: %d Y: %d\n", data[mk_max_arcade_buttons+1], data[mk_max_arcade_buttons+2]);
    input_report_abs(dev, ABS_RY, data[mk_max_arcade_buttons + 1]);
    input_report_abs(dev, ABS_RX, data[mk_max_arcade_buttons + 2]);
    input_report_abs(dev, ABS_Y, data[mk_max_arcade_buttons + 3]);
    input_report_abs(dev, ABS_X, data[mk_max_arcade_buttons + 4]);
    for (j = 4; j < mk_max_arcade_buttons; j++)
    {
        input_report_key(dev, mk_arcade_gpio_btn[j - 4], data[j]);
    }
    input_sync(dev);
}

static void mk_process_packet(struct mk *mk)
{
    unsigned short data[mk_max_arcade_buttons + 4];
    struct mk_pad *pad;
    int i;

    for (i = 0; i < MK_MAX_DEVICES; i++)
    {
        pad = &mk->pads[i];
        if (pad->type == MK_ARCADE_GPIO || pad->type == MK_ARCADE_GPIO_BPLUS || pad->type == MK_ARCADE_GPIO_CUSTOM)
        {
            mk_gpio_read_packet(pad, data);
            mk_mcp3008_read_packet(pad, data);
            mk_input_report(pad, data);
        }
    }
}

/*
 * mk_timer() initiates reads of console pads data.
 */

static void mk_timer(struct timer_list *t)
{
    struct mk *mk = from_timer(mk, t, timer);
    mk_process_packet(mk);
    mod_timer(&mk->timer, jiffies + MK_REFRESH_TIME);
}

static int mk_open(struct input_dev *dev)
{
    struct mk *mk = input_get_drvdata(dev);
    int err;

    err = mutex_lock_interruptible(&mk->mutex);
    if (err)
        return err;

    if (!mk->used++)
        mod_timer(&mk->timer, jiffies + MK_REFRESH_TIME);

    mutex_unlock(&mk->mutex);
    return 0;
}

static void mk_close(struct input_dev *dev)
{
    struct mk *mk = input_get_drvdata(dev);

    mutex_lock(&mk->mutex);
    if (!--mk->used)
    {
        del_timer_sync(&mk->timer);
    }
    mutex_unlock(&mk->mutex);
}

static int __init mk_setup_pad(struct mk *mk, int idx, int pad_type_arg)
{
    struct mk_pad *pad = &mk->pads[idx];
    struct input_dev *input_dev;
    int i, pad_type;
    int err;
    char FF = 0xFF;
    pr_err("pad type : %d\n", pad_type_arg);

    pad_type = pad_type_arg;

    if (pad_type < 1)
    {
        pr_err("Pad type %d unknown\n", pad_type);
        return -EINVAL;
    }

    if (pad_type == MK_ARCADE_GPIO_CUSTOM)
    {

        // if the device is custom, be sure to get correct pins
        if (gpio_cfg.nargs < 1)
        {
            pr_err("Custom device needs gpio argument\n");
            return -EINVAL;
        }
        else if (gpio_cfg.nargs != 12)
        {
            pr_err("Invalid gpio argument\n", pad_type);
            return -EINVAL;
        }
    }

    pr_err("pad type : %d\n", pad_type);
    pad->dev = input_dev = input_allocate_device();
    if (!input_dev)
    {
        pr_err("Not enough memory for input device\n");
        return -ENOMEM;
    }

    pad->type = pad_type;
    snprintf(pad->phys, sizeof(pad->phys),
             "input%d", idx);

    input_dev->name = mk_names[pad_type];
    input_dev->phys = pad->phys;
    input_dev->id.bustype = BUS_PARPORT;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = pad_type;
    input_dev->id.version = 0x0100;

    input_set_drvdata(input_dev, mk);

    input_dev->open = mk_open;
    input_dev->close = mk_close;

    input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    //input_dev->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) | BIT_MASK(ABS_RX) | BIT_MASK(ABS_RY);

    input_set_abs_params(input_dev, ABS_HAT0X, -1, 1, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT0Y, -1, 1, 0, 0);
    input_set_abs_params(input_dev, ABS_RX, 0, 1023, 4, 8);
    input_set_abs_params(input_dev, ABS_RY, 0, 1023, 4, 8);
    input_set_abs_params(input_dev, ABS_X + i, 0, 1023, 4, 8);
    input_set_abs_params(input_dev, ABS_Y + i, 0, 1023, 4, 8);
    for (i = 0; i < mk_max_arcade_buttons - 4; i++)
        __set_bit(mk_arcade_gpio_btn[i], input_dev->keybit);
    __set_bit(BTN_DPAD_UP, input_dev->keybit);
    __set_bit(BTN_DPAD_DOWN, input_dev->keybit);
    __set_bit(BTN_DPAD_LEFT, input_dev->keybit);
    __set_bit(BTN_DPAD_RIGHT, input_dev->keybit);

    mk->pad_count[pad_type]++;

    // asign gpio pins
    switch (pad_type)
    {
    case MK_ARCADE_GPIO:
        memcpy(pad->gpio_maps, mk_arcade_gpio_maps, 12 * sizeof(int));
        break;
    case MK_ARCADE_GPIO_BPLUS:
        memcpy(pad->gpio_maps, mk_arcade_gpio_maps_bplus, 12 * sizeof(int));
        break;
    case MK_ARCADE_GPIO_CUSTOM:
        memcpy(pad->gpio_maps, gpio_cfg.mk_arcade_gpio_maps_custom, 12 * sizeof(int));
        break;
    }

    for (i = 0; i < mk_max_arcade_buttons; i++)
    {
        printk("GPIO = %d\n", pad->gpio_maps[i]);
        if (pad->gpio_maps[i] != -1)
        { // to avoid unused buttons
            setGpioAsInput(pad->gpio_maps[i]);
        }
    }
    setGpioPullUps(getPullUpMask(pad->gpio_maps));
    printk("GPIO configured for pad%d\n", idx);

    if (spi_cfg.nargs == 4)
    {
        SPI_MISO_LINE = spi_cfg.spi_lines[0];
        SPI_MOSI_LINE = spi_cfg.spi_lines[1];
        SPI_CLK_LINE = spi_cfg.spi_lines[2];
        SPI_CS_LINE = spi_cfg.spi_lines[3];
    }

    spi_init();

    //Toggle clock line
    GPIO_SET = 1 << SPI_CLK_LINE;
    GPIO_CLR = 1 << SPI_CLK_LINE;

    udelay(1000);
    printk("Analog is ON!");

    err = input_register_device(pad->dev);
    if (err)
        goto err_free_dev;

    return 0;

err_free_dev:
    input_free_device(pad->dev);
    pad->dev = NULL;
    return err;
}

static struct mk __init *mk_probe(int *pads, int n_pads)
{
    struct mk *mk;
    int i;
    int count = 0;
    int err;

    mk = kzalloc(sizeof(struct mk), GFP_KERNEL);
    if (!mk)
    {
        pr_err("Not enough memory\n");
        err = -ENOMEM;
        goto err_out;
    }

    mutex_init(&mk->mutex);
    timer_setup(&mk->timer, mk_timer, 0);

    for (i = 0; i < n_pads && i < MK_MAX_DEVICES; i++)
    {
        if (!pads[i])
            continue;

        err = mk_setup_pad(mk, i, pads[i]);
        if (err)
            goto err_unreg_devs;

        count++;
    }

    if (count == 0)
    {
        pr_err("No valid devices specified\n");
        err = -EINVAL;
        goto err_free_mk;
    }

    return mk;

err_unreg_devs:
    while (--i >= 0)
        if (mk->pads[i].dev)
            input_unregister_device(mk->pads[i].dev);
err_free_mk:
    kfree(mk);
err_out:
    return ERR_PTR(err);
}

static void mk_remove(struct mk *mk)
{
    int i;

    for (i = 0; i < MK_MAX_DEVICES; i++)
        if (mk->pads[i].dev)
            input_unregister_device(mk->pads[i].dev);
    kfree(mk);
}

static int __init mk_init(void)
{
    /* Set up gpio pointer for direct register access */
    if ((gpio = ioremap(GPIO_BASE, 0xB0)) == NULL)
    {
        pr_err("io remap failed\n");
        return -EBUSY;
    }
    if (mk_cfg.nargs < 1)
    {
        pr_err("at least one device must be specified\n");
        return -EINVAL;
    }
    else
    {
        mk_base = mk_probe(mk_cfg.args, mk_cfg.nargs);
        if (IS_ERR(mk_base))
            return -ENODEV;
    }
    return 0;
}

static void __exit mk_exit(void)
{
    if (mk_base)
        mk_remove(mk_base);

    iounmap(gpio);
}

module_init(mk_init);
module_exit(mk_exit);
