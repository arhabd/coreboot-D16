# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_NPCX  keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SHI_NPCX     shi.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC           system.c)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE
        system_external_storage.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API
        system_download_from_flash.c)
zephyr_library_sources_ifdef(CONFIG_PM_POLICY_APP     power_policy.c)
