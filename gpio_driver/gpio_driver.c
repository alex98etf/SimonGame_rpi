#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

/* Driver Doc.*/
MODULE_LICENSE("Dual BSD/GPL");

MODULE_AUTHOR("Aleksandar Beslic ETF Banja Luka (RTOS)");

MODULE_DESCRIPTION("Driver for Simon Game");


// NOTE: Check Broadcom BCM8325 datasheet, page 91+
// NOTE: GPIO Base address is set to 0x7E200000,
//       but it is VC CPU BUS address, while the
//       ARM physical address is 0x3F200000, what
//       can be seen in pages 5-7 of Broadcom
//       BCM8325 datasheet, having in mind that
//       total system ram is 0x3F000000 (1GB - 16MB)
//       instead of 0x20000000 (512 MB)

/* GPIO registers base address. */
#define BCM2708_PERI_BASE   (0x3F000000)
#define GPIO_BASE           (BCM2708_PERI_BASE + 0x200000)
#define GPIO_ADDR_SPACE_LEN (0xB4)
//--

//Handle GPIO: 0-9
/* GPIO Function Select 0. */
#define GPFSEL0_OFFSET (0x00000000)

//Handle GPIO: 10-19
/* GPIO Function Select 1. */
#define GPFSEL1_OFFSET (0x00000004)

//Handle GPIO: 20-29
/* GPIO Function Select 2. */
#define GPFSEL2_OFFSET (0x00000008)

//Handle GPIO: 30-39
/* GPIO Function Select 3. */
#define GPFSEL3_OFFSET (0x0000000C)

//Handle GPIO: 40-49
/* GPIO Function Select 4. */
#define GPFSEL4_OFFSET (0x00000010)

//Handle GPIO: 50-53
/* GPIO Function Select 5. */
#define GPFSEL5_OFFSET (0x00000014)
//--

//GPIO: 0-31
/* GPIO Pin Output Set 0. */
#define GPSET0_OFFSET (0x0000001C)

//GPIO: 32-53
/* GPIO Pin Output Set 1. */
#define GPSET1_OFFSET (0x00000020)
//--

//GPIO: 0-31
/* GPIO Pin Output Clear 0. */
#define GPCLR0_OFFSET (0x00000028)

//GPIO: 32-53
/* GPIO Pin Output Clear 1. */
#define GPCLR1_OFFSET (0x0000002C)
//--

//GPIO: 0-31
/* GPIO Pin Level 0. */
#define GPLEV0_OFFSET (0x00000034)

//GPIO: 32-53
/* GPIO Pin Level 1. */
#define GPLEV1_OFFSET (0x00000038)
//--

//GPIO: 0-53
/* GPIO Pin Pull-up/down Enable. */
#define GPPUD_OFFSET (0x00000094)

//GPIO: 0-31
/* GPIO Pull-up/down Clock Register 0. */
#define GPPUDCLK0_OFFSET (0x00000098)

//GPIO: 32-53
/* GPIO Pull-up/down Clock Register 1. */
#define GPPUDCLK1_OFFSET (0x0000009C)
//--

/* PUD - GPIO Pin Pull-up/down */
typedef enum {PULL_NONE = 0, PULL_DOWN = 1, PULL_UP = 2} PUD;
//--

//000 = GPIO Pin 'x' is an input
//001 = GPIO Pin 'x' is an output
// By default GPIO pin is being used as an input
typedef enum {GPIO_DIRECTION_IN = 0, GPIO_DIRECTION_OUT = 1} DIRECTION;
//--

/* GPIO pins available on connector p1. */
#define GPIO_02 (2)
#define GPIO_03 (3)
#define GPIO_04 (4)
#define GPIO_05 (5)
#define GPIO_06 (6)
#define GPIO_07 (7)
#define GPIO_08 (8)
#define GPIO_09 (9)
#define GPIO_10 (10)
#define GPIO_11 (11)
#define GPIO_12 (12)
#define GPIO_13 (13)
#define GPIO_14 (14)
#define GPIO_15 (15)
#define GPIO_16 (16)
#define GPIO_17 (17)
#define GPIO_18 (18)
#define GPIO_19 (19)
#define GPIO_20 (20)
#define GPIO_21 (21)
#define GPIO_22 (22)
#define GPIO_23 (23)
#define GPIO_24 (24)
#define GPIO_25 (25)
#define GPIO_26 (26)
#define GPIO_27 (27)

#define DEVICE_NAME "gpio_driver"

/* Declaration of gpio_driver.c functions */
int gpio_driver_init(void);
void gpio_driver_exit(void);
static int gpio_driver_open(struct inode *, struct file *);
static int gpio_driver_release(struct inode *, struct file *);
static ssize_t gpio_driver_read(struct file *, char *buf, size_t , loff_t *);
static ssize_t gpio_driver_write(struct file *, const char *buf, size_t , loff_t *);

/* Structure that declares the usual file access functions. */
struct file_operations gpio_driver_fops =
{
    open    :   gpio_driver_open,
    release :   gpio_driver_release,
    read    :   gpio_driver_read,
    write   :   gpio_driver_write
};

/* Declaration of the init and exit functions. */
module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

/* Global variables of the driver */

/* IRQ Number */
unsigned int GPIO_12_irq_Number = 0;
unsigned int GPIO_16_irq_Number = 0;
unsigned int GPIO_20_irq_Number = 0;
unsigned int GPIO_21_irq_Number = 0;

/* Save Flag */
static unsigned long flag = 0;

/* For Debouncing*/
extern unsigned long volatile jiffies;
unsigned long old_jiffie = 0;

/* Major number. */
int gpio_driver_major;

/* Buffer to store data. */
#define BUF_LEN 80
char* gpio_driver_buffer;
char* gpio_sequnce_buffer;


/* Virtual address where the physical GPIO address is mapped */
void* virt_gpio_base;

/*
 * GetGPFSELReg function
 *  Parameters:
 *   pin    - number of GPIO pin;
 *
 *   return - GPFSELn offset from GPIO base address, for containing desired pin control
 *  Operation:
 *   Based on the passed GPIO pin number, finds the corresponding GPFSELn reg and
 *   returns its offset from GPIO base address.
 */
unsigned int GetGPFSELReg(char pin)
{
    unsigned int addr;

    if(pin >= 0 && pin <10)
        addr = GPFSEL0_OFFSET;
    else if(pin >= 10 && pin <20)
        addr = GPFSEL1_OFFSET;
    else if(pin >= 20 && pin <30)
        addr = GPFSEL2_OFFSET;
    else if(pin >= 30 && pin <40)
        addr = GPFSEL3_OFFSET;
    else if(pin >= 40 && pin <50)
        addr = GPFSEL4_OFFSET;
    else /*if(pin >= 50 && pin <53) */
        addr = GPFSEL5_OFFSET;

  return addr;
}

/*
 * GetGPIOPinOffset function
 *  Parameters:
 *   pin    - number of GPIO pin;
 *
 *   return - offset of the pin control bit, position in control registers
 *  Operation:
 *   Based on the passed GPIO pin number, finds the position of its control bit
 *   in corresponding control registers.
 */
char GetGPIOPinOffset(char pin)
{
    if(pin >= 0 && pin <10)
        pin = pin;
    else if(pin >= 10 && pin <20)
        pin -= 10;
    else if(pin >= 20 && pin <30)
        pin -= 20;
    else if(pin >= 30 && pin <40)
        pin -= 30;
    else if(pin >= 40 && pin <50)
        pin -= 40;
    else /*if(pin >= 50 && pin <53) */
        pin -= 50;

    return pin;
}

/*
 * SetInternalPullUpDown function
 *  Parameters:
 *   pin    - number of GPIO pin;
 *   pull   - set internal pull up/down/none if PULL_UP/PULL_DOWN/PULL_NONE selected
 *  Operation:
 *   Sets to use internal pull-up or pull-down resistor, or not to use it if pull-none
 *   selected for desired GPIO pin.
 */
