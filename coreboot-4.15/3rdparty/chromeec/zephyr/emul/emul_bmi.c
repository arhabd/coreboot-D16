/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_bmi

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(emul_bmi);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "emul/emul_bmi.h"

#include "driver/accelgyro_bmi160.h"
#include "driver/accelgyro_bmi260.h"
#include "driver/accelgyro_bmi_common.h"

/**
 * Describe if there is no ongoing I2C message or if there is message handled
 * at the moment (last message doesn't ended with stop or write is not followed
 * by read).
 */
enum bmi_emul_msg_state {
	BMI_EMUL_NONE_MSG,
	BMI_EMUL_IN_WRITE,
	BMI_EMUL_IN_READ
};

/** Run-time data used by the emulator */
struct bmi_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** BMI device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct bmi_emul_cfg *cfg;

	/** Current state of all emulated BMI registers */
	uint8_t reg[BMI_EMUL_MAX_REG];
	/** Internal offset values used in calculations */
	int16_t off_acc_x;
	int16_t off_acc_y;
	int16_t off_acc_z;
	int16_t off_gyr_x;
	int16_t off_gyr_y;
	int16_t off_gyr_z;
	/** Internal values of sensors */
	int32_t acc_x;
	int32_t acc_y;
	int32_t acc_z;
	int32_t gyr_x;
	int32_t gyr_y;
	int32_t gyr_z;
	/** Current state of NVM where offset and configuration can be saved */
	uint8_t nvm[BMI_EMUL_MAX_NVM_REGS];

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;
	/**
	 * If effect of command is vissable after simulated time from issuing
	 * command
	 */
	bool simulate_command_exec_time;
	/** Return error when trying to read WO register */
	bool error_on_wo_read;

	/** Current state of I2C bus (if emulator is handling message) */
	enum bmi_emul_msg_state msg_state;
	/** Number of already handled bytes in ongoing message */
	int msg_byte;
	/** Register selected in last write command */
	uint8_t cur_reg;
	/** Value of data byte in ongoing write message */
	uint8_t write_byte;

	/** Custom write function called on I2C write opperation */
	bmi_emul_write_func write_func;
	/** Data passed to custom write function */
	void *write_func_data;
	/** Custom read function called on I2C read opperation */
	bmi_emul_read_func read_func;
	/** Data passed to custom read function */
	void *read_func_data;

	/** Control if read should fail on given register */
	int read_fail_reg;
	/** Control if write should fail on given register */
	int write_fail_reg;

	/** List of FIFO frames */
	struct bmi_emul_frame *fifo_frame;
	/** First FIFO frame in byte format */
	uint8_t fifo[21];
	/** Number of FIFO frames that were skipped */
	uint8_t fifo_skip;
	/** Currently accessed byte of first frame */
	int fifo_frame_byte;
	/** Length of first frame */
	int fifo_frame_len;

	/** Last time when emulator was resetted in sensor time units */
	int64_t zero_time;
	/** Time when current command should end */
	uint32_t cmd_end_time;

	/** Emulated model of BMI */
	int type;
	/** Pointer to data specific for emulated model of BMI */
	const struct bmi_emul_type_data *type_data;

	/** Mutex used to control access to emulator data */
	struct k_mutex data_mtx;
};

/** Static configuration for the emulator */
struct bmi_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Pointer to run-time data */
	struct bmi_emul_data *data;
	/** Address of BMI on i2c bus */
	uint16_t addr;
};

/** Check description in emul_bmi.h */
int bmi_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	return k_mutex_lock(&data->data_mtx, timeout);
}

/** Check description in emul_bmi.h */
int bmi_emul_unlock_data(struct i2c_emul *emul)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	return k_mutex_unlock(&data->data_mtx);
}

/** Check description in emul_bmi.h */
void bmi_emul_set_write_func(struct i2c_emul *emul,
			     bmi_emul_write_func func, void *data)
{
	struct bmi_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	emul_data->write_func = func;
	emul_data->write_func_data = data;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_read_func(struct i2c_emul *emul,
			    bmi_emul_read_func func, void *data)
{
	struct bmi_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	emul_data->read_func = func;
	emul_data->read_func_data = data;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct bmi_emul_data *data;

	if (reg < 0 || reg > BMI_EMUL_MAX_REG) {
		return;
	}

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->reg[reg] = val;
}

/** Check description in emul_bmi.h */
uint8_t bmi_emul_get_reg(struct i2c_emul *emul, int reg)
{
	struct bmi_emul_data *data;

	if (reg < 0 || reg > BMI_EMUL_MAX_REG) {
		return 0;
	}

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	return data->reg[reg];
}

/** Check description in emul_bmi.h */
void bmi_emul_set_read_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->read_fail_reg = reg;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_write_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->write_fail_reg = reg;
}

