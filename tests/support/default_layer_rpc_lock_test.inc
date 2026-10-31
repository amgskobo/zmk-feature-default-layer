#include "checked_init.h"
#include "settings_save_fault.h"
/*
 * Included at the end of src/studio/default_layer_handler.c when
 * CONFIG_ZMK_DEFAULT_LAYER_TEST is set, so it drives the real, static RPC entry
 * point and reads its response buffer. Studio starts locked: a change must be
 * refused with "Unlock required" and leave the layer unchanged, while GetState
 * still answers. Once unlocked the same change applies; the original value is
 * then written back and Studio is locked again. A value written past the
 * setters must still resolve to a layer index.
 */

#include <string.h>

#include <cormoran/zmk/custom_settings.h>
#include <zephyr/init.h>

static int default_layer_lock_test_call(const cormoran_default_layer_Request *request) {
    zmk_custom_CallRequest call = zmk_custom_CallRequest_init_zero;
    pb_ostream_t stream = pb_ostream_from_buffer(call.payload.bytes, sizeof(call.payload.bytes));
    if (!pb_encode(&stream, cormoran_default_layer_Request_fields, request)) {
        LOG_ERR("default layer lock test: encode failed: %s", PB_GET_ERROR(&stream));
        return -EIO;
    }
    call.payload.size = stream.bytes_written;
    pb_callback_t encode_response = {0};
    return default_layer_rpc_handle_request(&call, &encode_response) ? 0 : -EIO;
}

static const cormoran_default_layer_Response *default_layer_lock_test_response(void) {
    return &zmk_rpc_custom_subsystem_response_buffer_cormoran__default_layer;
}

static bool default_layer_lock_test_refused(void) {
    const cormoran_default_layer_Response *resp = default_layer_lock_test_response();
    return resp->which_response_type == cormoran_default_layer_Response_error_tag &&
           strcmp(resp->response_type.error.message, "Unlock required") == 0;
}

static int default_layer_lock_test_set_os(uint32_t os, int32_t value) {
    cormoran_default_layer_Request request = cormoran_default_layer_Request_init_zero;
    request.which_request_type = cormoran_default_layer_Request_set_os_layer_tag;
    request.request_type.set_os_layer.os = os;
    request.request_type.set_os_layer.value = value;
    return default_layer_lock_test_call(&request);
}

static int default_layer_lock_test_run(void) {
    const int32_t before = zmk_default_layer_get_os(0);
    const int32_t target = before == 0 ? ZMK_DEFAULT_LAYER_UNSET : 0;
    if (zmk_studio_core_get_lock_state() != ZMK_STUDIO_CORE_LOCK_STATE_LOCKED) {
        LOG_ERR("default layer lock test: Studio does not start locked");
        return -EINVAL;
    }

    /* Locked: an OS and an endpoint change are refused, nothing changes. */
    if (default_layer_lock_test_set_os(0, target) < 0 || !default_layer_lock_test_refused() ||
        zmk_default_layer_get_os(0) != before) {
        LOG_ERR("default layer lock test: locked OS change was not refused");
        return -EACCES;
    }
    const uint32_t endpoint = ZMK_ENDPOINT_NONE_COUNT;
    const int32_t endpoint_before = zmk_default_layer_get_endpoint(endpoint);
    cormoran_default_layer_Request set_endpoint = cormoran_default_layer_Request_init_zero;
    set_endpoint.which_request_type = cormoran_default_layer_Request_set_endpoint_layer_tag;
    set_endpoint.request_type.set_endpoint_layer.endpoint_index = endpoint;
    set_endpoint.request_type.set_endpoint_layer.value =
        endpoint_before == 0 ? ZMK_DEFAULT_LAYER_UNSET : 0;
    if (default_layer_lock_test_call(&set_endpoint) < 0 || !default_layer_lock_test_refused() ||
        zmk_default_layer_get_endpoint(endpoint) != endpoint_before) {
        LOG_ERR("default layer lock test: locked endpoint change was not refused");
        return -EACCES;
    }

    /* Locked: GetState still answers. */
    cormoran_default_layer_Request get = cormoran_default_layer_Request_init_zero;
    get.which_request_type = cormoran_default_layer_Request_get_state_tag;
    if (default_layer_lock_test_call(&get) < 0 ||
        default_layer_lock_test_response()->which_response_type !=
            cormoran_default_layer_Response_state_tag) {
        LOG_ERR("default layer lock test: locked GetState was refused");
        return -EINVAL;
    }
    const cormoran_default_layer_StateResponse *state =
        &default_layer_lock_test_response()->response_type.state;
    if (!state->has_configurable_range ||
        state->configurable_range.min_index != CONFIG_ZMK_DEFAULT_LAYER_MIN_INDEX ||
        state->configurable_range.max_index != CONFIG_ZMK_DEFAULT_LAYER_MAX_INDEX) {
        return -EINVAL;
    }

    /* Unlocked: the same change applies, then the original is written back. */
    zmk_studio_core_unlock();
    int ret = default_layer_lock_test_set_os(0, target);
    bool applied =
        ret == 0 && !default_layer_lock_test_refused() && zmk_default_layer_get_os(0) == target;
    ret = default_layer_lock_test_set_os(0, before);
    bool restored = ret == 0 && zmk_default_layer_get_os(0) == before;
    zmk_studio_core_lock();
    if (!applied || !restored) {
        LOG_ERR("default layer lock test: unlocked change applied=%d restored=%d", applied,
                restored);
        return -EINVAL;
    }

    LOG_INF("PASS: default_layer_rpc_requires_unlock");
    return 0;
}