void SetInternalPullUpDown(char pin, PUD pull)
{
    unsigned int gppud_offset;
    unsigned int gppudclk_offset;
    unsigned int tmp;
    unsigned int mask;

    /* Get the offset of GPIO Pull-up/down Register (GPPUD) from GPIO base address. */
    gppud_offset = GPPUD_OFFSET;

    /* Get the offset of GPIO Pull-up/down Clock Register (GPPUDCLK) from GPIO base address. */
    gppudclk_offset = (pin < 32) ? GPPUDCLK0_OFFSET : GPPUDCLK1_OFFSET;

    /* Get pin offset in register . */
    pin = (pin < 32) ? pin : pin - 32;

    /* Write to GPPUD to set the required control signal (i.e. Pull-up or Pull-Down or neither
       to remove the current Pull-up/down). */
    iowrite32(pull, virt_gpio_base + gppud_offset);

    /* Wait 150 cycles � this provides the required set-up time for the control signal */

    /* Write to GPPUDCLK0/1 to clock the control signal into the GPIO pads you wish to
       modify � NOTE only the pads which receive a clock will be modified, all others will
       retain their previous state. */
    tmp = ioread32(virt_gpio_base + gppudclk_offset);
    mask = 0x1 << pin;
    tmp |= mask;
    iowrite32(tmp, virt_gpio_base + gppudclk_offset);

    /* Wait 150 cycles � this provides the required hold time for the control signal */

    /* Write to GPPUD to remove the control signal. */
    iowrite32(PULL_NONE, virt_gpio_base + gppud_offset);

    /* Write to GPPUDCLK0/1 to remove the clock. */
    tmp = ioread32(virt_gpio_base + gppudclk_offset);
    mask = 0x1 << pin;
    tmp &= (~mask);
    iowrite32(tmp, virt_gpio_base + gppudclk_offset);
}

/*
 * SetGpioPinDirection function
 *  Parameters:
 *   pin       - number of GPIO pin;
 *   direction - GPIO_DIRECTION_IN or GPIO_DIRECTION_OUT
 *  Operation:
 *   Sets the desired GPIO pin to be used as input or output based on the direcation value.
 */
void SetGpioPinDirection(char pin, DIRECTION direction)
{
    unsigned int GPFSELReg_offset;
    unsigned int tmp;
    unsigned int mask;

    /* Get base address of function selection register. */
    GPFSELReg_offset = GetGPFSELReg(pin);

    /* Calculate gpio pin offset. */
    pin = GetGPIOPinOffset(pin);

    /* Set gpio pin direction. */
    tmp = ioread32(virt_gpio_base + GPFSELReg_offset);
    if(direction)
    { //set as output: set 1
      mask = 0x1 << (pin*3);
      tmp |= mask;
    }
    else
    { //set as input: set 0
      mask = ~(0x1 << (pin*3));
      tmp &= mask;
    }
    iowrite32(tmp, virt_gpio_base + GPFSELReg_offset);
}

/*
 * SetGpioPin function
 *  Parameters:
 *   pin       - number of GPIO pin;
 *  Operation:
 *   Sets the desired GPIO pin to HIGH level. The pin should previously be defined as output.
 */
void SetGpioPin(char pin)
{
    unsigned int GPSETreg_offset;
    unsigned int tmp;

    /* Get base address of gpio set register. */
    GPSETreg_offset = (pin < 32) ? GPSET0_OFFSET : GPSET1_OFFSET;
    pin = (pin < 32) ? pin : pin - 32;

    /* Set gpio. */
    tmp = 0x1 << pin;
    iowrite32(tmp, virt_gpio_base + GPSETreg_offset);
}

/*
 * ClearGpioPin function
 *  Parameters:
 *   pin       - number of GPIO pin;
 *  Operation:
 *   Sets the desired GPIO pin to LOW level. The pin should previously be defined as output.
 */
void ClearGpioPin(char pin)
{
    unsigned int GPCLRreg_offset;
    unsigned int tmp;

    /* Get base address of gpio clear register. */
    GPCLRreg_offset = (pin < 32) ? GPCLR0_OFFSET : GPCLR1_OFFSET;
    pin = (pin < 32) ? pin : pin - 32;

    /* Clear gpio. */
    tmp = 0x1 << pin;
    iowrite32(tmp, virt_gpio_base + GPCLRreg_offset);
}

/*
 * GetGpioPinValue function
 *  Parameters:
 *   pin       - number of GPIO pin;
 *
 *   return    - the level read from desired GPIO pin
 *  Operation:
 *   Reads the level from the desired GPIO pin and returns the read value.
 */
char GetGpioPinValue(char pin)
{
    unsigned int GPLEVreg_offset;
    unsigned int tmp;
    unsigned int mask;

    /* Get base address of gpio level register. */
    GPLEVreg_offset = (pin < 32) ?  GPLEV0_OFFSET : GPLEV1_OFFSET;
    pin = (pin < 32) ? pin : pin - 32;

    /* Read gpio pin level. */
    tmp = ioread32(virt_gpio_base + GPLEVreg_offset);
    mask = 0x1 << pin;
    tmp &= mask;

    return (tmp >> pin);
}

/* Interupt Handler For GPIO pin going low*/
static irqreturn_t gpio_irq_handler_falling(int irq,void *dev_id) 
{
    int id = (int) (int *) dev_id;

    /* Debouncing proc. */
    unsigned long diff = jiffies - old_jiffie;

    if (diff < 20)
    {
        return IRQ_HANDLED;
    }

    old_jiffie = jiffies;

    /* What LED was activated  */
    if (id == GPIO_06)
    {
        strcat(gpio_sequnce_buffer, "1");
    }
    else if (id == GPIO_13)
    {
       strcat(gpio_sequnce_buffer, "2");
    }
    else if (id == GPIO_19)
    {
       strcat(gpio_sequnce_buffer, "3");
    }
    else if (id == GPIO_26)
    {
       strcat(gpio_sequnce_buffer, "4");
    }
    else
    {
        strcat(gpio_sequnce_buffer, "0");
    }
    

    local_irq_save(flag); // Save all curent interupts;
    
    SetGpioPin(id);
    mdelay(100);
    ClearGpioPin(id);
    printk(KERN_INFO "IRQ req: %d\n", irq);
    
    local_irq_restore(flag);
    
    return IRQ_HANDLED;
}

/*
 * Initialization:
 *  1. Register device driver
 *  2. Allocate buffer
 *  3. Initialize buffer
 *  4. Map GPIO Physical address space to virtual address
 *  5. Initialize GPIO pins
 *  6. Init and start the high resoultion timer
 */
int gpio_driver_init(void)
{
    int result = -1;

    printk(KERN_INFO "Inserting gpio_driver module\n");

    /* Registering device. */
    result = register_chrdev(0, DEVICE_NAME, &gpio_driver_fops);
    if (result < 0)
    {
        printk(KERN_INFO "gpio_driver: cannot obtain major number %d\n", gpio_driver_major);
        return result;
    }

    gpio_driver_major = result;
    printk(KERN_INFO "gpio_driver major number is %d\n", gpio_driver_major);

    /* Allocating memory for the buffer. */
    gpio_driver_buffer = kmalloc(BUF_LEN, GFP_KERNEL);
    if (!gpio_driver_buffer)
    {
        result = -ENOMEM;
        goto fail_no_mem_d;
    }

    /* Initialize data buffer. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

    /* Allocating memory for the buffer. */
    gpio_sequnce_buffer = kmalloc(BUF_LEN, GFP_KERNEL);
    if (!gpio_sequnce_buffer)
    {
        result = -ENOMEM;
        goto fail_no_mem_s;
    }

    /* Initialize data buffer. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

    /* map the GPIO register space from PHYSICAL address space to virtual address space */
    virt_gpio_base = ioremap(GPIO_BASE, GPIO_ADDR_SPACE_LEN);
    if(!virt_gpio_base)
    {
        result = -ENOMEM;
        goto fail_no_virt_mem;
    }

    /* Initialize GPIO pins. */
    /* LEDS */
    SetGpioPinDirection(GPIO_06, GPIO_DIRECTION_OUT);
    SetGpioPinDirection(GPIO_13, GPIO_DIRECTION_OUT);
    SetGpioPinDirection(GPIO_19, GPIO_DIRECTION_OUT);
    SetGpioPinDirection(GPIO_26, GPIO_DIRECTION_OUT);


    /* SWitches */
    SetInternalPullUpDown(GPIO_12, PULL_UP);
    SetInternalPullUpDown(GPIO_16, PULL_UP);
    SetInternalPullUpDown(GPIO_20, PULL_UP);
    SetInternalPullUpDown(GPIO_21, PULL_UP);
    SetGpioPinDirection(GPIO_12, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_16, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_20, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_21, GPIO_DIRECTION_IN);
    

    // Getting IRQ Number for GPIO Pins
    GPIO_12_irq_Number = gpio_to_irq(GPIO_12);
    if (request_irq(GPIO_12_irq_Number, gpio_irq_handler_falling, IRQF_TRIGGER_FALLING, DEVICE_NAME, (void *) GPIO_06))
    {
        printk(KERN_INFO "IRQ GPIO 12 ERROR");
        result = -EINTR;
        goto free_gpio_12;
    }

    GPIO_16_irq_Number = gpio_to_irq(GPIO_16);
    if (request_irq(GPIO_16_irq_Number, (void *) gpio_irq_handler_falling, IRQF_TRIGGER_FALLING, DEVICE_NAME, (void *) GPIO_13))
    {
        printk(KERN_INFO "IRQ GPIO 16 ERROR");
        result = -EINTR;
        goto free_gpio_16;
    }

    GPIO_20_irq_Number = gpio_to_irq(GPIO_20);
    if (request_irq(GPIO_20_irq_Number, (void *) gpio_irq_handler_falling, IRQF_TRIGGER_FALLING, DEVICE_NAME, (void *) GPIO_19))
    {
        printk(KERN_INFO "IRQ GPIO 20 ERROR");
        result = -EINTR;
        goto free_gpio_20;
    }

    GPIO_21_irq_Number = gpio_to_irq(GPIO_21);
    if (request_irq(GPIO_21_irq_Number, (void *) gpio_irq_handler_falling, IRQF_TRIGGER_FALLING, DEVICE_NAME, (void *) GPIO_26))
    {
        printk(KERN_INFO "IRQ GPIO 21 ERROR");
        result = -EINTR;
        goto free_gpio_21;
    }

    printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, gpio_driver_major);
    printk(KERN_INFO " IRQ Numbers %d %d %d %d", GPIO_12_irq_Number, GPIO_16_irq_Number, GPIO_20_irq_Number, GPIO_21_irq_Number);
    
    return 0;