/**
 * @brief Convert @p val to two's complement representation. It makes sure that
 *        bit representation is correct even on platforms which represent
 *        signed inteager in different format. Unsigned bit representation
 *        allows to use well defined bitwise operations on returned value.
 *
 * @param val Inteager that is converted
 *
 * @return two's complement representation of @p val
 */
static uint32_t bmi_emul_val_to_twos_comp(int32_t val)
{
	uint32_t twos_comp_val;

	/* Make sure that value is converted to twos compliment format */
	if (val < 0) {
		twos_comp_val = (uint32_t)(-val);
		twos_comp_val = ~twos_comp_val + 1;
	} else {
		twos_comp_val = (uint32_t)val;
	}

	return twos_comp_val;
}

/**
 * @brief Convert accelerometer value from NVM format (8bit, 0x01 == 3.9mg)
 *        to internal offset format (16bit, 0x01 == 0.061mg).
 *
 * @param nvm Value in NVM format (8bit, 0x01 == 3.9mg). This is binary
 *            representation of two's complement signed number.
 *
 * @return offset Internal representation of @p nvm (16bit, 0x01 == 0.061mg)
 */
static int16_t bmi_emul_acc_nvm_to_off(uint8_t nvm)
{
	int16_t offset;
	int8_t sign;

	if (nvm & BIT(7)) {
		sign = -1;
		/* NVM value is in two's complement format */
		nvm = ~nvm + 1;
	} else {
		sign = 1;
	}

	offset = (int16_t)nvm;
	/* LSB in NVM is 3.9mg, while LSB in internal offset is 0.061mg */
	offset *= sign * 64;

	return offset;
}

/**
 * @brief Convert gyroscope value from NVM format (10bit, 0x01 == 0.061 °/s)
 *        to internal offset format (16bit, 0x01 == 0.0038 °/s)
 *
 * @param nvm Value in NVM format (10bit, 0x01 == 0.061 °/s). This is binary
 *            representation of two's complement signed number.
 *
 * @return offset Internal representation of @p nvm (16bit, 0x01 == 0.0038 °/s)
 */
static int16_t bmi_emul_gyr_nvm_to_off(uint16_t nvm)
{
	int16_t offset;
	int8_t sign;

	if (nvm & BIT(9)) {
		sign = -1;
		/* NVM value is in two's complement format */
		nvm = ~nvm + 1;
	} else {
		sign = 1;
	}

	/* Mask 10 bits which holds value */
	nvm &= 0x3ff;

	offset = (int16_t)nvm;
	/* LSB in NVM is 0.061°/s, while LSB in internal offset is 0.0038°/s */
	offset *= sign * 16;

	return offset;
}

/**
 * @brief Convert accelerometer value from internal offset format
 *        (16bit, 0x01 == 0.061mg) to NVM format (8bit, 0x01 == 7.8mg).
 *        Function makes sure that NVM value is representation of two's
 *        complement signed number.
 *
 * @param val Value in internal offset format (16bit, 0x01 == 0.061mg).
 *
 * @return nvm NVM format representation of @p val (8bit, 0x01 == 3.9mg)
 */
static uint8_t bmi_emul_acc_off_to_nvm(int16_t off)
{
	uint32_t twos_comp_val;
	uint8_t nvm = 0;

	twos_comp_val = bmi_emul_val_to_twos_comp(off);

	/*
	 * LSB in internal representation has value 0.061mg, while in NVM
	 * LSB is 3.9mg. Skip 0.06mg, 0.12mg, 0.24mg, 0.48mg, 0.97mg and
	 * 1.9mg bits.
	 */
	nvm |= (twos_comp_val >> 6) & 0x7f;
	/* Set sign bit */
	nvm |= (twos_comp_val & BIT(31)) ? BIT(7) : 0x00;

	return nvm;
}

/**
 * @brief Convert gyroscope value from internal offset format
 *        (16bit, 0x01 == 0.0038°/s) to NVM format (10bit, 0x01 == 0.061°/s).
 *        Function makes sure that NVM value is representation of two's
 *        complement signed number.
 *
 * @param val Value in internal offset format (16bit, 0x01 == 0.0038°/s).
 *
 * @return nvm NVM format representation of @p val (10bit, 0x01 == 0.061°/s)
 */
static uint16_t bmi_emul_gyr_off_to_nvm(int16_t off)
{
	uint32_t twos_comp_val;
	uint16_t nvm = 0;

	twos_comp_val = bmi_emul_val_to_twos_comp(off);

	/*
	 * LSB in internal representation has value 0.0038°/s, while in NVM
	 * LSB is 0.061°/s. Skip 0.0038°/s, 0.0076°/s, 0.015°/s, and
	 * 0.03°/s bits.
	 */
	nvm |= (twos_comp_val >> 4) & 0x1ff;
	/* Set sign bit */
	nvm |= (twos_comp_val & BIT(31)) ? BIT(9) : 0x00;

	return nvm;
}

