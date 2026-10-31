/* SPDX-License-Identifier: MIT */
#include "checked_init.h"
#include "settings_save_fault.h"
#include <zmk/events/layer_state_changed.h>

/* Runs against six real ZMK layers with only indices 1..3 configurable. */
static uint8_t application_test_endpoint;
static bool application_test_armed;
static bool application_test_injected;
static int application_test_inner_rc;
static bool application_test_other_thread;
static int application_test_handoff_rc;
K_SEM_DEFINE(application_test_request, 0, 1);
K_SEM_DEFINE(application_test_done, 0, 1);

static int application_test_write(int32_t value) {
    struct zmk_custom_setting_value setting = ZMK_CUSTOM_SETTING_VALUE_INT32(value);
    return zmk_custom_setting_write_array_by_key(DEFAULT_LAYER_SUBSYSTEM_ID, "endpoint_layer",
                                                 application_test_endpoint, &setting,
                                                 ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
}

static int application_test_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (application_test_armed && ev && !ev->state &&
        ev->layer == zmk_keymap_layer_index_to_id(2)) {
        application_test_armed = false;
        application_test_injected = true;
        if (application_test_other_thread) {
            k_sem_give(&application_test_request);
            application_test_handoff_rc = k_sem_take(&application_test_done, K_SECONDS(1));
        } else {
            application_test_inner_rc = application_test_write(3);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(default_layer_application_test, application_test_listener);
ZMK_SUBSCRIPTION(default_layer_application_test, zmk_layer_state_changed);

#define APPLICATION_CHECK(test)                                                                    \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            LOG_ERR("FAIL: default_layer_application line=%d", __LINE__);                          \
            return -EINVAL;                                                                        \
        }                                                                                          \
    } while (0)

static int default_layer_application_test_init(void) {
    application_test_endpoint = zmk_endpoint_instance_to_index(zmk_endpoint_get_selected());
    const int32_t before = zmk_default_layer_get_endpoint(application_test_endpoint);
    const int32_t os_before = zmk_default_layer_get_os(0);
    const int32_t invalid[] = {0, 4, 5, ZMK_KEYMAP_LAYERS_LEN, 256, -3};

    for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
        APPLICATION_CHECK(zmk_default_layer_set_endpoint(application_test_endpoint, invalid[i],
                                                         "test") == -EINVAL);
        APPLICATION_CHECK(zmk_default_layer_set_os(0, invalid[i], "test") == -EINVAL);
        APPLICATION_CHECK(zmk_default_layer_get_endpoint(application_test_endpoint) == before);
        APPLICATION_CHECK(zmk_default_layer_get_os(0) == os_before);
        /* Generic storage/import bypasses the setters, so resolution must also
         * reject an out-of-range index without activating or locking it. */
        APPLICATION_CHECK(application_test_write(invalid[i]) == 0);
        APPLICATION_CHECK(zmk_default_layer_resolve_current() == 0);
        APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 0);
        APPLICATION_CHECK(application_test_write(before) == 0);
    }
    LOG_INF("PASS: default_layer_configured_range");

    APPLICATION_CHECK(application_test_write(3) == 0);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 3);
    APPLICATION_CHECK(application_test_write(1) == 0);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 1);
    APPLICATION_CHECK(!zmk_keymap_layer_locked(zmk_keymap_layer_index_to_id(3)));
    /* A function layer outside the configured range belongs to its caller. */
    APPLICATION_CHECK(zmk_keymap_layer_activate(zmk_keymap_layer_index_to_id(5), true) == 0);
    APPLICATION_CHECK(application_test_write(2) == 0);
    APPLICATION_CHECK(application_test_write(1) == 0);
    APPLICATION_CHECK(zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(5)));
    APPLICATION_CHECK(zmk_keymap_layer_deactivate(zmk_keymap_layer_index_to_id(5), true) == 0);
    LOG_INF("PASS: default_layer_retains_unrelated_layer");

    APPLICATION_CHECK(application_test_write(2) == 0);
    application_test_injected = false;
    application_test_inner_rc = -EIO;
    application_test_armed = true;
    APPLICATION_CHECK(application_test_write(1) == 0);
    application_test_armed = false;
    APPLICATION_CHECK(application_test_injected && application_test_inner_rc == 0);
    APPLICATION_CHECK(zmk_default_layer_get_endpoint(application_test_endpoint) == 3);
    APPLICATION_CHECK(zmk_default_layer_resolve_current() == 3);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 3);
    APPLICATION_CHECK(!zmk_keymap_layer_locked(zmk_keymap_layer_index_to_id(1)));
    LOG_INF("PASS: default_layer_latest_reentrant_update");

    APPLICATION_CHECK(application_test_write(2) == 0);
    const zmk_keymap_layer_id_t previous = zmk_keymap_layer_index_to_id(2);
    APPLICATION_CHECK(zmk_keymap_move_layer(2, 5) == 0);
    APPLICATION_CHECK(application_test_write(1) == 0);
    APPLICATION_CHECK(!zmk_keymap_layer_active(previous));
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 1);
    APPLICATION_CHECK(zmk_keymap_move_layer(5, 2) == 0);
    /* A fallback is the stable global layer, even when its index has moved. */
    APPLICATION_CHECK(application_test_write(2) == 0);
    APPLICATION_CHECK(zmk_keymap_move_layer(0, 1) == 0);
    APPLICATION_CHECK(application_test_write(ZMK_DEFAULT_LAYER_UNSET) == 0);
    APPLICATION_CHECK(zmk_default_layer_resolve_current() == 1);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 1);
    APPLICATION_CHECK(!zmk_keymap_layer_active(previous));
    APPLICATION_CHECK(zmk_keymap_move_layer(1, 0) == 0);
    APPLICATION_CHECK(application_test_write(before) == 0);
    LOG_INF("PASS: default_layer_reordered_layers");

    APPLICATION_CHECK(application_test_write(1) == 0);
    default_layer_test_fail_saves(true);
    int save_rc = zmk_default_layer_set_endpoint(application_test_endpoint, 3, "test");
    default_layer_test_fail_saves(false);
    APPLICATION_CHECK(save_rc == -EIO && default_layer_test_save_failures() > 0);
    APPLICATION_CHECK(zmk_default_layer_get_endpoint(application_test_endpoint) == 3);
    APPLICATION_CHECK(zmk_default_layer_resolve_current() == 3);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 3);

    struct zmk_custom_setting_value os = ZMK_CUSTOM_SETTING_VALUE_INT32(1);
    APPLICATION_CHECK(
        zmk_custom_setting_write_array_by_key(DEFAULT_LAYER_SUBSYSTEM_ID, "os_layer", 0, &os,
                                              ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY) == 0);
    APPLICATION_CHECK(application_test_write(ZMK_DEFAULT_LAYER_OS_DETECTION) == 0);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 1);
    default_layer_test_fail_saves(true);
    save_rc = zmk_default_layer_set_os(0, 3, "test");
    default_layer_test_fail_saves(false);
    APPLICATION_CHECK(save_rc == -EIO && default_layer_test_save_failures() > 0);
    APPLICATION_CHECK(zmk_default_layer_get_os(0) == 3);
    APPLICATION_CHECK(zmk_default_layer_resolve_current() == 3);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 3);
    os = ZMK_CUSTOM_SETTING_VALUE_INT32(os_before);
    APPLICATION_CHECK(
        zmk_custom_setting_write_array_by_key(DEFAULT_LAYER_SUBSYSTEM_ID, "os_layer", 0, &os,
                                              ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY) == 0);
    APPLICATION_CHECK(application_test_write(before) == 0);
    LOG_INF("PASS: default_layer_failed_save_runtime");
    return 0;
}