fail_no_virt_mem:
    /* Freeing the major number. */
    unregister_chrdev(gpio_driver_major, DEVICE_NAME);

fail_no_mem_d:
    /* Freeing buffer gpio_driver_buffer. */
    if (gpio_driver_buffer)
    {
        kfree(gpio_driver_buffer);
    }

fail_no_mem_s:
    /* Freeing buffer gpio_driver_buffer. */
    if (gpio_sequnce_buffer)
    {
        kfree(gpio_sequnce_buffer);
    }

free_gpio_12:
    /* Freeing IRQ Line */
    free_irq(GPIO_12_irq_Number, (void *) GPIO_06);

free_gpio_16:
    /* Freeing IRQ Line */
    free_irq(GPIO_16_irq_Number, (void *) GPIO_13);

free_gpio_20:
    /* Freeing IRQ Line */
    free_irq(GPIO_20_irq_Number, (void *) GPIO_19);

free_gpio_21:
    /* Freeing IRQ Line */
    free_irq(GPIO_12_irq_Number, (void *) GPIO_26);

    return result;
}

/*
 * Cleanup:
 *  1. release GPIO pins (clear all outputs, set all as inputs and pull-none to minimize the power consumption)
 *  2. Unmap GPIO Physical address space from virtual address
 *  3. Free buffer
 *  4. Unregister device driver
 */
void gpio_driver_exit(void)
{
    printk(KERN_INFO "Removing gpio_driver module\n");

    /* Clear GPIO pins. */
    ClearGpioPin(GPIO_06);
    ClearGpioPin(GPIO_13);
    ClearGpioPin(GPIO_19);
    ClearGpioPin(GPIO_26);

    /* Set GPIO pins as inputs and disable pull-ups. */
    SetGpioPinDirection(GPIO_06, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_13, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_19, GPIO_DIRECTION_IN);
    SetGpioPinDirection(GPIO_26, GPIO_DIRECTION_IN);
    SetInternalPullUpDown(GPIO_12, PULL_NONE);
    SetInternalPullUpDown(GPIO_16, PULL_NONE);
    SetInternalPullUpDown(GPIO_20, PULL_NONE);
    SetInternalPullUpDown(GPIO_21, PULL_NONE);

    free_irq(GPIO_12_irq_Number, (void *) GPIO_06);
    free_irq(GPIO_16_irq_Number, (void *) GPIO_13);
    free_irq(GPIO_20_irq_Number, (void *) GPIO_19);
    free_irq(GPIO_21_irq_Number, (void *) GPIO_26);


    /* Unmap GPIO Physical address space. */
    if (virt_gpio_base)
    {
        iounmap(virt_gpio_base);
    }

    /* Freeing buffer gpio_driver_buffer. */
    if (gpio_driver_buffer)
    {
        kfree(gpio_driver_buffer);
    }

    /* Freeing buffer gpio_sequnce_buffer. */
    if (gpio_sequnce_buffer)
    {
        kfree(gpio_sequnce_buffer);
    }

    /* Freeing the major number. */
    unregister_chrdev(gpio_driver_major,  DEVICE_NAME);
}