/** Check description in emul_bmi.h */
int16_t bmi_emul_get_off(struct i2c_emul *emul, enum bmi_emul_axis axis)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	switch (axis) {
	case BMI_EMUL_ACC_X:
		return data->off_acc_x;
	case BMI_EMUL_ACC_Y:
		return data->off_acc_y;
	case BMI_EMUL_ACC_Z:
		return data->off_acc_z;
	case BMI_EMUL_GYR_X:
		return data->off_gyr_x;
	case BMI_EMUL_GYR_Y:
		return data->off_gyr_y;
	case BMI_EMUL_GYR_Z:
		return data->off_gyr_z;
	}

	return 0;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_off(struct i2c_emul *emul, enum bmi_emul_axis axis,
		      int16_t val)
{
	struct bmi_emul_data *data;
	uint16_t gyr_off;
	uint8_t gyr98_shift;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	switch (axis) {
	case BMI_EMUL_ACC_X:
		data->off_acc_x = val;
		data->reg[data->type_data->acc_off_reg] =
				bmi_emul_acc_off_to_nvm(data->off_acc_x);
		break;
	case BMI_EMUL_ACC_Y:
		data->off_acc_y = val;
		data->reg[data->type_data->acc_off_reg + 1] =
				bmi_emul_acc_off_to_nvm(data->off_acc_y);
		break;
	case BMI_EMUL_ACC_Z:
		data->off_acc_z = val;
		data->reg[data->type_data->acc_off_reg + 2] =
				bmi_emul_acc_off_to_nvm(data->off_acc_z);
		break;
	case BMI_EMUL_GYR_X:
		data->off_gyr_x = val;
		gyr_off = bmi_emul_gyr_off_to_nvm(data->off_gyr_x);
		data->reg[data->type_data->gyr_off_reg] = gyr_off & 0xff;
		gyr98_shift = 0;
		data->reg[data->type_data->gyr98_off_reg] &=
				~(0x3 << gyr98_shift);
		data->reg[data->type_data->gyr98_off_reg] |=
				(gyr_off & 0x300) >> (8 - gyr98_shift);
		break;
	case BMI_EMUL_GYR_Y:
		data->off_gyr_y = val;
		gyr_off = bmi_emul_gyr_off_to_nvm(data->off_gyr_y);
		data->reg[data->type_data->gyr_off_reg + 1] = gyr_off & 0xff;
		gyr98_shift = 2;
		data->reg[data->type_data->gyr98_off_reg] &=
				~(0x3 << gyr98_shift);
		data->reg[data->type_data->gyr98_off_reg] |=
				(gyr_off & 0x300) >> (8 - gyr98_shift);
		break;
	case BMI_EMUL_GYR_Z:
		data->off_gyr_z = val;
		gyr_off = bmi_emul_gyr_off_to_nvm(data->off_gyr_z);
		data->reg[data->type_data->gyr_off_reg + 2] = gyr_off & 0xff;
		gyr98_shift = 4;
		data->reg[data->type_data->gyr98_off_reg] &=
				~(0x3 << gyr98_shift);
		data->reg[data->type_data->gyr98_off_reg] |=
				(gyr_off & 0x300) >> (8 - gyr98_shift);
		break;
	}
}

/** Check description in emul_bmi.h */
int32_t bmi_emul_get_value(struct i2c_emul *emul, enum bmi_emul_axis axis)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	switch (axis) {
	case BMI_EMUL_ACC_X:
		return data->acc_x;
	case BMI_EMUL_ACC_Y:
		return data->acc_y;
	case BMI_EMUL_ACC_Z:
		return data->acc_z;
	case BMI_EMUL_GYR_X:
		return data->gyr_x;
	case BMI_EMUL_GYR_Y:
		return data->gyr_y;
	case BMI_EMUL_GYR_Z:
		return data->gyr_z;
	}

	return 0;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_value(struct i2c_emul *emul, enum bmi_emul_axis axis,
			int32_t val)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	switch (axis) {
	case BMI_EMUL_ACC_X:
		data->acc_x = val;
		break;
	case BMI_EMUL_ACC_Y:
		data->acc_y = val;
		break;
	case BMI_EMUL_ACC_Z:
		data->acc_z = val;
		break;
	case BMI_EMUL_GYR_X:
		data->gyr_x = val;
		break;
	case BMI_EMUL_GYR_Y:
		data->gyr_y = val;
		break;
	case BMI_EMUL_GYR_Z:
		data->gyr_z = val;
		break;
	}
}