OWNER_CHECKED_SYS_INIT(default_layer_application_test_init, APPLICATION, 99);

/* Exercise a distinct thread after initialization. The layer listener hands off
 * while the old application is in flight; the worker's registry event requests
 * the newer mapping before the old caller resumes. */
static void application_test_writer(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_take(&application_test_request, K_FOREVER);
    application_test_inner_rc = application_test_write(3);
    k_sem_give(&application_test_done);
}
K_THREAD_DEFINE(application_test_writer_thread, 4096, application_test_writer, NULL, NULL, NULL, 9,
                0, 0);

static int default_layer_concurrency_test_run(void) {
    const int32_t before = zmk_default_layer_get_endpoint(application_test_endpoint);
    APPLICATION_CHECK(application_test_write(2) == 0);
    application_test_injected = false;
    application_test_inner_rc = -EIO;
    application_test_handoff_rc = -EIO;
    application_test_other_thread = true;
    application_test_armed = true;
    APPLICATION_CHECK(application_test_write(1) == 0);
    application_test_armed = false;
    application_test_other_thread = false;
    APPLICATION_CHECK(application_test_injected && application_test_handoff_rc == 0);
    APPLICATION_CHECK(application_test_inner_rc == 0);
    APPLICATION_CHECK(zmk_default_layer_get_endpoint(application_test_endpoint) == 3);
    APPLICATION_CHECK(zmk_default_layer_resolve_current() == 3);
    APPLICATION_CHECK(zmk_keymap_highest_layer_active() == 3);
    APPLICATION_CHECK(!zmk_keymap_layer_locked(zmk_keymap_layer_index_to_id(1)));
    APPLICATION_CHECK(application_test_write(before) == 0);
    LOG_INF("PASS: default_layer_concurrent_update");
    return 0;
}

static void owner_checked_default_layer_concurrency_test(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    int result = default_layer_concurrency_test_run();
    printk("OWNER_TEST_RESULT default_layer_concurrency_test=%d\n", result);
}
K_THREAD_DEFINE(application_test_driver_thread, 4096, owner_checked_default_layer_concurrency_test,
                NULL, NULL, NULL, 10, 0, 500);
#undef APPLICATION_CHECK
