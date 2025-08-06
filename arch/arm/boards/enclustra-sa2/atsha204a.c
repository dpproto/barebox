#include "atsha204a.h"
#include <stdio.h>
#include <driver.h>
#include <linux/libfdt.h>
#include <linux/kernel.h>			/* ARRAY_SIZE */
#include <linux/bitrev.h>			/* bitrev16 */
#include <i2c/i2c.h>
#include "crc16.h"

#define ATSHA204A_TWLO_US					(60)
#define ATSHA204A_TWHI_US					(2500)
#define ATSHA204A_EXECTIME_US				(5000)
#define ATSHA204A_TRANSACTION_TIMEOUT		(100000)
#define ATSHA204A_TRANSACTION_RETRY			(5)

enum atsha204a_status
{
	ATSHA204A_STATUS_SUCCESS	= 0x00,
	ATSHA204A_STATUS_MISCOMPARE	= 0x01,
	ATSHA204A_STATUS_PARSE_ERROR	= 0x03,
	ATSHA204A_STATUS_EXEC_ERROR	= 0x0F,
	ATSHA204A_STATUS_AFTER_WAKE	= 0x11,
	ATSHA204A_STATUS_CRC_ERROR	= 0xFF,
};

enum atsha204a_func
{
	ATSHA204A_FUNC_RESET	= 0x00,
	ATSHA204A_FUNC_SLEEP	= 0x01,
	ATSHA204A_FUNC_IDLE		= 0x02,
	ATSHA204A_FUNC_COMMAND	= 0x03,
};

enum atsha204a_zone
{
	ATSHA204A_ZONE_CONFIG	= 0,
	ATSHA204A_ZONE_OTP		= 1,
	ATSHA204A_ZONE_DATA		= 2,
};

enum atsha204a_cmd
{
	ATSHA204A_CMD_READ		= 0x02,
	ATSHA204A_CMD_RANDOM	= 0x1B,
};

/**
 * @brief A response from the device to the host
 */
struct atsha204a_resp
{
	uint8_t length;			/**< Number of bytes in the struct, including \a length and \a code */
	uint8_t code;			/**< Op code that must match the last command */
	uint8_t data[82];		/**< Data buffer */
} __attribute__ ((packed));

struct atsha204a_req
{
	u8 function;
	u8 length;
	u8 command;
	u8 param1;
	u16 param2;
	u8 data[78];
} __attribute__ ((packed));

/**
 * @brief Calculate a CRC
 * @param[in] buffer Data on which the CRC must be calculated
 * @param[in] len Number of bytes in \a buffer
 *
 * For example, afer wake-up, the data read from the device is `0x04 0x11 0x33 0x43`.
 * The 1st byte is the packet length, the 2nd byte is the op code and the last
 * 2 bytes are the CRC, with the bytes swapped.
 * The function must be called with the 1st 2 bytes and if it returns 0x4333,
 * then the CRC is valid.
 *
 * @return The CRC.
 */
static inline u16 atsha204a_crc16(const u8 *buffer, size_t len)
{
	debug("%s() >> len = %u, buffer =", __FUNCTION__, len);
	for (size_t i=0 ; i<len ; i++) {
		debug(" 0x%02x", buffer[i]);
	}
	debug("\n");
	return bitrev16(crc16(0, buffer, len));
}

/**
 * @brief Get the device from the devicetree
 * @return A pointer to the device if found, or \t NULL otherwise.
 */
static struct device *atsha204a_get_dev(void)
{
	struct device *dev;
	struct i2c_client *client;

    dev = get_device_by_name("atsha204a0");
	if (dev == NULL) {
		printf("%s() >> ERROR: can't find device\n", __FUNCTION__);
		return NULL;
	}
	client = to_i2c_client(dev);
	debug("%s() >> ATASHA204a found at I2C address 0x%02x\n", __FUNCTION__, client->addr);

	return dev;
}

