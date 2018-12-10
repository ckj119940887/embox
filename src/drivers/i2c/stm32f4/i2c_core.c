/**
 * @file
 * @brief I2C driver for STM32
 *
 * @date    10.12.2018
 * @author  Alex Kalmuk
 */

#include <errno.h>
#include <string.h>
#include <assert.h>
#include <util/log.h>
#include <embox/unit.h>
#include <kernel/irq.h>

#include <drivers/i2c/i2c.h>

#include "stm32f4_discovery.h"

#define I2C1_EV_IRQ_NUM 47
#define I2C1_ER_IRQ_NUM 48

static I2C_HandleTypeDef i2c1_handle;

EMBOX_UNIT_INIT(stm32_i2c_init_all);

static int stm32_i2c_master_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
		int num);

static const struct i2c_algorithm stm32_i2c_algo = {
		.i2c_master_xfer = stm32_i2c_master_xfer,
		.i2c_functionality = NULL,
};

struct stm32_i2c {
	int ev_irq_nr;
	int er_irq_nr;
};

static struct stm32_i2c stm32_i2c1_priv = {
	.ev_irq_nr = I2C1_EV_IRQ_NUM,
	.er_irq_nr = I2C1_ER_IRQ_NUM
};

static struct i2c_adapter stm32_i2c1_adapter = {
	.i2c_algo_data = &stm32_i2c1_priv,
	.i2c_algo = &stm32_i2c_algo,
};

static I2C_HandleTypeDef *stm32_i2c_get_handle(struct stm32_i2c *adapter) {
	I2C_HandleTypeDef *i2c_handle;

	switch (adapter->ev_irq_nr) {
	case I2C1_EV_IRQ_NUM:
		i2c_handle = &i2c1_handle;
		break;
	default:
		log_error("Unsupported I2C bus\n");
		return NULL;
	}
	return i2c_handle;
}

static int stm32_i2c_slave_select(struct stm32_i2c *adapter, int slave_addr) {
	I2C_HandleTypeDef *i2c_handle = stm32_i2c_get_handle(adapter);

	i2c_handle->Init.OwnAddress1 = slave_addr;
	i2c_handle->Init.OwnAddress2 = 0;

	if (HAL_I2C_Init(i2c_handle) != HAL_OK) {
		log_error("HAL_I2C_Init failed\n");
		return -1;
	}

	return 0;
}

static int stm32_i2c_current_slave_addr(struct stm32_i2c *adapter) {
	I2C_HandleTypeDef *i2c_handle = stm32_i2c_get_handle(adapter);

	return i2c_handle->Init.OwnAddress1;
}

static int stm32_i2c_rx(struct stm32_i2c *adapter, uint16_t addr,
		uint8_t *buf, size_t len) {
	I2C_HandleTypeDef *i2c_handle = stm32_i2c_get_handle(adapter);

	while (HAL_I2C_GetState(i2c_handle) != HAL_I2C_STATE_READY)
		;
	if (HAL_I2C_Master_Receive_IT(i2c_handle, addr, buf, len) != HAL_OK) {
		return -1;
	}

	while (HAL_I2C_GetState(i2c_handle) != HAL_I2C_STATE_READY)
		;
	return HAL_I2C_GetError(i2c_handle) == HAL_I2C_ERROR_AF ? -1 : len;
}

static int stm32_i2c_tx(struct stm32_i2c *adapter, uint16_t addr,
		uint8_t *buf, size_t len) {
	I2C_HandleTypeDef *i2c_handle = stm32_i2c_get_handle(adapter);

	while (HAL_I2C_GetState(i2c_handle) != HAL_I2C_STATE_READY)
		;
	if (HAL_I2C_Master_Transmit_IT(i2c_handle, addr, buf, len) != HAL_OK) {
		return -1;
	}
	while (HAL_I2C_GetState(i2c_handle) != HAL_I2C_STATE_READY)
		;
	return HAL_I2C_GetError(i2c_handle) == HAL_I2C_ERROR_AF ? -1 : len;
}

static int stm32_i2c_master_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
		int num) {
	struct stm32_i2c *stm32_adapter;
	int i;
	int res = -1;

	stm32_adapter = adapter->i2c_algo_data;

	for (i = 0; i < num; i++) {
		if (stm32_i2c_current_slave_addr(stm32_adapter) != msgs->addr) {
			stm32_i2c_slave_select(stm32_adapter, msgs->addr);
		}

		if (msgs[i].flags & I2C_M_RD) {
			res = stm32_i2c_rx(stm32_adapter, msgs->addr, msgs->buf, msgs->len);
		} else {
			res = stm32_i2c_tx(stm32_adapter, msgs->addr, msgs->buf, msgs->len);
		}
	}

	return res;
}

static irq_return_t i2c_ev_irq_handler(unsigned int irq_nr, void *data) {
	I2C_HandleTypeDef *i2c_handle;

	switch (irq_nr) {
	case I2C1_EV_IRQ_NUM:
		i2c_handle = &i2c1_handle;
		break;
	default:
		log_error("Unsupported I2C number\n");
		return -1;
	}

	HAL_I2C_EV_IRQHandler(i2c_handle);
	return IRQ_HANDLED;
}

static irq_return_t i2c_er_irq_handler(unsigned int irq_nr, void *data) {
	I2C_HandleTypeDef *i2c_handle;

	switch (irq_nr) {
	case I2C1_ER_IRQ_NUM:
		i2c_handle = &i2c1_handle;
		break;
	default:
		log_error("Unsupported I2C number\n");
		return -1;
	}

	HAL_I2C_ER_IRQHandler(i2c_handle);
	return IRQ_HANDLED;
}

int stm32_i2c_init(int i2c_nr, const char *name) {
	int res = 0;
	int ev_irq_nr, er_irq_nr;
	I2C_TypeDef *i2c;
	I2C_HandleTypeDef *i2c_handle;
	struct i2c_adapter *adapter;

	switch (i2c_nr) {
	case 1:
		i2c = I2C1;
		i2c_handle = &i2c1_handle;
		adapter = &stm32_i2c1_adapter;
		ev_irq_nr = 47;
		er_irq_nr = 48;
		break;
	default:
		log_error("Unsupported I2C number\n");
		return -1;
	}

	i2c_handle->Instance             = i2c;

	i2c_handle->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
	i2c_handle->Init.ClockSpeed      = 400000;
	i2c_handle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	i2c_handle->Init.DutyCycle       = I2C_DUTYCYCLE_16_9;
	i2c_handle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	i2c_handle->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

	res |= irq_attach(ev_irq_nr, i2c_ev_irq_handler, 0, NULL, "I2C events");
	if (res < 0) {
		log_error("irq_attach failed\n");
		return -1;
	}
	res |= irq_attach(er_irq_nr, i2c_er_irq_handler, 0, NULL, "I2C errors");
	if (res < 0) {
		log_error("irq_attach failed\n");
		return -1;
	}

	i2c_bus_register(adapter, 1, name);

	return 0;
}

static int stm32_i2c_init_all(void) {
	int res;

	if ((res = stm32_i2c_init(1, "i2c1")) < 0) {
		log_error("stm32_i2c_init failed with error=%d", res);
		return -1;
	}

	return 0;
}
