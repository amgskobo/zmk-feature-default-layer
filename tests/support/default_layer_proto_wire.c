/* SPDX-License-Identifier: MIT */
/* Fixed wire vectors and full-capacity cross-codec checks; no Zephyr mocks. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <cormoran/default-layer/default_layer.pb.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "wire assertion failed: %s\n", #condition);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int write_message(const char *path, const pb_msgdesc_t *fields, const void *message) {
    uint8_t bytes[1024];
    pb_ostream_t stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    CHECK(pb_encode(&stream, fields, message));
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    size_t written = fwrite(bytes, 1, stream.bytes_written, file);
    int rc = fclose(file);
    CHECK(written == stream.bytes_written && rc == 0);
    return 0;
}

static int read_message(const char *path, const pb_msgdesc_t *fields, void *message) {
    uint8_t bytes[1024];
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    int error = ferror(file);
    int rc = fclose(file);
    CHECK(error == 0 && rc == 0 && size < sizeof(bytes));
    pb_istream_t stream = pb_istream_from_buffer(bytes, size);
    CHECK(pb_decode(&stream, fields, message) && stream.bytes_left == 0);
    return 0;
}

static void fill_available(cormoran_default_layer_AvailableLayers *available) {
    available->indices_count = 32;
    for (unsigned i = 0; i < 32; ++i)
        available->indices[i] = i;
}

static int available(const char *operation, const char *path) {
    cormoran_default_layer_AvailableLayers layers =
        cormoran_default_layer_AvailableLayers_init_zero;
    CHECK(sizeof(layers.indices) / sizeof(layers.indices[0]) == 32);
    if (strcmp(operation, "produce-available") == 0) {
        fill_available(&layers);
        CHECK(write_message(path, cormoran_default_layer_AvailableLayers_fields, &layers) == 0);
    } else {
        CHECK(read_message(path, cormoran_default_layer_AvailableLayers_fields, &layers) == 0);
    }
    CHECK(layers.indices_count == 32);
    for (unsigned i = 0; i < 32; ++i)
        CHECK(layers.indices[i] == i);
    puts("PASS: available32 ordered0..31");
    return 0;
}

static void fill_response(cormoran_default_layer_Response *response) {
    response->which_response_type = cormoran_default_layer_Response_state_tag;
    cormoran_default_layer_StateResponse *state = &response->response_type.state;
    state->endpoints_count = 10;
    for (unsigned i = 0; i < 10; ++i) {
        state->endpoints[i].index = i;
        state->endpoints[i].is_usb = i == 1;
        state->endpoints[i].ble_profile_index = i >= 2 ? i - 2 : 0;
        state->endpoints[i].value = (int32_t)i - 2;
    }
    state->os_layers_count = 6;
    for (unsigned i = 0; i < 6; ++i) {
        state->os_layers[i].os = i;
        state->os_layers[i].value = i == 5 ? INT32_MAX : (int32_t)i - 1;
    }
    state->active_endpoint_index = 9;
    state->current_os = 5;
    state->resolved_layer = 31;
    state->layer_count = 32;
    state->os_detection_available = true;
    state->has_configurable_range = true;
    state->configurable_range.min_index = 0;
    state->configurable_range.max_index = 31;
    state->has_available_layers = true;
    fill_available(&state->available_layers);
    state->application_pending = true;
}

static int response(const char *operation, const char *path) {
    cormoran_default_layer_Response message = cormoran_default_layer_Response_init_zero;
    if (strcmp(operation, "produce-response") == 0) {
        fill_response(&message);
        CHECK(write_message(path, cormoran_default_layer_Response_fields, &message) == 0);
    } else {
        CHECK(read_message(path, cormoran_default_layer_Response_fields, &message) == 0);
    }
    CHECK(message.which_response_type == cormoran_default_layer_Response_state_tag);
    const cormoran_default_layer_StateResponse *state = &message.response_type.state;
    CHECK(state->endpoints_count == 10 && state->os_layers_count == 6);
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(state->endpoints[i].index == i && state->endpoints[i].is_usb == (i == 1));
        CHECK(state->endpoints[i].ble_profile_index == (i >= 2 ? i - 2 : 0));
        CHECK(state->endpoints[i].value == (int32_t)i - 2);
    }
    for (unsigned i = 0; i < 6; ++i) {
        CHECK(state->os_layers[i].os == i);
        CHECK(state->os_layers[i].value == (i == 5 ? INT32_MAX : (int32_t)i - 1));
    }
    CHECK(state->active_endpoint_index == 9 && state->current_os == 5);
    CHECK(state->resolved_layer == 31 && state->layer_count == 32 &&
          state->os_detection_available && state->application_pending);
    CHECK(state->has_configurable_range && state->configurable_range.min_index == 0 &&
          state->configurable_range.max_index == 31);
    CHECK(state->has_available_layers && state->available_layers.indices_count == 32);
    for (unsigned i = 0; i < 32; ++i)
        CHECK(state->available_layers.indices[i] == i);
    puts("PASS: response endpoints10 os6 layers32 sentinels wide-saved-value presence pending");
    return 0;
}

static int request_widths(void) {
    /* Request.set_endpoint_layer(endpoint_index=257, value=0), specified bytes. */
    const uint8_t wide_index[] = {0x12, 0x03, 0x08, 0x81, 0x02};
    cormoran_default_layer_Request decoded = cormoran_default_layer_Request_init_zero;
    pb_istream_t input = pb_istream_from_buffer(wide_index, sizeof(wide_index));
    CHECK(pb_decode(&input, cormoran_default_layer_Request_fields, &decoded));
    CHECK(decoded.which_request_type == cormoran_default_layer_Request_set_endpoint_layer_tag &&
          decoded.request_type.set_endpoint_layer.endpoint_index == 257);
    CHECK(sizeof(decoded.request_type.set_endpoint_layer.endpoint_index) == 4 &&
          sizeof(decoded.request_type.set_endpoint_layer.value) == 4 &&
          sizeof(decoded.request_type.set_os_layer.os) == 4 &&
          sizeof(decoded.request_type.set_os_layer.value) == 4);

    for (unsigned variant = 0; variant < 2; ++variant) {
        cormoran_default_layer_Request request = cormoran_default_layer_Request_init_zero;
        request.which_request_type = variant
                                         ? cormoran_default_layer_Request_set_os_layer_tag
                                         : cormoran_default_layer_Request_set_endpoint_layer_tag;
        if (variant) {
            request.request_type.set_os_layer.os = UINT32_MAX;
            request.request_type.set_os_layer.value = INT32_MIN;
        } else {
            request.request_type.set_endpoint_layer.endpoint_index = UINT32_MAX;
            request.request_type.set_endpoint_layer.value = INT32_MAX;
        }
        uint8_t bytes[64];
        pb_ostream_t output = pb_ostream_from_buffer(bytes, sizeof(bytes));
        CHECK(pb_encode(&output, cormoran_default_layer_Request_fields, &request));
        decoded = (cormoran_default_layer_Request)cormoran_default_layer_Request_init_zero;
        input = pb_istream_from_buffer(bytes, output.bytes_written);
        CHECK(pb_decode(&input, cormoran_default_layer_Request_fields, &decoded));
        CHECK(decoded.which_request_type == request.which_request_type);
        if (variant) {
            CHECK(decoded.request_type.set_os_layer.os == UINT32_MAX &&
                  decoded.request_type.set_os_layer.value == INT32_MIN);
        } else {
            CHECK(decoded.request_type.set_endpoint_layer.endpoint_index == UINT32_MAX &&
                  decoded.request_type.set_endpoint_layer.value == INT32_MAX);
        }
    }
    puts("PASS: incoming257 UINT32_MAX INT32_MIN INT32_MAX remain full-width");
    return 0;
}

