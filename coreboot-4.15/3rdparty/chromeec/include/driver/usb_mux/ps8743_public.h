/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H
#define __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H

#include <inttypes.h>

#define PS8743_I2C_ADDR0_FLAG    0x10
#define PS8743_I2C_ADDR1_FLAG    0x11
#define PS8743_I2C_ADDR2_FLAG    0x19
#define PS8743_I2C_ADDR3_FLAG    0x1a

/* Mode register for setting mux */
#define PS8743_REG_MODE 0x00
#define PS8743_MODE_IN_HPD_ASSERT    BIT(0)
#define PS8743_MODE_IN_HPD_CONTROL   BIT(1)
#define PS8743_MODE_FLIP_ENABLE      BIT(2)
#define PS8743_MODE_FLIP_REG_CONTROL BIT(3)
#define PS8743_MODE_USB_ENABLE       BIT(4)
#define PS8743_MODE_USB_REG_CONTROL  BIT(5)
#define PS8743_MODE_DP_ENABLE        BIT(6)
#define PS8743_MODE_DP_REG_CONTROL   BIT(7)
/* To reset the state machine to default */
#define PS8743_MODE_POWER_DOWN (PS8743_MODE_USB_REG_CONTROL |  \
				PS8743_MODE_DP_REG_CONTROL)

/* USB equalization settings for Host to Mux */
#define PS8743_REG_USB_EQ_TX     0x32
#define PS8743_USB_EQ_TX_12_8_DB 0x00
#define PS8743_USB_EQ_TX_17_DB   0x20
#define PS8743_USB_EQ_TX_7_7_DB  0x40
#define PS8743_USB_EQ_TX_3_6_DB  0x60
#define PS8743_USB_EQ_TX_15_DB   0x80
#define PS8743_USB_EQ_TX_10_9_DB 0xc0
#define PS8743_USB_EQ_TX_4_5_DB  0xe0

/* USB equalization settings for Connector to Mux */
#define PS8743_REG_USB_EQ_RX     0x3b
#define PS8743_USB_EQ_RX_2_4_DB  0x00
#define PS8743_USB_EQ_RX_5_DB    0x10
#define PS8743_USB_EQ_RX_6_5_DB  0x20
#define PS8743_USB_EQ_RX_7_4_DB  0x30
#define PS8743_USB_EQ_RX_8_7_DB  0x40
#define PS8743_USB_EQ_RX_10_9_DB 0x50
#define PS8743_USB_EQ_RX_12_8_DB 0x60
#define PS8743_USB_EQ_RX_13_8_DB 0x70
#define PS8743_USB_EQ_RX_14_8_DB 0x80
#define PS8743_USB_EQ_RX_15_4_DB 0x90
#define PS8743_USB_EQ_RX_16_0_DB 0xa0
#define PS8743_USB_EQ_RX_16_7_DB 0xb0
#define PS8743_USB_EQ_RX_18_8_DB 0xc0
#define PS8743_USB_EQ_RX_21_3_DB 0xd0
#define PS8743_USB_EQ_RX_22_2_DB 0xe0

/* USB High Speed Signal Detector thershold adjustment */
#define PS8743_REG_HS_DET_THRESHOLD  0x3c
#define PS8743_USB_HS_THRESH_DEFAULT 0x00
#define PS8743_USB_HS_THRESH_POS_10  0x20
#define PS8743_USB_HS_THRESH_POS_33  0x40
#define PS8743_USB_HS_THRESH_NEG_10  0x60
#define PS8743_USB_HS_THRESH_NEG_25  0x80
#define PS8743_USB_HS_THRESH_POS_25  0xa0
#define PS8743_USB_HS_THRESH_NEG_45  0xc0
#define PS8743_USB_HS_THRESH_NEG_35  0xe0

int ps8743_tune_usb_eq(const struct usb_mux *me, uint8_t tx, uint8_t rx);
int ps8743_write(const struct usb_mux *me, uint8_t reg, uint8_t val);
int ps8743_read(const struct usb_mux *me, uint8_t reg, int *val);
int ps8743_check_chip_id(const struct usb_mux *me, int *val);

#endif /* __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H */
