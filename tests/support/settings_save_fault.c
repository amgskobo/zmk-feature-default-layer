/* SPDX-License-Identifier: MIT */
#include "settings_save_fault.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>

static bool fail_saves;
static unsigned int failures;

void default_layer_test_fail_saves(bool fail) {
    fail_saves = fail;
    if (fail) {
        failures = 0;
    }
}

unsigned int default_layer_test_save_failures(void) { return failures; }

int __real_settings_save_one(const char *name, const void *value, size_t len);

int __wrap_settings_save_one(const char *name, const void *value, size_t len) {
    if (fail_saves && strstr(name, "cormoran__default_layer")) {
        failures++;
        return -EIO;
    }
    return __real_settings_save_one(name, value, len);
}