/** Check description in emul_bmi.h */
void bmi_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->error_on_ro_write = set;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->error_on_rsvd_write = set;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_err_on_wo_read(struct i2c_emul *emul, bool set)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->error_on_wo_read = set;
}

/** Check description in emul_bmi.h */
void bmi_emul_simulate_cmd_exec_time(struct i2c_emul *emul, bool set)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	data->simulate_command_exec_time = set;
}

/** Check description in emul_bmi.h */
void bmi_emul_set_skipped_frames(struct i2c_emul *emul, uint8_t skip)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	data->fifo_skip = skip;
}

/**
 * @brief Convert current time to sensor time (39 us units)
 *
 * @return time in 39 us units
 */
static int64_t bmi_emul_get_sensortime(void)
{
	return k_uptime_ticks() * 1000000 / 39 / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

/**
 * @brief Set registers at address @p reg with sensor time that elapsed since
 *        last reset of emulator
 *
 * @param emul Pointer to BMI emulator
 * @param reg Pointer to 3 byte array, where current sensor time should be
 *            stored
 */
static void bmi_emul_set_sensortime_reg(struct i2c_emul *emul, uint8_t *reg)
{
	struct bmi_emul_data *data;
	uint32_t twos_comp_val;
	int64_t time;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	time = bmi_emul_get_sensortime();

	twos_comp_val = bmi_emul_val_to_twos_comp(time - data->zero_time);

	*reg = twos_comp_val & 0xff;
	*(reg + 1) = (twos_comp_val >> 8) & 0xff;
	*(reg + 2) = (twos_comp_val >> 16) & 0xff;
}

/**
 * @brief Convert given sensor axis @p val from internal units to register
 *        units. It shifts value by @p shift bits to the right to account
 *        range set in emulator's registers. Result is saved at address @p reg
 *
 * @param emul Pointer to BMI emulator
 * @param val Accelerometer or gyroscope value in internal units
 * @param reg Pointer to 2 byte array, where sensor value should be stored
 * @param shift How many bits should be shift to the right
 */
static void bmi_emul_set_data_reg(struct i2c_emul *emul, int32_t val,
				  uint8_t *reg, int shift)
{
	struct bmi_emul_data *data;
	uint32_t twos_comp_val;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	twos_comp_val = bmi_emul_val_to_twos_comp(val);

	/* Shift unused bits because of selected range */
	twos_comp_val >>= shift;

	*reg = twos_comp_val & 0xff;
	*(reg + 1) = (twos_comp_val >> 8) & 0xff;
}

/**
 * @brief Compute length of given FIFO @p frame. If frame is null then length
 *        of empty frame is returned.
 *
 * @param emul Pointer to BMI emulator
 * @param frame Pointer to FIFO frame
 * @param tag_time Indicate if sensor time should be included in empty frame
 * @param header Indicate if header should be included in frame
 *
 * @return length of frame
 */
static uint8_t bmi_emul_get_frame_len(struct i2c_emul *emul,
				      struct bmi_emul_frame *frame,
				      bool tag_time, bool header)
{
	struct bmi_emul_data *data;
	int len;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	/* Empty FIFO frame */
	if (frame == NULL) {
		if (tag_time && header) {
			/* Header of sensortime + sensortime + empty FIFO */
			return 5;
		}

		/* Empty fifo */
		return 1;
	}

	/* Config FIFO frame */
	if (frame->type & BMI_EMUL_FRAME_CONFIG) {
		if (header) {
			/* Header + byte of data */
			len = 2;
			if (data->type_data->sensortime_follow_config_frame) {
				/* Sensortime data */
				len += 3;
			}

			return len;
		}

		/* This frame doesn't exist in headerless mode */
		return 0;
	}

	/* Sensor data FIFO frame */
	if (header) {
		len = 1;
	} else {
		len = 0;
	}

	if (frame->type & BMI_EMUL_FRAME_ACC) {
		len += 6;
	}
	if (frame->type & BMI_EMUL_FRAME_MAG) {
		len += 8;
	}
	if (frame->type & BMI_EMUL_FRAME_GYR) {
		len += 6;
	}

	return len;
}

/**
 * @brief Set given FIFO @p frame as current frame in fifo field of emulator
 *        data structure
 *
 * @param emul Pointer to BMI emulator
 * @param frame Pointer to FIFO frame
 * @param tag_time Indicate if sensor time should be included in empty frame
 * @param header Indicate if header should be included in frame
 * @param acc_shift How many bits should be right shifted from accelerometer
 *                  data
 * @param gyr_shift How many bits should be right shifted from gyroscope data
 */
static void bmi_emul_set_current_frame(struct i2c_emul *emul,
				       struct bmi_emul_frame *frame,
				       bool tag_time, bool header,
				       int acc_shift, int gyr_shift)
{
	struct bmi_emul_data *data;
	int i = 0;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	data->fifo_frame_byte = 0;
	data->fifo_frame_len = bmi_emul_get_frame_len(emul, frame, tag_time,
						      header);
	/* Empty FIFO frame */
	if (frame == NULL) {
		if (tag_time && header) {
			/* Header */
			data->fifo[0] = BMI_EMUL_FIFO_HEAD_TIME;
			bmi_emul_set_sensortime_reg(emul, &(data->fifo[1]));
			i = 4;
		}

		/* Empty header */
		data->fifo[i] = BMI_EMUL_FIFO_HEAD_EMPTY;

		return;
	}

	/* Config FIFO frame */
	if (frame->type & BMI_EMUL_FRAME_CONFIG) {
		/* Header */
		data->fifo[0] = BMI_EMUL_FIFO_HEAD_CONFIG;
		data->fifo[1] = frame->config;
		if (data->type_data->sensortime_follow_config_frame) {
			bmi_emul_set_sensortime_reg(emul, &(data->fifo[2]));
		}

		return;
	}

	/* Sensor data FIFO frame */
	if (header) {
		data->fifo[0] = BMI_EMUL_FIFO_HEAD_DATA;
		data->fifo[0] |= frame->type & BMI_EMUL_FRAME_MAG ?
						BMI_EMUL_FIFO_HEAD_DATA_MAG : 0;
		data->fifo[0] |= frame->type & BMI_EMUL_FRAME_GYR ?
						BMI_EMUL_FIFO_HEAD_DATA_GYR : 0;
		data->fifo[0] |= frame->type & BMI_EMUL_FRAME_ACC ?
						BMI_EMUL_FIFO_HEAD_DATA_ACC : 0;
		data->fifo[0] |= frame->tag & BMI_EMUL_FIFO_HEAD_DATA_TAG_MASK;
		i = 1;
	}

	if (frame->type & BMI_EMUL_FRAME_MAG) {
		bmi_emul_set_data_reg(emul, frame->mag_x, &(data->fifo[i]), 0);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->mag_y, &(data->fifo[i]), 0);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->mag_z, &(data->fifo[i]), 0);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->rhall, &(data->fifo[i]), 0);
		i += 2;
	}

	if (frame->type & BMI_EMUL_FRAME_GYR) {
		bmi_emul_set_data_reg(emul, frame->gyr_x, &(data->fifo[i]),
				      gyr_shift);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->gyr_y, &(data->fifo[i]),
				      gyr_shift);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->gyr_z, &(data->fifo[i]),
				      gyr_shift);
		i += 2;
	}

	if (frame->type & BMI_EMUL_FRAME_ACC) {
		bmi_emul_set_data_reg(emul, frame->acc_x, &(data->fifo[i]),
				      acc_shift);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->acc_y, &(data->fifo[i]),
				      acc_shift);
		i += 2;
		bmi_emul_set_data_reg(emul, frame->acc_z, &(data->fifo[i]),
				      acc_shift);
		i += 2;
	}
}

