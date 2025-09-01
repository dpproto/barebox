#include <i2c/i2c.h>
#include <clock.h>
#include <linux/kernel.h>               /* ARRAY_SIZE */
#include "si5338_config.h"
#include "Si5338-RevB-Registers.h"

/**
 * @brief Get the device from the devicetree
 * @return A pointer to the device if found, or \t NULL otherwise.
 */
static struct device *get_dev(void)
{
    struct device *dev;
    struct i2c_client *client;

    dev = get_device_by_name("si53380");
    if (dev == NULL) {
        printf("%s() >> ERROR: can't find device SI5338\n", __FUNCTION__);
        return NULL;
    }
    client = to_i2c_client(dev);
    debug("%s() >> SI5338 found at I2C address 0x%02x\n", __FUNCTION__, client->addr);

    return dev;
}

/**
 * @brief Write a single byte to a register in the SI5338
 * @param[in] dev The I²C device.
 * @param[in] addr The register address.
 * @param[in] data The byte to be written to the register.
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int i2c_write_simple(struct device *dev, u8 addr, u8 data)
{
    int ret;
    struct i2c_client *client;
    client = to_i2c_client(dev);
    u8 buffer[2];

    buffer[0] = addr;
    buffer[1] = data;

    struct i2c_msg msg[] = {
        {
            .addr   = client->addr,
            .buf    = buffer,
            .len    = 2,
            .flags   = 0,
        }
    };
    debug("%s() >> dev addr = 0x%02x\n", __FUNCTION__, client->addr);

    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ret < 0) {
        printf("%s() >> ERROR: SI5338 write failed addr: %02x, data: %02x\n", __FUNCTION__, addr, data);
        return ret;
    }

    return 0;
}

/**
 * @brief Change some bits in a register in the SI5338
 * @param[in] dev The I²C device.
 * @param[in] addr The register address.
 * @param[in] data The byte to be written to the register.
 * @param[in] mask Sets which bits in the register will change.
 *
 * The bits in the register are allowed to change if the corresponding bit in \a mask is 1.
 *
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int i2c_write_masked(struct device *dev, u8 addr, u8 data, u8 mask)
{
    if (mask == 0x00) {
        return 0;
    }
    if (mask == 0xff) {
        return i2c_write_simple(dev, addr, data);
    }

    int ret;
    struct i2c_client *client;
    client = to_i2c_client(dev);
    u8 buffer[2];

    buffer[0] = addr;
    buffer[1] = data;
    struct i2c_msg msg[] = {
        {
            .addr   = client->addr,
            .buf    = buffer,
            .len    = 2,
            .flags  = I2C_M_RD,
        }
    };

    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ret < 0) {
        printf("%s() >> ERROR: SI5338 read failed addr: %02x\n", __FUNCTION__, addr);
        return ret;
    }
    msg[0].buf[1] &= ~mask;
    msg[0].buf[1] |= data & mask;
    msg[0].flags &= ~I2C_M_RD;
    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ret < 0) {
        printf("%s() >> ERROR: SI5338 write failed addr: %02x, data: %02x\n", __FUNCTION__, addr, data);
        return ret;
    }
    return 0;
}

/**
 * @brief Read a single byte from a register in the SI5338
 * @param[in] dev The I²C device.
 * @param[in] addr The register address.
 * @param[out] data The byte read from the register.
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int i2c_read_register(struct device *dev, u8 addr, u8 *data)
{
    int ret;
    struct i2c_client *client;
    client = to_i2c_client(dev);
    u8 buffer[2];

    buffer[0] = addr;
    buffer[1] = 0x00;
    struct i2c_msg msg[] = {
        {
            .addr   = client->addr,
            .buf    = buffer,
            .len    = 2,
            .flags  = I2C_M_RD,
        }
    };

    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ret < 0) {
        printf("%s() >> ERROR: SI5338 read failed addr: %02x\n", __FUNCTION__, addr);
        return ret;
    }
    *data = msg[0].buf[1];

    return 0;
}

/**
 * @brief Validate input clock status
 * @param[in] dev The I²C device.
 *
 * Loop until the \c LOS_CLKIN bit is clear.
 *
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int check_input_clock(struct device *dev)
{
    // validate input clock status
    int ret;
    struct i2c_client *client;
    client = to_i2c_client(dev);
    u8 buffer[2] = { 218, 0 };
    struct i2c_msg msg[] = {
        {
            .addr   = client->addr,
            .buf    = buffer,
            .len    = 2,
            .flags  = I2C_M_RD,
        }
    };

    do {
        ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
        if (ret < 0) {
            printf("%s() >> ERROR: SI5338 read failed addr: %02x\n", __FUNCTION__, msg[0].addr);
            return ret;
        }
    } while (msg[0].buf[1] & 0x04);

    return 0;
}

/**
 * @brief Check output PLL status
 * @param[in] dev The I²C device.
 *
 * Loop until the \c PLL_LOL, \c LOS_CLKIN and \c SYS_CAL bits are clear.
 *
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error
 * (-EIO if too many trials).
 */
