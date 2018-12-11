/**
 * @file
 *
 * @date Nov 16, 2018
 * @author Anton Bondarev
 */
#include <util/log.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/reg.h>

#include <drivers/i2c/i2c.h>

#include "imx_i2c.h"

static int imx_i2c_master_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
		int num);

const struct i2c_algorithm imx_i2c_algo = {
		.i2c_master_xfer = imx_i2c_master_xfer,
};

static void delay(int i) {
	volatile int cnt;
	for (cnt = 0; cnt < i * 100 ; cnt++) {
	}
}

static int imx_i2c_bus_busy(struct imx_i2c *adapter, int for_busy) {
	uint32_t temp;
	volatile int i;

	for (i = 0; i < 100; i++) {
		temp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2SR);
		/* check for arbitration lost */
		if (temp & IMX_I2C_I2SR_IAL) {
			temp &= ~IMX_I2C_I2SR_IAL;
			REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, temp);
			log_error("arbitration lost");
			return -EAGAIN;
		}

		if (for_busy && (temp & IMX_I2C_I2SR_IBB) ) {
			return 0;
		}
		if (!for_busy && !(temp & IMX_I2C_I2SR_IBB) ) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int imx_i2c_trx_complete(struct imx_i2c *adapter) {
	uint32_t temp;
	volatile int i;

	for (i = 0; i < 10000; i++) {
		temp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2SR);

		if ( temp & IMX_I2C_I2SR_IIF) {
			return 0;
		}
		delay(10);
	}
	return -ETIMEDOUT;
}

static int imx_i2c_tx(struct imx_i2c *adapter, uint16_t addr, uint8_t *buff, size_t sz) {
	int res = -1;
	int i;

	REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
	/* write slave address */
	REG8_STORE(adapter->base_addr + IMX_I2C_I2DR, (uint8_t)(((addr << 1)) & 0xFF));
	res = imx_i2c_trx_complete(adapter);
	if (res) {
		goto out;
	}
	if (REG8_LOAD(adapter->base_addr + IMX_I2C_I2SR) &  IMX_I2C_I2SR_RXAK) {
		res = -ENODEV;
		goto out;
	}

	log_debug("ACK received 0x%x", addr);
	res = 0;
	for (i = 0; i < sz; i++) {
		log_debug("write byte: B%d=0x%X", i, buff[i]);
		REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
		REG8_STORE(adapter->base_addr + IMX_I2C_I2DR, buff[i]);
		res = imx_i2c_trx_complete(adapter);
		if (res)
			return res;

		if (REG8_LOAD(adapter->base_addr + IMX_I2C_I2SR) &  IMX_I2C_I2SR_RXAK) {
			res = -ENODEV;
			goto out;
		}
		log_debug("byte sent addr (0x%x) val (0x%x)", addr, buff[i]);
	}

out:

	return res;
}

static int imx_i2c_stop(struct imx_i2c *adapter) {
	uint32_t tmp;

	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	tmp &= ~(IMX_I2C_I2CR_MTX | IMX_I2C_I2CR_TXAK);
	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	delay(1);
	tmp &= ~(IMX_I2C_I2CR_MSTA) ;
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);
	delay(10);
	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	tmp &= ~(IMX_I2C_I2CR_IEN) ;
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, 0);

	return 0;
}

static int imx_i2c_start(struct imx_i2c *adapter) {
	uint32_t tmp;

	REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0 );
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, IMX_I2C_I2CR_IEN );

	delay(100);

	/* Start I2C transaction */
	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	tmp |= IMX_I2C_I2CR_MSTA ;
	REG8_STORE(adapter->base_addr+ IMX_I2C_I2CR, tmp);

	tmp = imx_i2c_bus_busy(adapter, 1);
	if (tmp) {
		return -1;
	}

	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	tmp |= IMX_I2C_I2CR_MTX | IMX_I2C_I2CR_TXAK;
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);

	return 0;
}
#if 0
static int imx_i2c_tx_byte(struct imx_i2c *adapter, uint8_t byte) {
	REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
	REG8_STORE(adapter->base_addr + IMX_I2C_I2DR, byte);
	res = imx_i2c_trx_complete(adapter);
	if (res) {
		goto out;
	}

}
#endif

static int imx_i2c_rx(struct imx_i2c *adapter, uint16_t addr, uint8_t *buff, size_t sz) {
	int res = -1;
	int cnt;
	uint32_t tmp;

	REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
	/* write slave address */
	REG8_STORE(adapter->base_addr + IMX_I2C_I2DR, ((addr << 1) | 0x1) & 0xFF);
	res = imx_i2c_trx_complete(adapter);
	if (res) {
		goto out;
	}
	if (REG8_LOAD(adapter->base_addr + IMX_I2C_I2SR) &  IMX_I2C_I2SR_RXAK) {
		res = -ENODEV;
		goto out;
	}

	log_debug("ACK received 0x%x", addr);
	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
	tmp &= ~IMX_I2C_I2CR_MTX;
	if (sz > 1) {
		tmp &= ~IMX_I2C_I2CR_TXAK;
	}
	REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);

	REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
	/* dummy read */
	tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2DR);

	res = sz;

	for (cnt = sz; cnt > 0; cnt--) {
		tmp = imx_i2c_trx_complete(adapter);
		if (tmp) {
			log_debug("i2c complition error");
			res = -1;
			goto out;
		}

		log_debug("i2c complition ok");
		res = 1;

		if (cnt == 1) {
			/*
			 * It must generate STOP before read I2DR to prevent
			 * controller from generating another clock cycle
			 */
			tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
			tmp &= ~(IMX_I2C_I2CR_MTX | IMX_I2C_I2CR_MSTA);
			REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);
		}
		if (cnt == 2) {
			tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
			tmp |= IMX_I2C_I2CR_TXAK;
			REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);
		}

		imx_i2c_bus_busy(adapter, 0);
		REG8_STORE(adapter->base_addr + IMX_I2C_I2SR, 0);
		tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2DR);

		log_debug("read 0x%x ", tmp);
		*buff++ = (uint8_t)(tmp & 0xFF);
	}
out:
	return res;
}

static int imx_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
		int num) {
	struct imx_i2c *adapter;
	int res = -1;
	int i;
	uint32_t tmp;

	adapter = adap->i2c_algo_data;
	if (imx_i2c_start(adapter)) {
		log_error("i2c  bus error");
		res = -1;
		goto out;
	}

	for (i = 0; i < num; i ++) {
		if (i) {
			tmp = REG8_LOAD(adapter->base_addr + IMX_I2C_I2CR);
			tmp |= IMX_I2C_I2CR_RSTA;
			REG8_STORE(adapter->base_addr + IMX_I2C_I2CR, tmp);
			res = imx_i2c_bus_busy(adapter, 1);
			if (res)
				goto out;
		}
		if (msgs[i].flags & I2C_M_RD) {
			res = imx_i2c_rx(adapter, msgs->addr, msgs->buf, msgs->len);
		} else {
			res = imx_i2c_tx(adapter, msgs->addr, msgs->buf, msgs->len);
		}
	}

out:
	imx_i2c_stop(adapter);

	return res;
}
