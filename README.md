# zmk-feature-default-layer

## Reviewed fork

This fork's reviewed source is on `codex/default-layer-review` in
[`amgskobo/zmk-feature-default-layer`](https://github.com/amgskobo/zmk-feature-default-layer/tree/codex/default-layer-review).
Select that repository URL and revision in the module's west project entry.
Keep the module's documented imports enabled where required. The original
upstream installation examples below describe the upstream repository.

The dependency manifests select the available Custom Settings revision
`c38ce14beb07ecebc266c809870a3540942cdb24` from its reviewed fork;
production imports and isolated tests use the same registry implementation.
OS Detection is pinned to
`6def6f3ca29007ac083f90016dcb09683ff4dad7` from its reviewed fork as well.
CI preparation checks both named `codex/*-review` branch tips against these pins
before fetching dependencies. Update and review a pin explicitly when its
review branch advances.

ZMK module to switch the default (always-on) layer depending on the
currently connected endpoint (USB or a BLE profile) — and, optionally,
depending on the detected host operating system.

Originally based on
[elpekenin's ZMK PR #2222](https://github.com/zmkfirmware/zmk/pull/2222);
this version adds runtime configuration via a Web UI, using the
**unofficial** custom ZMK Studio RPC protocol.

## Features

- **Per-connection default layer**: assign a default layer to each output
  (USB, BLE profile 0-4). Switching connections automatically activates the
  configured layer.
- **Per-OS default layer**: assign a default layer per detected host OS
  (Windows / macOS / Linux / iOS / Android / Unknown), using
  [zmk-feature-os-detection](https://github.com/cormoran/zmk-feature-os-detection).
  Set a connection's mapping to "OS detection" to resolve its layer this way
  instead of a fixed layer.
- **Web UI**: configure both mappings from a browser over WebSerial — no
  reflash needed to change layer assignments.
- **`&df` keymap behavior** (unchanged from the original module): `&df DF_SEL
  <layer>` sets the current connection's layer directly; `&df DF_INC` cycles
  through the configured `[MIN_INDEX, MAX_INDEX]` range.

## Module User Guide

1. Add the dependency to your `config/west.yml`. This module requires the
   patched `cormoran/zmk` fork with custom Studio RPC support, and depends on
   `zmk-feature-custom-settings` (always) and `zmk-feature-os-detection`
   (only needed for the per-OS feature — see its own west.yml import).

   ```yaml
   manifest:
     remotes:
       - name: cormoran
         url-base: https://github.com/cormoran
     projects:
       - name: zmk-feature-default-layer
         remote: cormoran
         revision: main
         import: true
       # Required: patched ZMK with custom Studio RPC support
       - name: zmk
         remote: cormoran
         revision: main+custom-studio-protocol
         import:
           file: app/west.yml
   ```

2. Enable flags in your `config/<shield>.conf`:

   ```conf
   CONFIG_ZMK_DEFAULT_LAYER=y
   CONFIG_ZMK_DEFAULT_LAYER_MAX_INDEX=<highest configurable layer index>

   # Optionally enable the Web UI
   CONFIG_ZMK_STUDIO=y
   CONFIG_ZMK_DEFAULT_LAYER_STUDIO_RPC=y
   CONFIG_ZMK_CUSTOM_SETTINGS=y
   CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y
   CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128
   CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048

   # Optionally enable per-OS default layer (needs zmk-feature-os-detection)
   CONFIG_ZMK_DEFAULT_LAYER_OS_DETECTION=y
   CONFIG_ZMK_OS_DETECTION=y
   CONFIG_ZMK_OS_DETECTION_USB=y
   CONFIG_ZMK_OS_DETECTION_BLE=y
   ```

   Do not also enable `zmk-feature-os-detection`'s own
   `CONFIG_ZMK_OS_DETECTION_LAYER_*` auto-switch options together with this
   module — both would compete to activate/deactivate layers.

   Only live indices in the inclusive `[MIN_INDEX, MAX_INDEX]` range at or above
   the current global default can be assigned. Both bounds must be within the
   compiled keymap capacity. ZMK does not dispatch keys through layers below
   its global default. Direct setters reject unavailable indices; older stored
   or imported values remain visible but resolve to the current index of the
   stable global default. Unset uses that same fallback, which may be outside
   the configurable range. The Web UI receives the bounds and an authoritative
   list of available indices, including holes or an empty list, and keeps an
   unavailable stored value as a disabled option until replacement succeeds.

   Values are indices in the **current ordering**, not stable layer IDs.
   Reordering changes which layer a saved number names. Studio move, removal,
   addition, reset and discard notifications reconcile the active mapping and
   retire its previously applied ID. An invalid saved number is not silently
   changed to another number or erased; if its position becomes available again,
   it follows that position as before. No narrow integer conversion is used to
   turn an invalid saved value into another layer.

   Runtime application uses the system workqueue used by physical and split key
   events. Event listeners request work without waiting. Explicit setters
   synchronize with queued work unless application is already in flight;
   reentrant/in-flight changes coalesce. Each invocation performs at most four
   reconciliation passes, then returns remaining work to the queue tail.
   GetState reads the desired mapping without waiting for that queue.
   `resolved_layer` is the desired mapping; `application_pending` says that its
   application is still queued or running. The Web UI shows that condition and
   Refresh reads completion. A successful write alone is not proof of completion.

3. Optionally add `&df` to your keymap if you want to change the mapping
   from the keyboard itself, in addition to the Web UI:

   ```dts
   #include <behaviors/default_layer.dtsi>
   #include <dt-bindings/zmk_behavior_default_layer/default_layer.h>

   / {
       keymap {
           default_layer {
               bindings = <&df DF_INC>;
           };
       };
   };
   ```

4. Connect over WebSerial from the [Web UI](https://cormoran.github.io/zmk-feature-default-layer/)
   to assign a layer (or "OS detection") to each connection, and a layer to
   each OS.

### Studio lock

`GetState` works while Studio is locked. Setting an endpoint's or an OS's
default layer needs an unlocked Studio, because it persists and picks the base
layer every host gets; a locked keyboard answers "Unlock required" and changes
nothing. Both settings arrays are registered with secure write permission, so
the generic custom settings RPC cannot change them while locked either. The
`&df` keymap behavior is unaffected.

### Save errors

The settings registry can retain a new runtime value when saving to storage
fails. Endpoint and OS setters still apply the effective runtime mapping and
return the save error. They do not roll back over a newer update. RPC errors
include the current runtime state; the Web UI displays it together with the
error. These values may be unsaved, so check the current state before retrying.
The explicit **Retry last change** action resends the failed assignment even if
its runtime value is already selected. Older errors without a state leave the
displayed mapping unchanged.

Removing ZMK's stable global default leaves the core without its dispatch floor;
this module cannot repair that core keymap state. Resolution reports `-1` instead
of selecting an unrelated layer at index zero, retires its managed layer and
does not activate an invalid ID. Direct users of core structural-edit APIs must
request reconciliation afterward; Studio supplies the notifications automatically.

The reconciliation on a failed save above belongs to this module's endpoint and
OS setters. A failed `PERSIST` write through generic Custom Settings RPC may also
retain a new RAM value, but the registry emits no change event for failed writes.
Retry the write or save successfully to trigger application, or use this module's
setter. `GetState` reads the desired mapping and does not request application;
`application_pending=false` only says no application is queued or running.

### Web UI

See [web/README.md](./web/README.md) for web UI development instructions.

### Publishing Web UI

**GitHub Pages**: Merge a pull request into `main+custom-studio-protocol` to deploy to `https://<account>.github.io/<repo>/`.

**Cloudflare Workers (PR previews)**: Configure `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` secrets.

## Module Development Guide

### Setup for running test

#### Option0: Dev container (recommended)

Open this repository in VS Code with the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers). The container automatically initializes the west workspace using the isolated layout.

#### Option1: west workspace directory layout

Set west topdir as parent of repository root and download dependencies under `../`.
This layout is useful to reduce disk usage by sharing dependencies with other zephyr modules.
The build result is located in `../build`.

```bash
mkdir west-workspace
cd west-workspace # this directory becomes west workspace root (topdir)
git clone <this repository>
# rm -r .west # if exists to reset workspace
west init -l . --mf west/west-test-workspace.yml
west update --narrow
west zephyr-export
```

#### Option2: isolated directory layout

Set west topdir as repository root and download dependencies under `./dependencies`.
This layout is useful if you don't want to share dependencies to other zephyr modules.
Dev container and github actions uses this layout.
The build result is located in `./build`.

```bash
git clone <this repository>
cd <cloned directory>
west init -l west --mf west-test-isolated.yml
west update --narrow
west zephyr-export
```

### Pre-commit

Every commit need to pass pre-commit verification. The verification contains formatting code and running tests.

```
pip install pre-commit
pre-commit install

# Run pre-commit manually
pre-commit run --all-files
# Run for git staged files
pre-commit run
```

### Running Test

```bash
# Run the complete owner checks, native fixtures and board builds
python3 test.py -v
# Run build test directly (diagnostic subset)
west zmk-build tests/zmk-config
# Run native snapshots directly (diagnostic subset)
west zmk-test tests -m .
# Generate schema types and run every Web test
bash tests/run-web.sh
```

### Sync changes from template

Run `Actions > Sync Changes in Template > Run workflow` to get the latest template changes as a pull request.

If the template contains changes in `.github/workflows/*`, register a GitHub personal access token as `GH_TOKEN` repository secret (`repo` + `workflow` scopes).

### Coding agent on actions

Actions for github copilot and claude are available.

- Mention `@copilot`
- Setup `ANTHROPIC_API_KEY` secret and mention `@claude`
  - Or fix [claude.yml](./github/workflows/claude.yml) to use `CLAUDE_CODE_OAUTH_TOKEN`