static int check_pll(struct device *dev)
{
    int ret;
    int try = 0;
    u8 data;

    do {
        ret = i2c_read_register(dev, 218, &data);
        if (ret < 0) {
            return ret;
        }
        mdelay(100);
        try++;
        if (try > 10) {
            printf("%s() >> ERROR: SI5338 PLL is not locking\n", __FUNCTION__);
            return -EIO;
        }
    } while (data & 0x15);

    return 0;
}

int si5338_init(void)
{
    unsigned char buf[1];
    struct device *dev;
    int ret;

    dev = get_dev();
    if (dev == NULL) {
        return -ENODEV;
    }

    /* Set PAGE_SEL bit to 0. If bit is 1, registers with address
     * greater than 255 can be addressed.
     */
    if (i2c_write_simple(dev, 255, 0x00)) {
        return -1;
    }

    // disable outputs
    if (i2c_write_masked(dev, 230, 0x10, 0x10)) {
        return -1;
    }

    // pause lol
    if (i2c_write_masked(dev, 241, 0x80, 0x80)) {
        return -1;
    }

    // write new configuration
    for (int i = 0; i < NUM_REGS_MAX; i++) {
        if (i2c_write_masked(dev, Reg_Store[i].Reg_Addr, Reg_Store[i].Reg_Val,
                             Reg_Store[i].Reg_Mask)) {
            return -1;
        }
    }

    ret = check_input_clock(dev);
    if (ret) {
        return ret;
    }

    // configure PLL for locking
    ret = i2c_write_masked(dev, 49, 0, 0x80);
    if (ret) {
        return ret;
    }

    // initiate locking of PLL
    ret = i2c_write_simple(dev, 246, 0x02);
    if (ret) {
        return ret;
    }

    // wait 25ms (100ms to be on the safe side)
    mdelay(100);

    // restart lol
    ret = i2c_write_masked(dev, 241, 0x65, 0xff);
    if (ret) {
        return ret;
    }

    ret = check_pll(dev);
    if (ret) {
        return ret;
    }

    // copy fcal values to active registers: FCAL[17:16]
    ret = i2c_read_register(dev, 237, buf);
    if (ret) {
        return ret;
    }
    ret = i2c_write_masked(dev, 47, buf[0], 0x03);
    if (ret) {
        return ret;
    }

    // copy fcal values to active registers: FCAL[15:8]
    ret = i2c_read_register(dev, 236, buf);
    if (ret) {
        return ret;
    }
    ret = i2c_write_simple(dev, 46, buf[0]);
    if (ret) {
        return ret;
    }

    // copy fcal values to active registers: FCAL[7:0]
    ret = i2c_read_register(dev, 235, buf);
    if (ret) {
        return ret;
    }
    ret = i2c_write_simple(dev, 45, buf[0]);
    if (ret) {
        return ret;
    }

    // Must write 000101b to these bits if the device is not factory programmed.
    ret = i2c_write_masked(dev, 47, 0x14, 0xFC);
    if (ret) {
        return ret;
    }

    // set PLL to use FCAL values
    ret = i2c_write_masked(dev, 49, 0x80, 0x80);
    if (ret) {
        return ret;
    }

    // enable outputs
    ret = i2c_write_simple(dev, 230, 0x00);
    if (ret) {
        return ret;
    }

    printf("SI5338 init successful\n");

    return 0;
}
