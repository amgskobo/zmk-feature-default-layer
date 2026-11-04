# zmk-feature-default-layer - Web Frontend

Web UI for configuring `zmk-feature-default-layer` over the unofficial custom
ZMK Studio RPC protocol: pick a default layer per connection (USB / each BLE
profile), or set it to "OS detection" and let it follow a per-OS mapping
resolved by [zmk-feature-os-detection](https://github.com/cormoran/zmk-feature-os-detection).

## Quick Start

```bash
# Install dependencies
npm install

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
After changing dependencies, run `npm audit --omit=dev`, the Web tests and the
production build.
