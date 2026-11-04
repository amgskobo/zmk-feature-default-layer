/* SPDX-License-Identifier: MIT */
#include "settings_save_fault.h"
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk_behavior_default_layer/default_layer.h>

int default_layer_review_rpc_state(const uint32_t *expected, size_t count, uint32_t live_count,
                                   bool pending);

static uint8_t review_endpoint;
static atomic_t review_context_bad, review_key_bad;
static bool review_monitor, review_capture_key, review_pending_capture;
static unsigned review_pressed, review_released;
static uint32_t review_keycode;
static int review_pending_result;
static bool review_gate_key, review_setter;
static int review_writer_result;
static unsigned review_churn_left;
static bool review_probe_scheduled, review_probe_progress;
static void review_fairness_probe_handler(struct k_work *work);
static K_WORK_DEFINE(review_fairness_probe, review_fairness_probe_handler);
K_SEM_DEFINE(review_key_entered, 0, 1);
K_SEM_DEFINE(review_key_release, 0, 1);
K_SEM_DEFINE(review_writer_start, 0, 1);
K_SEM_DEFINE(review_writer_done, 0, 1);
K_SEM_DEFINE(review_requested, 0, 1);
K_SEM_DEFINE(review_probe_seen, 0, 1);

#define REVIEW_CHECK(test)                                                                         \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            LOG_ERR("FAIL: reviewed_default_layer line=%d", __LINE__);                             \
            return -EINVAL;                                                                        \
        }                                                                                          \
    } while (0)

static void review_application_requested(void) {
    if (review_monitor && k_current_get() != &k_sys_work_q.thread)
        k_sem_give(&review_requested);
}

static int review_write(const char *key, uint32_t index, int32_t value) {
    struct zmk_custom_setting_value setting = ZMK_CUSTOM_SETTING_VALUE_INT32(value);
    return zmk_custom_setting_write_array_by_key(DEFAULT_LAYER_SUBSYSTEM_ID, key, index, &setting,
                                                 ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
}
static int review_set(int32_t value) {
    int result = review_write("endpoint_layer", review_endpoint, value);
    if (result == 0)
        zmk_default_layer_resolve_and_apply("review-sync");
    return result;
}

static int review_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);
    if (review_monitor && layer && k_current_get() != &k_sys_work_q.thread)
        atomic_inc(&review_context_bad);
    if (layer && layer->state && (layer->layer == 1 || layer->layer == 3) && review_churn_left) {
        --review_churn_left;
        if (!review_probe_scheduled) {
            review_probe_scheduled = true;
            k_work_submit(&review_fairness_probe);
        }
        int32_t next = zmk_default_layer_get_endpoint(review_endpoint) == 1 ? 3 : 1;
        if (review_write("endpoint_layer", review_endpoint, next) != 0)
            atomic_inc(&review_context_bad);
    }
    if (layer && layer->layer != 5 && !layer->state && review_pending_capture) {
        review_pending_capture = false;
        const uint32_t available[] = {1, 2, 3};
        /* Real reentrant GetState cannot claim this application has finished. */
        review_pending_result = default_layer_review_rpc_state(available, 3, 6, true);
    }
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(eh);
    if (review_capture_key && key) {
        if (key->state) {
            ++review_pressed;
            review_keycode = key->keycode;
        } else
            ++review_released;
        if (key->usage_page != HID_USAGE_KEY)
            atomic_inc(&review_key_bad);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(default_layer_review, review_listener);
ZMK_SUBSCRIPTION(default_layer_review, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(default_layer_review, zmk_keycode_state_changed);

static void review_key_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (review_gate_key) {
        k_sem_give(&review_key_entered);
        if (k_sem_take(&review_key_release, K_SECONDS(1)) != 0)
            atomic_inc(&review_key_bad);
        /* This is the same event path used by physical scan work, with a real
         * &mo press/release and core state/locking behavior. */
        if (raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .position = 3,
                .state = true,
                .timestamp = k_uptime_get()}) < 0 ||
            !zmk_keymap_layer_active(5))
            atomic_inc(&review_key_bad);
        if (raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .position = 3,
                .state = false,
                .timestamp = k_uptime_get()}) < 0 ||
            zmk_keymap_layer_active(5))
            atomic_inc(&review_key_bad);
    } else {
        review_capture_key = true;
        if (raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .position = 2,
                .state = true,
                .timestamp = k_uptime_get()}) < 0)
            atomic_inc(&review_key_bad);
        if (raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .position = 2,
                .state = false,
                .timestamp = k_uptime_get()}) < 0)
            atomic_inc(&review_key_bad);
        review_capture_key = false;
    }
}
static K_WORK_DEFINE(review_key_work, review_key_work_handler);