/**
 * @brief Send one message to the device
 * @param[in] dev A pointer to the device, returned by #atsha204a_get_dev()
 * @param[in] buf The data to send
 * @param[in] len The number of bytes in \a buf
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int atsha204a_send(struct device *dev, const uint8_t *buf, uint8_t len)
{
	int ret;
	struct i2c_client *client;
	client = to_i2c_client(dev);
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.buf	= (uint8_t*) buf,
			.len	= len,
		}
	};
	debug("%s() >> dev addr = 0x%02x\n", __FUNCTION__, client->addr);

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * @brief Read from the device
 * @param[in] dev A pointer to the device, returned by #atsha204a_get_dev()
 * @param[in] buf The data to send
 * @param[in] len The number of bytes in \a buf
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int atsha204a_recv(struct device *dev, uint8_t *buf, uint8_t len)
{
	int ret;
	struct i2c_client *client;
	client = to_i2c_client(dev);
	/* flags: this is a read operation and generate a stop condition */
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.buf	= (uint8_t*) buf,
			.len	= len,
			.flags  = I2C_M_RD | I2C_M_STOP,
		}
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		printf("%s() >> ERROR: ret = %d\n", __FUNCTION__, ret);
		return ret;
	}

	return 0;
}

/**
 * @brief Read from the device and check the CRC
 * @param[in] dev A pointer to the device, returned by #atsha204a_get_dev()
 * @param[in] resp The response from the device
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int atsha204a_recv_resp(struct device *dev, struct atsha204a_resp *resp)
{
	int ret;
	uint16_t resp_crc, computed_crc;
	uint8_t *p = (uint8_t *) resp;

	ret = atsha204a_recv(dev, p, 4);
	if (ret) {
		return ret;
	}
	debug("%s() >> resp:", __FUNCTION__);
	for (size_t i=0 ; i<4 ; i++)
		debug(" 0x%02x", p[i]);
	debug("\n%s() >> length=0x%02x, code=0x%02x, data[0]=0x%02x, data[1]=0x%02x\n", __FUNCTION__,
		   resp->length, resp->code, resp->data[0], resp->data[1]);

	if (resp->length > 4) {
		if (resp->length > sizeof(*resp)) {
			printf("%s() >> ERROR: resp->length %d > 4\n", __FUNCTION__, resp->length);
			return -EMSGSIZE;
		}
		ret = atsha204a_recv(dev, p + 4, resp->length - 4);
		if (ret)
			return ret;
	}

	debug("%s() >> checking CRC... resp->length = %d\n", __FUNCTION__, resp->length);
	resp_crc = (uint16_t) p[resp->length - 2]
		   | (((uint16_t) p[resp->length - 1]) << 8);
	computed_crc = atsha204a_crc16(p, resp->length - 2);

	if (resp_crc != computed_crc) {
		printf("%s() >> ERROR: Invalid CRC. Received: 0x%04x; computed: 0x%04x\n", __FUNCTION__,
			   resp_crc, computed_crc);
		return -EBADMSG;
	}
	debug("%s() >> CRC OK: 0x%04x\n", __FUNCTION__, resp_crc);
	return 0;
}

/**
 * @brief Put the device to sleep
 * @param[in] dev A pointer to the device, returned by #atsha204a_get_dev()
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int atsha204a_sleep(struct device *dev)
{
	int ret;
	uint8_t req = ATSHA204A_FUNC_SLEEP;

	for (int i = 1 ; i < 10 ; i++) {
		ret = atsha204a_send(dev, &req, 1);
		if (!ret) {
			debug("%s() >> sleeping! Trial #%d\n", __FUNCTION__, i);
			break;
		}
		udelay(ATSHA204A_EXECTIME_US);
	}

	return ret;
}

/**
 * @brief Wake up the device
 * @param[in] dev A pointer to the device, returned by #atsha204a_get_dev()
 *
 * See datasheet §5.3.2 Synchronization Procedures.
 *
 * @return 0 on success, a negative value from `asm-generic/errno.h` on error.
 */
static int atsha204a_wakeup(struct device *dev)
{
	uint8_t buf = 0x00;
	struct atsha204a_resp resp;
	int ret;
	struct i2c_client *client;
	client = to_i2c_client(dev);

	for (int i = 1; i <= 10; i++) {
		/*
		 * The device ignores any levels or transitions on the SCL pin
		 * when the device is idle, asleep or during waking up.
		 * Generate the wake condition: set SDA low for at least t_WLO.
		 */
		struct i2c_msg msg;

		msg.addr = 0;
		msg.flags = I2C_M_IGNORE_NAK;
		msg.len = 1;
		msg.buf = &buf;
		i2c_transfer(client->adapter, &msg, 1);		/* don't check errors: there is always one */

		udelay(ATSHA204A_TWLO_US + ATSHA204A_TWHI_US);

		ret = atsha204a_recv_resp(dev, &resp);
		if (ret == -EBADMSG) {
			debug("%s() >> WARN: CRC error. Retrying...\n", __FUNCTION__);
			continue;	/* retry on CRC error */
		}
		else if (ret) {
			printf("%s() >> ERROR: no response\n", __FUNCTION__);
			return ret;
		}

		if (resp.code != ATSHA204A_STATUS_AFTER_WAKE) {
			printf("%s() >> ERROR: bad response, code = %02x, expected = 0x11\n",
				   __FUNCTION__, resp.code);
			return -EBADMSG;
		}

		return 0;
	}

	return -ETIMEDOUT;
}

