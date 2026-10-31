/* SPDX-License-Identifier: MIT */
#pragma once

#include <zephyr/init.h>
#include <zephyr/sys/printk.h>

/* A nonzero SYS_INIT result does not stop normal ZMK key events. Retain it as
 * an explicit completion checked independently of the key-event snapshot. */
#define OWNER_CHECKED_SYS_INIT(function, level, priority)                                          \
    static int owner_checked_##function(void) {                                                    \
        int result = function();                                                                   \
        printk("OWNER_TEST_RESULT " #function "=%d\n", result);                                    \
        return result;                                                                             \
    }                                                                                              \
    SYS_INIT(owner_checked_##function, level, priority)