/* The generic custom settings RPC writes these arrays without the setters'
 * check. A value that is not a layer index resolves to the keymap default;
 * 256 would otherwise be truncated to layer 0 and 257 to layer 1. The writes
 * stay in memory: the fixture has no settings backend to persist to. */
static int default_layer_invalid_value_test_run(void) {
    const int32_t invalid[] = {ZMK_KEYMAP_LAYERS_LEN, 256, 257, -3};
    const int index = zmk_endpoint_instance_to_index(zmk_endpoint_get_selected());
    const int32_t before = zmk_default_layer_get_endpoint(index);
    int ret = 0;

    for (size_t i = 0; i < ARRAY_SIZE(invalid) && ret == 0; i++) {
        struct zmk_custom_setting_value value = ZMK_CUSTOM_SETTING_VALUE_INT32(invalid[i]);
        int rc = zmk_custom_setting_write_array_by_key("cormoran__default_layer", "endpoint_layer",
                                                       index, &value,
                                                       ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
        if (rc != 0) {
            LOG_ERR("default layer invalid value test: write %d failed: %d", invalid[i], rc);
            ret = -EIO;
        } else if (zmk_default_layer_resolve_current() != zmk_keymap_layer_default()) {
            LOG_ERR("default layer invalid value test: %d resolved to %d", invalid[i],
                    zmk_default_layer_resolve_current());
            ret = -EINVAL;
        }
    }

    struct zmk_custom_setting_value restore = ZMK_CUSTOM_SETTING_VALUE_INT32(before);
    zmk_custom_setting_write_array_by_key("cormoran__default_layer", "endpoint_layer", index,
                                          &restore, ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (ret == 0) {
        LOG_INF("PASS: default_layer_resolve_rejects_invalid");
    }
    return ret;
}

static int default_layer_error_state_test_run(void) {
    const int32_t before = zmk_default_layer_get_os(0);
    zmk_studio_core_unlock();
    default_layer_test_fail_saves(true);
    int rc = default_layer_lock_test_set_os(0, 0);
    default_layer_test_fail_saves(false);
    zmk_studio_core_lock();
    const cormoran_default_layer_Response *response = default_layer_lock_test_response();
    bool passed = rc == 0 && default_layer_test_save_failures() > 0 &&
                  response->which_response_type == cormoran_default_layer_Response_error_tag &&
                  response->response_type.error.has_state;

    /* Exercise real nanopb encoding/decoding, including nested presence flags. */
    uint8_t bytes[512];
    pb_ostream_t output = pb_ostream_from_buffer(bytes, sizeof(bytes));
    cormoran_default_layer_Response decoded = cormoran_default_layer_Response_init_zero;
    if (passed && pb_encode(&output, cormoran_default_layer_Response_fields, response)) {
        pb_istream_t input = pb_istream_from_buffer(bytes, output.bytes_written);
        passed = pb_decode(&input, cormoran_default_layer_Response_fields, &decoded) &&
                 decoded.which_response_type == cormoran_default_layer_Response_error_tag &&
                 decoded.response_type.error.has_state &&
                 decoded.response_type.error.state.os_layers_count > 0 &&
                 decoded.response_type.error.state.os_layers[0].value == 0 &&
                 decoded.response_type.error.state.has_configurable_range;
    } else {
        passed = false;
    }
    /* Reusing the buffer for a locked refusal must clear the prior state. */
    if (default_layer_lock_test_set_os(0, before) != 0 || !default_layer_lock_test_refused() ||
        default_layer_lock_test_response()->response_type.error.has_state) {
        passed = false;
    }
    struct zmk_custom_setting_value restore = ZMK_CUSTOM_SETTING_VALUE_INT32(before);
    int restored = zmk_custom_setting_write_array_by_key(
        "cormoran__default_layer", "os_layer", 0, &restore, ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (!passed || restored != 0) {
        return -EINVAL;
    }
    LOG_INF("PASS: default_layer_rpc_failed_save_state");
    return 0;
}

static int default_layer_lock_test_init(void) {
    int ret = default_layer_lock_test_run();
    if (ret < 0) {
        LOG_ERR("FAIL: default_layer_rpc_requires_unlock (%d)", ret);
    }
    ret = default_layer_invalid_value_test_run();
    if (ret < 0) {
        LOG_ERR("FAIL: default_layer_resolve_rejects_invalid (%d)", ret);
    }
    ret = default_layer_error_state_test_run();
    if (ret < 0) {
        LOG_ERR("FAIL: default_layer_rpc_failed_save_state (%d)", ret);
    }
    return 0;
}

OWNER_CHECKED_SYS_INIT(default_layer_lock_test_init, APPLICATION, 99);