/**
 * @brief Update internal sensors offset values using values from emulated
 *        registers.
 *
 * @param emul Pointer to BMI emulator
 */
static void bmi_emul_updata_int_off(struct i2c_emul *emul)
{
	struct bmi_emul_data *data;
	uint16_t gyr_nvm;
	uint8_t gyr98;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	data->off_acc_x = bmi_emul_acc_nvm_to_off(
				data->reg[data->type_data->acc_off_reg]);
	data->off_acc_y = bmi_emul_acc_nvm_to_off(
				data->reg[data->type_data->acc_off_reg + 1]);
	data->off_acc_z = bmi_emul_acc_nvm_to_off(
				data->reg[data->type_data->acc_off_reg + 2]);

	gyr98 = data->reg[data->type_data->gyr98_off_reg];

	gyr_nvm = data->reg[data->type_data->gyr_off_reg];
	gyr_nvm |= (gyr98 & 0x3) << 8;
	data->off_gyr_x = bmi_emul_gyr_nvm_to_off(gyr_nvm);
	gyr_nvm = data->reg[data->type_data->gyr_off_reg + 1];
	gyr_nvm |= (gyr98 & 0xc) << 6;
	data->off_gyr_y = bmi_emul_gyr_nvm_to_off(gyr_nvm);
	gyr_nvm = data->reg[data->type_data->gyr_off_reg + 2];
	gyr_nvm |= (gyr98 & 0x30) << 4;
	data->off_gyr_z = bmi_emul_gyr_nvm_to_off(gyr_nvm);
}