static int overflow(void) {
    /* AvailableLayers.indices=[256], specified packed uint32 bytes. */
    const uint8_t bytes[] = {0x0a, 0x02, 0x80, 0x02};
    cormoran_default_layer_AvailableLayers decoded =
        cormoran_default_layer_AvailableLayers_init_zero;
    pb_istream_t input = pb_istream_from_buffer(bytes, sizeof(bytes));
    bool ok = pb_decode(&input, cormoran_default_layer_AvailableLayers_fields, &decoded);
    if (sizeof(decoded.indices[0]) == 1) {
        CHECK(!ok);
        puts("PASS: outgoing-only byte decoder rejects256 without truncation");
    } else {
        CHECK(ok && decoded.indices_count == 1 && decoded.indices[0] == 256);
        puts("PASS: original full-width decoder retains256");
    }
    return 0;
}

static int storage(void) {
    const size_t width = sizeof(((cormoran_default_layer_AvailableLayers *)0)->indices[0]);
    printf("storage: available=%zu state=%zu response=%zu request=%zu element=%zu capacity=%zu\n",
           sizeof(cormoran_default_layer_AvailableLayers),
           sizeof(cormoran_default_layer_StateResponse), sizeof(cormoran_default_layer_Response),
           sizeof(cormoran_default_layer_Request), width,
           sizeof(((cormoran_default_layer_AvailableLayers *)0)->indices) / width);
    if (width != 1) {
        fprintf(stderr, "storage assertion failed: expected byte indices, actual=%zu\n", width);
        return 2;
    }
    puts("PASS: byte storage capacity32");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "storage") == 0)
        return storage();
    if (argc == 2 && strcmp(argv[1], "request-widths") == 0)
        return request_widths();
    if (argc == 2 && strcmp(argv[1], "overflow") == 0)
        return overflow();
    if (argc == 3 &&
        (strcmp(argv[1], "produce-available") == 0 || strcmp(argv[1], "consume-available") == 0))
        return available(argv[1], argv[2]);
    if (argc == 3 &&
        (strcmp(argv[1], "produce-response") == 0 || strcmp(argv[1], "consume-response") == 0))
        return response(argv[1], argv[2]);
    fputs("invalid wire fixture operation\n", stderr);
    return 3;
}