static void atsha204a_req_crc32(struct atsha204a_req *req)
{
	u8 *p = (u8 *) req;
	u16 computed_crc;
	u16 *crc_ptr = (u16 *) &p[req->length - 1];

	/* The buffer to crc16 starts at byte 1, not 0 */
	computed_crc = atsha204a_crc16(p + 1, req->length - 2);

	*crc_ptr = cpu_to_le16(computed_crc);
}

static int atsha204a_transaction(struct device *dev, struct atsha204a_req *req,
				struct atsha204a_resp *resp)
{
	int ret, timeout = ATSHA204A_TRANSACTION_TIMEOUT;

	ret = atsha204a_send(dev, (u8 *) req, req->length + 1);
	if (ret) {
		printf("%s() >> ERROR: transaction send failed\n", __FUNCTION__);
		return -EBUSY;
	}

	do {
		udelay(ATSHA204A_EXECTIME_US);
		ret = atsha204a_recv_resp(dev, resp);
		if (!ret || ret == -EMSGSIZE || ret == -EBADMSG) {
			break;
		}

		debug("%s() >> polling for response "
		      "(timeout = %d)\n", __FUNCTION__, timeout);

		timeout -= ATSHA204A_EXECTIME_US;
	} while (timeout > 0);

	if (timeout <= 0) {
		printf("%s() >> ERROR: transaction timed out\n", __FUNCTION__);
		return -ETIMEDOUT;
	}

	return ret;
}

static int atsha204a_read(struct device *dev, enum atsha204a_zone zone, bool read32,
		  u16 addr, u8 *buffer)
{
	int res, retry = ATSHA204A_TRANSACTION_RETRY;
	struct atsha204a_req req;
	struct atsha204a_resp resp;

	req.function = ATSHA204A_FUNC_COMMAND;
	req.length = 7;
	req.command = ATSHA204A_CMD_READ;

	req.param1 = (u8) zone;
	if (read32)
		req.param1 |= 0x80;

	req.param2 = cpu_to_le16(addr);

	atsha204a_req_crc32(&req);

	do {
		res = atsha204a_transaction(dev, &req, &resp);
		if (!res)
			break;

		debug("ATSHA204A read retry (%d)\n", retry);
		retry--;
		atsha204a_wakeup(dev);
	} while (retry >= 0);

	if (res) {
		debug("ATSHA204A read failed\n");
		return res;
	}

	if (resp.length != (read32 ? 32 : 4) + 3) {
		debug("ATSHA204A read bad response length (%d)\n",
		      resp.length);
		return -EBADMSG;
	}

	memcpy(buffer, ((u8 *) &resp) + 1, read32 ? 32 : 4);

	return 0;
}

int atsha204_get_mac(uint8_t *buffer)
{
	int ret;
	uint8_t data[4];
	struct device *dev;

	dev = atsha204a_get_dev();
	if (dev == NULL) {
		return -ENODEV;
	}

	/* put the device to sleep to make sure it is in a defined state */
	ret = atsha204a_sleep(dev);
	if (ret) {
		printf("%s() >> ERROR: can't put the device to sleep; ret = %d\n", __FUNCTION__, ret);
		return ret;
	}

	ret = atsha204a_wakeup(dev);
	if (ret) {
		printf("%s() >> ERROR: can't wake up the device; ret = %d\n", __FUNCTION__, ret);
		return ret;
	}

	ret = atsha204a_read(dev, ATSHA204A_ZONE_OTP, false,
			     4, data);
	if (ret) {
		return ret;
	}
	for (int i = 0; i < 4; i++) {
		buffer[i] = data[i];
	}

	ret = atsha204a_read(dev, ATSHA204A_ZONE_OTP, false,
			     5, data);
	if (ret) {
		return ret;
	}
	buffer[4] = data[0];
	buffer[5] = data[1];

	atsha204a_sleep(dev);
	debug("%s() >> MAC address: ", __FUNCTION__);
	for (int i = 0; i <= 5; i++) {
		debug("%02x", buffer[i]);
		if (i != 5) {
			debug(":");
		}
	}
	debug("\n");

    return 0;
}