static void review_fairness_probe_handler(struct k_work *work) {
    ARG_UNUSED(work);
    review_probe_progress = review_churn_left > 0;
    review_key_work_handler(&review_key_work);
    k_sem_give(&review_probe_seen);
}

static int review_increment_result;
static void review_increment_handler(struct k_work *work) {
    ARG_UNUSED(work);
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(df)),
        .param1 = DEFAULT_LAYER_CMD_NEXT,
    };
    struct zmk_behavior_binding_event event = {.position = 1, .timestamp = k_uptime_get()};
    review_increment_result = zmk_behavior_invoke_binding(&binding, event, true);
}
static K_WORK_DEFINE(review_increment_work, review_increment_handler);
static int review_increment(void) {
    default_layer_test_fail_saves(true);
    k_work_submit(&review_increment_work);
    struct k_work_sync sync;
    k_work_flush(&review_increment_work, &sync);
    default_layer_test_fail_saves(false);
    return review_increment_result;
}

static int review_dispatch(uint32_t expected) {
    review_gate_key = false;
    review_pressed = review_released = 0;
    k_work_submit(&review_key_work);
    struct k_work_sync sync;
    k_work_flush(&review_key_work, &sync);
    REVIEW_CHECK(review_pressed == 1 && review_released == 1 && review_keycode == expected);
    REVIEW_CHECK(atomic_get(&review_key_bad) == 0);
    return 0;
}

static int review_structural_change(void) {
    int result = raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = ZMK_RPC_NOTIFICATION(keymap, unsaved_changes_status_changed, true)});
    REVIEW_CHECK(result == 0);
    struct k_work_sync sync;
    k_work_flush(&default_layer_apply_work, &sync);
    REVIEW_CHECK(!zmk_default_layer_application_pending());
    return 0;
}

static int review_global_floor(void) {
    REVIEW_CHECK(review_set(1) == 0);
    REVIEW_CHECK(zmk_keymap_move_layer(0, 2) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    REVIEW_CHECK(zmk_default_layer_get_endpoint(review_endpoint) == 1);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 2);
    REVIEW_CHECK(zmk_keymap_highest_layer_active() == 2);
    REVIEW_CHECK(!zmk_keymap_layer_locked(1));
    REVIEW_CHECK(!zmk_default_layer_index_available(1));
    REVIEW_CHECK(zmk_default_layer_set_endpoint(review_endpoint, 1, "review") == -EINVAL);
    REVIEW_CHECK(zmk_default_layer_set_os(0, 1, "review") == -EINVAL);
    REVIEW_CHECK(review_set(1) == 0);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 2);
    REVIEW_CHECK(review_dispatch(0x04) == 0); /* Global stable ID0: A. */
    const uint32_t permitted[] = {2, 3};
    REVIEW_CHECK(default_layer_review_rpc_state(permitted, 2, 6, false) == 0);
    REVIEW_CHECK(review_write("os_layer", 0, 1) == 0);
    REVIEW_CHECK(review_set(ZMK_DEFAULT_LAYER_OS_DETECTION) == 0);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 2);
    REVIEW_CHECK(review_set(3) == 0 && review_dispatch(0x07) == 0); /* ID3: D. */
    REVIEW_CHECK(review_increment() == -EIO);
    REVIEW_CHECK(zmk_default_layer_get_endpoint(review_endpoint) == 2);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 2);
    REVIEW_CHECK(zmk_keymap_move_layer(2, 4) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 4);
    REVIEW_CHECK(default_layer_review_rpc_state(NULL, 0, 6, false) == 0);
    REVIEW_CHECK(review_increment() == -ENODEV);
    REVIEW_CHECK(zmk_keymap_move_layer(4, 0) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    REVIEW_CHECK(review_set(ZMK_DEFAULT_LAYER_UNSET) == 0);
    LOG_INF("PASS: reviewed_default_layer_global_floor");
    return 0;
}

