# zmk-feature-default-layer - Web Frontend

Web UI for configuring `zmk-feature-default-layer` over the unofficial custom
ZMK Studio RPC protocol: pick a default layer per connection (USB / each BLE
profile), or set it to "OS detection" and let it follow a per-OS mapping
resolved by [zmk-feature-os-detection](https://github.com/cormoran/zmk-feature-os-detection).

## Quick Start

```bash
# Prepare the locked dependencies (networked preparation)
npm ci

# Generate TypeScript types from proto
npm run generate

# Run development server
npm run dev

# Build for production
npm run build

# Run tests
npm test
```

## Project Structure

```
src/
├── main.tsx              # React entry point
├── App.tsx               # Connection UI + DefaultLayerPanel
├── App.css               # Styles
└── proto/                # Generated protobuf TypeScript types
    └── cormoran/default-layer/
        └── default_layer.ts

test/
├── App.spec.tsx                # Tests for App component / connection flow
└── DefaultLayerPanel.spec.tsx  # Tests for the endpoint/OS layer panel
```

## How It Works

### 1. Protocol Definition

The protobuf schema is defined in `../proto/cormoran/default-layer/default_layer.proto`.
`value` fields share one sentinel encoding: `>=0` is a keymap layer index,
`-1` means unset, and `-2` (endpoint values only) means "resolve via the
per-OS mapping".

Saved nonnegative values follow the current layer ordering. Firmware with
`available_layers` supplies the exact indices that exist, lie within its range
and are at or above the current global default. The submessage is authoritative
even when empty; removed/unavailable saved numbers remain selected as disabled
options until a permitted replacement succeeds. Legacy firmware without that
submessage retains range-based choices. A live count alone cannot describe holes.

`resolved_layer` is the desired mapping. If `application_pending` is true, the
system-workqueue application has not finished; the panel says so and Refresh
reads completion. GetState remains a nonblocking read of that public state.

### 2. Code Generation

TypeScript types are generated using `ts-proto`:

```bash
npm run generate
```

This runs `buf generate` which uses the configuration in `buf.gen.yaml`.

### 3. Using react-zmk-studio

```typescript
import { useZMKApp, ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";

const { state, connect, findSubsystem, isConnected } = useZMKApp();
const subsystem = findSubsystem("cormoran__default_layer");
const service = new ZMKCustomSubsystem(state.connection, subsystem.index);
const response = await service.callRPC(payload);
```

## Testing

From the module root, use `bash tests/run-web.sh` inside the prepared container
for canonical schema generation and the complete Web selection, including
`tests/web/`. Run the separate `npm run lint` and `npm run build` checks as well.

```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run tests with coverage
npm run test:coverage
```

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
After changing dependencies, retain both `npm audit --json` and
`npm audit --omit=dev --json`, their actual exits and advisory counts. The full
audit still fails if development dependencies have findings; an empty
production-only audit does not establish whole-product security.

The 2026-11-07 compatible development-transitive lock update preserves the root
declarations, every direct locked dependency, Git SDK/hook revisions, protobuf
override and production dependency records. Every changed incoming dependency
range must accept the selected version. Full audit findings decreased from 29
(one critical, eight high, fifteen moderate, five low) to eighteen (fourteen
moderate, four low); production-only audits reported zero in both states.
Remaining findings require separate dependency-boundary review, not a forced
update of the declared tools.

Use a fresh cache for scripts-enabled `npm ci` and record the actual Linux
lifecycle packages. The fork-review workflow selects Node 22.23.4; the general
Web workflow selects Node 24. Verify generation, all Web tests, lint and build
with each selected runtime. Use the installed SDK's runtime and declarations for
these checks; a source-rich reference checkout is a separate input. These local
checks do not establish hosted CI, browser transport or physical acceptance.