/* File open function. */
static int gpio_driver_open(struct inode *inode, struct file *filp)
{
    /* Initialize driver variables here. */

    /* Reset the device here. */

    /* Success. */
    return 0;
}

/* File close function. */
static int gpio_driver_release(struct inode *inode, struct file *filp)
{
    /* Success. */
    return 0;
}

/*
 * File read function
 *  Parameters:
 *   filp  - a type file structure;
 *   buf   - a buffer, from which the user space function (fread) will read;
 *   len - a counter with the number of bytes to transfer, which has the same
 *           value as the usual counter in the user space function (fread);
 *   f_pos - a position of where to start reading the file;
 *  Operation:
 *   The gpio_driver_read function transfers data from the driver buffer (gpio_driver_buffer)
 *   to user space with the function copy_to_user.
 */
static ssize_t gpio_driver_read(struct file *filp, char *buf, size_t len, loff_t *f_pos)
{
    /* Size of valid data in gpio_driver - data to send in user space. */
    int data_size = 0;

    /* Reset memory. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

    strcpy(gpio_driver_buffer, gpio_sequnce_buffer);

    /* Reset memory. */
    memset(gpio_sequnce_buffer, 0, BUF_LEN);

    if (*f_pos == 0)
    {
        /* Get size of valid data. */
        data_size = strlen(gpio_driver_buffer);

        /* Send data to user space. */
        if (copy_to_user(buf, gpio_driver_buffer, data_size) != 0)
        {
            return -EFAULT;
        }
        else
        {
            (*f_pos) += data_size;

            return data_size;
        }
    }
    else
    {
        return 0;
    }
}

/*
 * File write function
 *  Parameters:
 *   filp  - a type file structure;
 *   buf   - a buffer in which the user space function (fwrite) will write;
 *   len - a counter with the number of bytes to transfer, which has the same
 *           values as the usual counter in the user space function (fwrite);
 *   f_pos - a position of where to start writing in the file;
 *  Operation:
 *   The function copy_from_user transfers the data from user space to kernel space.
 */
static ssize_t gpio_driver_write(struct file *filp, const char *buf, size_t len, loff_t *f_pos)
{
    /* Reset memory. */
    memset(gpio_driver_buffer, 0, BUF_LEN);

    /* Get data from user space.*/
    if (copy_from_user(gpio_driver_buffer, buf, len) != 0)
    {
        return -EFAULT;
    }
    else
    {
        if (strstr(gpio_driver_buffer, "LED") && strlen(gpio_driver_buffer) >= 5)
        {
            if ( gpio_driver_buffer[3] == '1')
            {
                if (gpio_driver_buffer[5] == '1')
                    SetGpioPin(GPIO_06);
                else
                    ClearGpioPin(GPIO_06);
            }
            else if (gpio_driver_buffer[3] == '2')
            {
                if (gpio_driver_buffer[5] == '1')
                    SetGpioPin(GPIO_13);
                else
                    ClearGpioPin(GPIO_13);
            }
            else if (gpio_driver_buffer[3] == '3')
            {
                if (gpio_driver_buffer[5] == '1')
                    SetGpioPin(GPIO_19);
                else
                    ClearGpioPin(GPIO_19);
            }
            else if (gpio_driver_buffer[3] == '4')
            {
                if (gpio_driver_buffer[5] == '1')
                    SetGpioPin(GPIO_26);
                else
                    ClearGpioPin(GPIO_26);
            }

            /* Reset memory. */
             memset(gpio_driver_buffer, 0, BUF_LEN);

            return len;
        }
        
        return 0;
    }
}
