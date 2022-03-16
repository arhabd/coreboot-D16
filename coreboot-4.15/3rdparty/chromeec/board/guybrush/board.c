/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "base_fw_config.h"
#include "board_fw_config.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accel_bma422.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "temp_sensor/thermistor.h"
#include "temp_sensor/tmp112.h"
#include "usb_mux.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Motion sensor mutex */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Motion sensor private data */
static struct bmi_drv_data_t g_bmi160_data;
static struct bmi3xx_drv_data g_bmi323_data;
static struct accelgyro_saved_data_t g_bma422_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(1),  0},
	{ 0, 0,  FLOAT_TO_FP(-1)}
};

const mat33_fp_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0,  FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI323,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi3xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi323_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI3_ADDR_I2C_PRIM,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA422,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma4_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma422_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA4_I2C_ADDR_PRIMARY,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA4_ACCEL_MIN_FREQ,
		.max_frequency = BMA4_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, enough for laptop. */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI323,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi3xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi323_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI3_ADDR_I2C_PRIM,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t bmi160_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI160,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi160_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_bmi160_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	.rot_standard_ref = &base_standard_ref,
	.min_frequency = BMI_ACCEL_MIN_FREQ,
	.max_frequency = BMI_ACCEL_MAX_FREQ,
	.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 0,
		},
	},
};

struct motion_sensor_t bmi160_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI160,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi160_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_bmi160_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref,
	.min_frequency = BMI_GYRO_MIN_FREQ,
	.max_frequency = BMI_GYRO_MAX_FREQ,
};

__override enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	/* Set channel A output swing */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_A_SWING,
		PS8811_CHAN_A_SWING_MASK, 0x2 << PS8811_CHAN_A_SWING_SHIFT));

	/* Set channel B output swing */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_SWING,
		PS8811_CHAN_B_SWING_MASK, 0x2 << PS8811_CHAN_B_SWING_SHIFT));

	/* Set channel B de-emphasis to -6dB and pre-shoot to 1.5 dB */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
		PS8811_CHAN_B_DE_PS_LSB_MASK, PS8811_CHAN_B_DE_6_PS_1_5_LSB));

	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
		PS8811_CHAN_B_DE_PS_MSB_MASK, PS8811_CHAN_B_DE_6_PS_1_5_MSB));

	return EC_SUCCESS;
}

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 * TODO(b/179036200): Adjust PS8818 tuning for guybrush reference
 */
__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_RX_PHY,
					PS8818_RX_INPUT_TERM_MASK,
					PS8818_RX_INPUT_TERM_112_OHM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_DPEQ_LEVEL,
					PS8818_DPEQ_LEVEL_UP_MASK,
					PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 0);
	}

	return rv;
}

/*
 * ANX7491(A1) and ANX7451(C1) are on the same i2c bus. Both default
 * to 0x29 for the USB i2c address. This moves ANX7451(C1) USB i2c
 * address to 0x2A. ANX7491(A1) will stay at the default 0x29.
 */
uint16_t board_anx7451_get_usb_i2c_addr(const struct usb_mux *me)
{
	ASSERT(me->usb_port == USBC_PORT_C1);
	return 0x2a;
}

/*
 * Base Gyro Sensor dynamic configuration
 */
static int base_gyro_config;

static void board_update_motion_sensor_config(void)
{
	if (board_is_convertible()) {
		if (get_board_version() == 1) {
			motion_sensors[BASE_ACCEL] = bmi160_base_accel;
			motion_sensors[BASE_GYRO] = bmi160_base_gyro;
			base_gyro_config = BASE_GYRO_BMI160;
			ccprints("BASE GYRO is BMI160");
		} else {
			base_gyro_config = BASE_GYRO_BMI323;
			ccprints("BASE GYRO is BMI323");
		}

		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable Base Accel and Gyro interrupt */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_gyro_config) {
	case BASE_GYRO_BMI160:
		bmi160_interrupt(signal);
		break;
	case BASE_GYRO_BMI323:
	default:
		bmi3xx_interrupt(signal);
		break;
	}
}

static void board_init(void)
{
	board_update_motion_sensor_config();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	if (get_board_version() > 1)
		tmp112_init();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup,
	     HOOK_PRIO_DEFAULT);

int board_get_soc_temp(int idx, int *temp_k)
{
	uint32_t board_version = get_board_version();

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	if (board_version == 1)
		return get_temp_3v3_30k9_47k_4050b(idx, temp_k);

	return tmp112_get_val(idx, temp_k);
}

#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif
