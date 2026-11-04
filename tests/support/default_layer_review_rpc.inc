/* SPDX-License-Identifier: MIT */
/* Test-only entry into the real handler and nanopb codec. */
int default_layer_review_rpc_state(const uint32_t *expected, size_t count, uint32_t live_count,
                                   bool pending) {
    cormoran_default_layer_Request request = cormoran_default_layer_Request_init_zero;
    request.which_request_type = cormoran_default_layer_Request_get_state_tag;
    zmk_custom_CallRequest raw = zmk_custom_CallRequest_init_zero;
    pb_ostream_t output = pb_ostream_from_buffer(raw.payload.bytes, sizeof(raw.payload.bytes));
    if (!pb_encode(&output, cormoran_default_layer_Request_fields, &request))
        return -EIO;
    raw.payload.size = output.bytes_written;
    pb_callback_t encode = {0};
    if (!default_layer_rpc_handle_request(&raw, &encode))
        return -EIO;
    const cormoran_default_layer_Response *response =
        &zmk_rpc_custom_subsystem_response_buffer_cormoran__default_layer;
    uint8_t wire[512];
    output = pb_ostream_from_buffer(wire, sizeof(wire));
    cormoran_default_layer_Response decoded = cormoran_default_layer_Response_init_zero;
    if (!pb_encode(&output, cormoran_default_layer_Response_fields, response))
        return -EIO;
    pb_istream_t input = pb_istream_from_buffer(wire, output.bytes_written);
    if (!pb_decode(&input, cormoran_default_layer_Response_fields, &decoded) ||
        decoded.which_response_type != cormoran_default_layer_Response_state_tag)
        return -EIO;
    const cormoran_default_layer_StateResponse *state = &decoded.response_type.state;
    if (!state->has_available_layers || state->available_layers.indices_count != count ||
        state->layer_count != live_count || state->application_pending != pending ||
        !state->has_configurable_range || state->configurable_range.min_index != 1 ||
        state->configurable_range.max_index != 3)
        return -EINVAL;
    for (size_t i = 0; i < count; ++i) {
        if (state->available_layers.indices[i] != expected[i])
            return -EINVAL;
    }
    return 0;
}