/**
 * @brief Restore registers backed in NVM to emulator's registers. Each model
 *        of BMI may have different set of NVM backed registers.
 *
 * @param emul Pointer to BMI emulator
 */
static void bmi_emul_restore_nvm(struct i2c_emul *emul)
{
	struct bmi_emul_data *data;
	int i;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	ASSERT(data->type_data->nvm_len <= BMI_EMUL_MAX_NVM_REGS);

	/* Restore registers values */
	for (i = 0; i < data->type_data->nvm_len; i++) {
		data->reg[data->type_data->nvm_reg[i]] = data->nvm[i];
	}

	bmi_emul_updata_int_off(emul);
}

/** Check description in emul_bmi.h */
void bmi_emul_flush_fifo(struct i2c_emul *emul, bool tag_time, bool header)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	data->fifo_skip = 0;
	data->fifo_frame = NULL;
	/*
	 * Gyroscope and accelerometer shift (last two arguments)
	 * are not important for NULL (empty) FIFO frame.
	 */
	bmi_emul_set_current_frame(emul, NULL, tag_time, header, 0, 0);
}

/** Check description in emul_bmi.h */
void bmi_emul_reset_common(struct i2c_emul *emul, bool tag_time, bool header)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	/* Restore registers backed in NVM */
	bmi_emul_restore_nvm(emul);

	/* Flush FIFO */
	bmi_emul_flush_fifo(emul, tag_time, header);

	/* Reset sensor timer */
	data->zero_time = bmi_emul_get_sensortime();
}

/** Check description in emul_bmi.h */
void bmi_emul_set_cmd_end_time(struct i2c_emul *emul, int time)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	data->cmd_end_time = k_uptime_get_32() + time;
}

/** Check description in emul_bmi.h */
bool bmi_emul_is_cmd_end(struct i2c_emul *emul)
{
	struct bmi_emul_data *data;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	/* We are simulating command execution time and it doesn't expired */
	if (data->simulate_command_exec_time &&
	    data->cmd_end_time > k_uptime_get_32()) {
		return false;
	}

	return true;
}

/**
 * @brief Handle I2C write message. Before any handling, custom function
 *        is called if provided. Next BMI model specific write function is
 *        called. It is checked if accessed register isn't RO and reserved bits
 *        are set to 0. Write set value of reg field of bmi emulator data
 *        ignoring reserved bits. If required internal sensor offset values are
 *        updated. Emulator may be configured to fail on write to specific
 *        register.
 *
 * @param emul Pointer to BMI emulator
 * @param reg Register which is written
 * @param byte Number of handled bytes in this write command
 * @param val Value being written to @p reg
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bmi_emul_handle_write(struct i2c_emul *emul, int reg, int byte,
				 uint8_t val)
{
	struct bmi_emul_data *data;
	uint8_t rsvd_mask;
	int ret;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (data->write_func) {
		ret = data->write_func(emul, reg, byte, val,
				       data->write_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	ret = data->type_data->handle_write(data->reg, emul, &reg, byte, val);
	if (ret != 0) {
		if (ret == BMI_EMUL_ACCESS_E) {
			if (!data->error_on_ro_write) {
				return 0;
			}
			LOG_ERR("Writing to reg 0x%x which is RO", reg);
		}

		return -EIO;
	}

	if (data->write_fail_reg == reg ||
	    data->write_fail_reg == BMI_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	rsvd_mask = data->type_data->rsvd_mask[reg];

	if (data->error_on_rsvd_write && rsvd_mask & val) {
		LOG_ERR("Writing 0x%x to reg 0x%x with rsvd bits mask 0x%x",
			val, reg, rsvd_mask);
		return -EIO;
	}

	/* Ignore all reserved bits */
	val &= ~rsvd_mask;
	val |= data->reg[reg] & rsvd_mask;

	data->reg[reg] = val;

	if ((reg >= data->type_data->acc_off_reg &&
	     reg <= data->type_data->acc_off_reg + 2) ||
	    (reg >= data->type_data->gyr_off_reg &&
	     reg <= data->type_data->gyr_off_reg + 2) ||
	    reg == data->type_data->gyr98_off_reg) {
		/*
		 * Internal offset value should be updated to new value of
		 * offset registers
		 */
		bmi_emul_updata_int_off(emul);
	}

	return 0;
}

