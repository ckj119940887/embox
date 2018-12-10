/**
 * @file
 * @brief
 *
 * @date    29.03.2017
 * @author  Alex Kalmuk
 */

#include <unistd.h>
#include <util/log.h>
#include <drivers/i2c/i2c.h>

#include "gy_30.h"

#define GY30_ADDR       0x46

uint16_t gy_30_read_light_level(void) {
	uint16_t level = 0;

	while (0 > i2c_bus_read(1, GY30_ADDR, (uint8_t *) &level, 2))
		;

	/* The actual value in lx is level / 1.2 */
	return (((uint32_t) level) * 5) / 6;
}

void gy_30_setup_mode(uint8_t mode) {
	while (0 > i2c_bus_write(1, GY30_ADDR, &mode, 1))
		;

	/* Documentation says that we need to wait approximately 180 ms here.*/
	usleep(180000);

	log_debug("%s\n", "gy_30_setup_mode OK!\n");
}
