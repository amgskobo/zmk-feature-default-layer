/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/keymap.h>

/* Stored per-endpoint / per-OS layer values. Non-negative values are a plain
 * current keymap layer index, not a stable ID: reordering changes which layer
 * it names. Missing, out-of-range and below-global-default indices remain
 * stored but resolve to the current index of the stable global default. The
 * sentinels share the same int32 storage as custom settings and Studio RPC. */
#define ZMK_DEFAULT_LAYER_UNSET (-1)
#define ZMK_DEFAULT_LAYER_OS_DETECTION (-2)

/* enum zmk_os (zmk-feature-os-detection) has 6 values: unknown, windows,
 * macos, linux, ios, android. Kept as a plain count here so callers that
 * don't need CONFIG_ZMK_DEFAULT_LAYER_OS_DETECTION don't need
 * os_detection.h just to iterate the per-OS mapping. */
#define ZMK_DEFAULT_LAYER_OS_COUNT 6

/* Resolve and (re-)apply the default layer for the currently selected
 * endpoint, on the system workqueue used by ordinary key events. Other thread
 * callers wait for queued work unless an application is already in flight.
 * Event listeners only request work and never wait. Updates during application are
 * coalesced, with at most four reconciliation passes per invocation; remaining
 * work returns to the queue tail. Reentrant/in-flight callers do not wait.
 * Check application_pending before treating a desired mapping as applied. */
void zmk_default_layer_resolve_and_apply(const char *reason);

/* Resolve the currently selected endpoint's configured value (layer index,
 * UNSET, or OS_DETECTION) down to a concrete keymap layer index - the same
 * resolution zmk_default_layer_resolve_and_apply() applies, without
 * re-activating anything. Used as the baseline for the DF_INC behavior and
 * reported to Studio RPC clients. Returns UNSET if the stable global default
 * is missing from the current ordering, rather than aliasing another layer. */
int32_t zmk_default_layer_resolve_current(void);

/* Whether an explicit index is assignable in the current keymap ordering.
 * A layer below the stable global default is never visited by key dispatch. */
bool zmk_default_layer_index_available(int32_t index);
bool zmk_default_layer_application_pending(void);

/* Get/set the configured value (layer index, UNSET, or OS_DETECTION) for one
 * endpoint, addressed by zmk_endpoint_instance_to_index(). A persistence error
 * is returned even when the registry retains a new runtime value; the setter
 * still reconciles the effective mapping. Read back before retrying. */
int32_t zmk_default_layer_get_endpoint(uint8_t endpoint_index);
int zmk_default_layer_set_endpoint(uint8_t endpoint_index, int32_t value, const char *command);

/* Get/set the configured value (layer index or UNSET) for one OS, addressed
 * by `enum zmk_os` (0=unknown, 1=windows, 2=macos, 3=linux, 4=ios,
 * 5=android). OS_DETECTION is not a valid value here. Persistence errors have
 * the same runtime-value semantics as the endpoint setter. */
int32_t zmk_default_layer_get_os(uint8_t os);
int zmk_default_layer_set_os(uint8_t os, int32_t value, const char *command);