/** Check description in emul_bmi.h */
void bmi_emul_state_to_reg(struct i2c_emul *emul, int acc_shift,
			   int gyr_shift, int acc_reg, int gyr_reg,
			   int sensortime_reg, bool acc_off_en,
			   bool gyr_off_en)
{
	struct bmi_emul_data *data;
	int32_t val[3];
	int i;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (gyr_off_en) {
		val[0] = data->gyr_x - data->off_gyr_x;
		val[1] = data->gyr_y - data->off_gyr_y;
		val[2] = data->gyr_z - data->off_gyr_z;
	} else {
		val[0] = data->gyr_x;
		val[1] = data->gyr_y;
		val[2] = data->gyr_z;
	}

	for (i = 0; i < 3; i++) {
		bmi_emul_set_data_reg(emul, val[i],
				      &(data->reg[gyr_reg + i * 2]), gyr_shift);
	}

	if (acc_off_en) {
		val[0] = data->acc_x - data->off_acc_x;
		val[1] = data->acc_y - data->off_acc_y;
		val[2] = data->acc_z - data->off_acc_z;
	} else {
		val[0] = data->acc_x;
		val[1] = data->acc_y;
		val[2] = data->acc_z;
	}

	for (i = 0; i < 3; i++) {
		bmi_emul_set_data_reg(emul, val[i],
				      &(data->reg[acc_reg + i * 2]), acc_shift);
	}

	bmi_emul_set_sensortime_reg(emul, &(data->reg[sensortime_reg]));
}

/** Check description in emul_bmi.h */
void bmi_emul_append_frame(struct i2c_emul *emul, struct bmi_emul_frame *frame)
{
	struct bmi_emul_data *data;
	struct bmi_emul_frame *tmp_frame;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (data->fifo_frame == NULL) {
		data->fifo_frame = frame;
	} else {
		tmp_frame = data->fifo_frame;
		while (tmp_frame->next != NULL) {
			tmp_frame = tmp_frame->next;
		}
		tmp_frame->next = frame;
	}
}

/** Check description in emul_bmi.h */
uint16_t bmi_emul_fifo_len(struct i2c_emul *emul, bool tag_time, bool header)
{
	struct bmi_emul_frame *frame;
	struct bmi_emul_data *data;
	uint16_t len = 0;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (data->fifo_skip != 0 && header) {
		len += 2;
	}

	frame = data->fifo_frame;
	while (frame != NULL) {
		len += bmi_emul_get_frame_len(emul, frame, tag_time, header);
		frame = frame->next;
	}

	len += bmi_emul_get_frame_len(emul, NULL, tag_time, header);
	/* Do not count last empty frame byte */
	len--;

	return len;
}

/** Check description in emul_bmi.h */
uint8_t bmi_emul_get_fifo_data(struct i2c_emul *emul, int byte,
			       bool tag_time, bool header, int acc_shift,
			       int gyr_shift)
{
	struct bmi_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (byte == 0) {
		/* Repeat uncompleated read of frame */
		bmi_emul_set_current_frame(emul, data->fifo_frame, tag_time,
					   header, acc_shift, gyr_shift);

		/* Return header for skip frame */
		if (data->fifo_skip != 0 && header) {
			return BMI_EMUL_FIFO_HEAD_SKIP;
		}
	}

	if (data->fifo_skip != 0 && byte == 1 && header) {
		/* Return number of skipped frames */
		ret = data->fifo_skip;
		data->fifo_skip = 0;

		return ret;
	}

	/* Get next valid frame */
	while (data->fifo_frame_byte >= data->fifo_frame_len) {
		/* No data */
		if (data->fifo_frame == NULL) {
			return 0;
		}
		data->fifo_frame = data->fifo_frame->next;
		bmi_emul_set_current_frame(emul, data->fifo_frame, tag_time,
					   header, acc_shift, gyr_shift);
	}

	return data->fifo[data->fifo_frame_byte++];
}

/**
 * @brief Handle I2C read message. Before any handling, custom function
 *        is called if provided. Next BMI model specific read function is
 *        called. It is checked if accessed register isn't WO. Emulator may
 *        be configured to fail on given register read.
 *
 * @param emul Pointer to BMI emulator
 * @param reg Register address to read
 * @param byte Byte which is accessed during block read
 * @param buf Pointer where result should be stored
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bmi_emul_handle_read(struct i2c_emul *emul, int reg, int byte,
				char *buf)
{
	struct bmi_emul_data *data;
	uint16_t fifo_len;
	int ret;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);

	if (data->read_func) {
		ret = data->read_func(emul, reg, byte, data->read_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			/* Immediately return value set by custom function */
			*buf = data->reg[reg];

			return 0;
		}
	}

	ret = data->type_data->handle_read(data->reg, emul, &reg, byte, buf);
	if (ret == BMI_EMUL_ACCESS_E && data->error_on_wo_read) {
		LOG_ERR("Reading reg 0x%x which is WO", reg);
	} else if (ret != 0) {
		return ret;
	}

	if (data->read_fail_reg == reg ||
	    data->read_fail_reg == BMI_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	return 0;
}

