/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_
#define ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_

#include <devicetree.h>

#ifdef CONFIG_PLATFORM_EC_TEMP_SENSOR

#define ZSHIM_TEMP_SENSOR_ID(node_id)      \
	DT_ENUM_UPPER_TOKEN(node_id, enum_name)
#define TEMP_SENSOR_ID_WITH_COMMA(node_id) ZSHIM_TEMP_SENSOR_ID(node_id),

enum temp_sensor_id {
#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors),
			 TEMP_SENSOR_ID_WITH_COMMA)
#endif /* named_temp_sensors */
	TEMP_SENSOR_COUNT
};

#undef TEMP_SENSOR_ID_WITH_COMMA

#endif /* CONFIG_PLATFORM_EC_TEMP_SENSOR */

#endif /* ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_ */