static int review_order_and_removal(void) {
    REVIEW_CHECK(review_set(1) == 0);
    REVIEW_CHECK(zmk_keymap_move_layer(2, 1) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    REVIEW_CHECK(zmk_default_layer_get_endpoint(review_endpoint) == 1);
    REVIEW_CHECK(zmk_keymap_layer_active(2) && !zmk_keymap_layer_active(1));
    REVIEW_CHECK(review_dispatch(0x06) == 0); /* Index1 now stable ID2: C. */
    REVIEW_CHECK(zmk_keymap_move_layer(1, 2) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    REVIEW_CHECK(review_set(3) == 0);
    for (unsigned i = 0; i < 3; ++i) {
        const zmk_keymap_layer_id_t removed = zmk_keymap_layer_index_to_id(3);
        REVIEW_CHECK(zmk_keymap_remove_layer(3) == 0);
        REVIEW_CHECK(review_structural_change() == 0);
        REVIEW_CHECK(!zmk_keymap_layer_active(removed));
    }
    REVIEW_CHECK(zmk_default_layer_get_endpoint(review_endpoint) == 3);
    REVIEW_CHECK(zmk_default_layer_resolve_current() == 0);
    REVIEW_CHECK(zmk_keymap_highest_layer_active() == 0);
    const uint32_t remaining[] = {1, 2};
    REVIEW_CHECK(default_layer_review_rpc_state(remaining, 2, 3, false) == 0);
    REVIEW_CHECK(zmk_default_layer_set_endpoint(review_endpoint, 3, "review") == -EINVAL);
    /* A hole inside the compiled range is authoritative; live later indices
     * cannot be inferred from layer_count alone. */
    REVIEW_CHECK(zmk_keymap_move_layer(3, 1) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    const uint32_t hole[] = {2, 3};
    REVIEW_CHECK(default_layer_review_rpc_state(hole, 2, 3, false) == 0);
    REVIEW_CHECK(review_dispatch(0x06) == 0); /* Live index3 beyond live count-1. */
    REVIEW_CHECK(zmk_keymap_move_layer(1, 3) == 0);
    REVIEW_CHECK(review_structural_change() == 0);
    for (unsigned id = 3; id < 6; ++id) {
        REVIEW_CHECK(zmk_keymap_restore_layer(id, id) == 0);
        REVIEW_CHECK(review_structural_change() == 0);
    }
    REVIEW_CHECK(review_set(ZMK_DEFAULT_LAYER_UNSET) == 0);
    LOG_INF("PASS: reviewed_default_layer_order_and_removal");
    return 0;
}

static void review_writer(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&review_writer_start, K_FOREVER);
        review_writer_result =
            review_setter ? zmk_default_layer_set_endpoint(review_endpoint, 3, "review-rpc")
                          : review_write("endpoint_layer", review_endpoint, 3);
        k_sem_give(&review_writer_done);
    }
}
K_THREAD_DEFINE(default_layer_review_writer, 4096, review_writer, NULL, NULL, NULL, 9, 0, 0);

static int review_key_queue(void) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        REVIEW_CHECK(review_set(1) == 0);
        review_setter = mode != 0;
        review_gate_key = true;
        review_monitor = true;
        review_pending_capture = true;
        review_pending_result = -EIO;
        k_sem_reset(&review_requested);
        k_work_submit(&review_key_work);
        if (k_sem_take(&review_key_entered, K_MSEC(100)) != 0) {
            k_sem_give(&review_key_release);
            return -EINVAL;
        }
        default_layer_test_fail_saves(review_setter);
        k_sem_give(&review_writer_start);
        bool queued = k_sem_take(&review_requested, K_MSEC(100)) == 0;
        /* While actual key work owns the queue, a foreign generic/direct
         * request may update storage but cannot apply any layer state. */
        bool deferred =
            zmk_default_layer_application_pending() && zmk_keymap_highest_layer_active() == 1;
        const uint32_t available[] = {1, 2, 3};
        bool queued_state = default_layer_review_rpc_state(available, 3, 6, true) == 0;
        k_sem_give(&review_key_release); /* Always release, also for controls. */
        struct k_work_sync sync;
        k_work_flush(&review_key_work, &sync);
        bool writer_done = k_sem_take(&review_writer_done, K_SECONDS(1)) == 0;
        default_layer_test_fail_saves(false);
        k_work_flush(&default_layer_apply_work, &sync);
        review_monitor = false;
        review_gate_key = false;
        REVIEW_CHECK(queued && deferred && queued_state && writer_done);
        REVIEW_CHECK(review_writer_result == (review_setter ? -EIO : 0));
        REVIEW_CHECK(zmk_keymap_highest_layer_active() == 3);
        REVIEW_CHECK(!zmk_keymap_layer_active(5) && !zmk_keymap_layer_locked(1));
        REVIEW_CHECK(review_pending_result == 0);
        REVIEW_CHECK(atomic_get(&review_context_bad) == 0 && atomic_get(&review_key_bad) == 0);
    }
    REVIEW_CHECK(review_set(ZMK_DEFAULT_LAYER_UNSET) == 0);
    LOG_INF("PASS: reviewed_default_layer_key_queue");
    return 0;
}