/**
 * @biref Emulate an I2C transfer to a BMI accelerometer
 *
 * This handles simple reads and writes
 *
 * @param emul I2C emulation information
 * @param msgs List of messages to process
 * @param num_msgs Number of messages to process
 * @param addr Address of the I2C target device
 *
 * @retval 0 If successful
 * @retval -EIO General input / output error
 */
static int bmi_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
			     int num_msgs, int addr)
{
	const struct bmi_emul_cfg *cfg;
	struct bmi_emul_data *data;
	unsigned int len;
	int ret, i;
	bool read;

	data = CONTAINER_OF(emul, struct bmi_emul_data, emul);
	cfg = data->cfg;

	if (cfg->addr != addr) {
		LOG_ERR("Address mismatch, expected %02x, got %02x", cfg->addr,
			addr);
		return -EIO;
	}

	i2c_dump_msgs("emul", msgs, num_msgs, addr);

	for (; num_msgs > 0; num_msgs--, msgs++) {
		read = msgs->flags & I2C_MSG_READ;

		switch (data->msg_state) {
		case BMI_EMUL_NONE_MSG:
			data->msg_byte = 0;
			break;
		case BMI_EMUL_IN_WRITE:
			if (read) {
				data->msg_byte = 0;
			}
			break;
		case BMI_EMUL_IN_READ:
			if (!read) {
				data->msg_byte = 0;
			}
			break;
		}
		data->msg_state = read ? BMI_EMUL_IN_READ : BMI_EMUL_IN_WRITE;

		if (msgs->flags & I2C_MSG_STOP) {
			data->msg_state = BMI_EMUL_NONE_MSG;
		}

		if (!read) {
			/* Dispatch wrtie command */
			i = 0;
			/*
			 * Save first byte of write command as currently
			 * accessed register.
			 */
			if (data->msg_byte == 0 && msgs->len) {
				data->cur_reg = msgs->buf[0];
				i++;
				data->msg_byte++;
			}

			/* Handle rest of write command bytes */
			for (; i < msgs->len; i++) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bmi_emul_handle_write(emul, data->cur_reg,
							    data->msg_byte,
							    msgs->buf[i]);
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
				data->msg_byte++;
			}
		} else {
			/* Dispatch read command */
			for (i = 0; i < msgs->len; i++) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bmi_emul_handle_read(emul, data->cur_reg,
							   data->msg_byte,
							   &(msgs->buf[i]));
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
				data->msg_byte++;
			}
		}
	}

	return 0;
}

/* Device instantiation */

static struct i2c_emul_api bmi_emul_api = {
	.transfer = bmi_emul_transfer,
};

/**
 * @brief Set up a new BMI emulator
 *
 * This should be called for each BMI device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int bmi_emul_init(const struct emul *emul,
			 const struct device *parent)
{
	const struct bmi_emul_cfg *cfg = emul->cfg;
	struct bmi_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &bmi_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	k_mutex_init(&data->data_mtx);

	switch (data->type) {
	case BMI_EMUL_160:
		data->type_data = get_bmi160_emul_type_data();
		break;
	case BMI_EMUL_260:
		data->type_data = get_bmi260_emul_type_data();
		break;
	}

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	data->type_data->reset(data->reg, &data->emul);

	return ret;
}

#define BMI_EMUL(n)							\
	static struct bmi_emul_data bmi_emul_data_##n = {		\
		.error_on_ro_write = DT_INST_PROP(n, error_on_ro_write),\
		.error_on_wo_read = DT_INST_PROP(n, error_on_wo_read),	\
		.error_on_rsvd_write = DT_INST_PROP(n,			\
					error_on_reserved_bit_write),	\
		.simulate_command_exec_time = DT_INST_PROP(n,		\
					simulate_command_exec_time),	\
		.type = DT_ENUM_TOKEN(DT_DRV_INST(n), device_model),	\
		.msg_state = BMI_EMUL_NONE_MSG,				\
		.cur_reg = 0,						\
		.write_func = NULL,					\
		.read_func = NULL,					\
		.write_fail_reg = BMI_EMUL_NO_FAIL_REG,			\
		.read_fail_reg = BMI_EMUL_NO_FAIL_REG,			\
	};								\
									\
	static const struct bmi_emul_cfg bmi_emul_cfg_##n = {		\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.data = &bmi_emul_data_##n,				\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(bmi_emul_init, DT_DRV_INST(n), &bmi_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(BMI_EMUL)

#define BMI_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &bmi_emul_data_##n.emul;

/** Check description in emul_bmi.h */
struct i2c_emul *bmi_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(BMI_EMUL_CASE)

	default:
		return NULL;
	}
}