static int review_bounded_updates(void) {
    REVIEW_CHECK(review_set(1) == 0);
    review_gate_key = false;
    review_pressed = review_released = 0;
    review_probe_progress = review_probe_scheduled = false;
    review_churn_left = 20;
    REVIEW_CHECK(review_write("endpoint_layer", review_endpoint, 3) == 0);
    REVIEW_CHECK(k_sem_take(&review_probe_seen, K_SECONDS(1)) == 0);
    /* Finite replenishment retains a qualified assertion for an unbounded
     * worker: the queued real key still runs, but only after churn is over. */
    for (unsigned i = 0; i < 32 && zmk_default_layer_application_pending(); ++i) {
        struct k_work_sync sync;
        k_work_flush(&default_layer_apply_work, &sync);
    }
    REVIEW_CHECK(review_probe_progress && review_churn_left == 0);
    REVIEW_CHECK(review_pressed == 1 && review_released == 1);
    REVIEW_CHECK(!zmk_default_layer_application_pending());
    REVIEW_CHECK(atomic_get(&review_context_bad) == 0 && atomic_get(&review_key_bad) == 0);
    REVIEW_CHECK(review_set(ZMK_DEFAULT_LAYER_UNSET) == 0);
    LOG_INF("PASS: reviewed_default_layer_bounded_updates");
    return 0;
}

static void owner_checked_default_layer_review_test(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    review_endpoint = zmk_endpoint_instance_to_index(zmk_endpoint_get_selected());
    const int32_t before = zmk_default_layer_get_endpoint(review_endpoint);
    const int32_t os_before = zmk_default_layer_get_os(0);
    int result = review_global_floor();
    if (result == 0)
        result = review_order_and_removal();
    if (result == 0)
        result = review_key_queue();
    if (result == 0)
        result = review_bounded_updates();
    if (result == 0)
        result = review_write("os_layer", 0, os_before);
    if (result == 0)
        result = review_set(before);
    printk("OWNER_TEST_RESULT default_layer_review_test=%d\n", result);
}
K_THREAD_DEFINE(default_layer_review_driver, 8192, owner_checked_default_layer_review_test, NULL,
                NULL, NULL, 10, 0, 500);
#undef REVIEW_CHECK
