import { useCallback, useContext, useEffect, useRef, useState } from "react";
import "./App.css";
import { connect as serial_connect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import {
  ZMKConnection,
  ZMKCustomSubsystem,
  ZMKAppContext,
} from "@cormoran/zmk-studio-react-hook";
import {
  Request,
  Response,
  StateResponse,
} from "./proto/cormoran/default-layer/default_layer";

export const SUBSYSTEM_IDENTIFIER = "cormoran__default_layer";

const UNSET = -1;
const OS_DETECTION = -2;

const OS_LABELS = ["Unknown", "Windows", "macOS", "Linux", "iOS", "Android"];

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>🗂️ ZMK Default Layer</h1>
        <p>Per-connection and per-OS default layer configuration</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card">
            <h2>Device Connection</h2>
            {isLoading && <p>⏳ Connecting...</p>}
            {error && (
              <div className="error-message">
                <p>🚨 {error}</p>
              </div>
            )}
            {!isLoading && (
              <button
                className="btn btn-primary"
                onClick={() => connect(serial_connect)}
              >
                🔌 Connect Serial
              </button>
            )}
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card">
              <h2>Device Connection</h2>
              <div className="device-info">
                <h3>✅ Connected to: {deviceName}</h3>
              </div>
              <button className="btn btn-secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>

            <DefaultLayerPanel />
          </>
        )}
      />

      <footer className="app-footer">
        <p>Per-endpoint and per-OS default layer for ZMK</p>
      </footer>
    </div>
  );
}

function layerValueOptions(
  state: StateResponse,
  allowOsDetection: boolean,
  storedValue: number
) {
  const options: { value: number; label: string; disabled?: boolean }[] = [
    { value: UNSET, label: "— (unset)" },
  ];
  if (allowOsDetection) {
    options.push({ value: OS_DETECTION, label: "OS detection" });
  }
  const min = state.configurableRange?.minIndex ?? 0;
  const max = Math.min(
    state.configurableRange?.maxIndex ?? state.layerCount - 1,
    state.layerCount - 1
  );
  for (let i = min; i <= max; i++) {
    options.push({ value: i, label: `Layer ${i}` });
  }
  if (!options.some((option) => option.value === storedValue)) {
    options.push({
      value: storedValue,
      label:
        storedValue >= 0
          ? `Unavailable layer ${storedValue}`
          : `Unsupported value ${storedValue}`,
      disabled: true,
    });
  }
  return options;
}

function endpointLabel(endpoint: { isUsb: boolean; bleProfileIndex: number }) {
  return endpoint.isUsb ? "USB" : `BLE profile ${endpoint.bleProfileIndex}`;
}

const connectionIds = new WeakMap<object, number>();
let nextConnectionId = 1;
export function DefaultLayerPanel() {
  const app = useContext(ZMKAppContext);
  const connection = app?.state.connection;
  let identity = 0;
  if (connection) {
    identity = connectionIds.get(connection) ?? nextConnectionId++;
    connectionIds.set(connection, identity);
  }
  const index = app?.findSubsystem(SUBSYSTEM_IDENTIFIER)?.index;
  return <DefaultLayerSession key={`${identity}:${index ?? "missing"}`} />;
}

function DefaultLayerSession() {
  const zmkApp = useContext(ZMKAppContext);
  const [state, setState] = useState<StateResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [failedRequest, setFailedRequest] = useState<Request | null>(null);

  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER);
  const connection = zmkApp?.state.connection;
  const subsystemIndex = subsystem?.index;
  const lifetime = useRef({ active: true, request: 0 });
  useEffect(() => {
    const current = lifetime.current;
    current.active = true;
    return () => {
      current.active = false;
      current.request++;
    };
  }, []);

  const callRPC = useCallback(
    async (request: Request): Promise<StateResponse | null> => {
      const current = lifetime.current;
      if (!current.active || !connection || subsystemIndex === undefined)
        return null;
      const ticket = ++current.request;
      const owns = () => current.active && current.request === ticket;
      setIsLoading(true);
      setError(null);
      setFailedRequest(null);
      try {
        const service = new ZMKCustomSubsystem(connection, subsystemIndex);
        const responsePayload = await service.callRPC(
          Request.encode(request).finish()
        );
        if (!owns()) return null;
        if (!responsePayload) throw new Error("Empty default-layer response");
        const resp = Response.decode(responsePayload);
        if (resp.error) {
          if (resp.error.state) setState(resp.error.state);
          throw new Error(resp.error.message || "Default-layer request failed");
        }
        if (!resp.state) throw new Error("Missing default-layer state");
        setState(resp.state);
        return resp.state;
      } catch (err) {
        if (owns()) {
          setError(err instanceof Error ? err.message : "Unknown error");
          if (request.setEndpointLayer || request.setOsLayer)
            setFailedRequest(request);
        }
        return null;
      } finally {
        if (owns()) setIsLoading(false);
      }
    },
    [connection, subsystemIndex]
  );

  const refresh = useCallback(() => {
    void callRPC(Request.create({ getState: {} }));
  }, [callRPC]);

  useEffect(() => {
    const timer = window.setTimeout(refresh, 0);
    return () => window.clearTimeout(timer);
  }, [refresh]);

  if (!zmkApp) return null;

  if (!subsystem) {
    return (
      <section className="card">
        <div className="warning-message">
          <p>
            ⚠️ Subsystem "{SUBSYSTEM_IDENTIFIER}" not found. Make sure your
            firmware includes the default-layer module.
          </p>
        </div>
      </section>
    );
  }

  const setEndpointLayer = (endpointIndex: number, value: number) =>
    callRPC(Request.create({ setEndpointLayer: { endpointIndex, value } }));

  const setOsLayer = (os: number, value: number) =>
    callRPC(Request.create({ setOsLayer: { os, value } }));

  return (
    <section className="card">
      <h2>Default Layer</h2>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
          {failedRequest && (
            <button
              disabled={isLoading}
              onClick={() => void callRPC(failedRequest)}
            >
              Retry last change
            </button>
          )}
        </div>
      )}

      {!state ? (
        <p>{isLoading ? "⏳ Loading..." : "No data yet."}</p>
      ) : (
        <>
          <div className="status-line">
            <p>
              Current OS: <strong>{OS_LABELS[state.currentOs] ?? "?"}</strong>
              {" · "}
              Resolved layer: <strong>{state.resolvedLayer}</strong>
            </p>
          </div>

          <h3>Connections</h3>
          <table className="endpoint-table">
            <thead>
              <tr>
                <th>Connection</th>
                <th>Default layer</th>
              </tr>
            </thead>
            <tbody>
              {state.endpoints.map((endpoint) => (
                <tr
                  key={endpoint.index}
                  className={
                    endpoint.index === state.activeEndpointIndex
                      ? "active-row"
                      : undefined
                  }
                >
                  <td>
                    {endpointLabel(endpoint)}
                    {endpoint.index === state.activeEndpointIndex
                      ? " (active)"
                      : ""}
                  </td>
                  <td>
                    <select
                      aria-label={`${endpointLabel(endpoint)} default layer`}
                      value={endpoint.value}
                      disabled={isLoading}
                      onChange={(e) =>
                        setEndpointLayer(
                          endpoint.index,
                          parseInt(e.target.value, 10)
                        )
                      }
                    >
                      {layerValueOptions(state, true, endpoint.value).map(
                        (opt) => (
                          <option
                            key={opt.value}
                            value={opt.value}
                            disabled={opt.disabled}
                          >
                            {opt.label}
                          </option>
                        )
                      )}
                    </select>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>

          <h3>Per-OS default layer</h3>
          {!state.osDetectionAvailable && (
            <p className="hint">
              OS detection is not enabled in this firmware build. Set a
              connection above to "OS detection" once it is.
            </p>
          )}
          <table className="os-table">
            <thead>
              <tr>
                <th>OS</th>
                <th>Default layer</th>
              </tr>
            </thead>
            <tbody>
              {state.osLayers.map((entry) => (
                <tr key={entry.os}>
                  <td>{OS_LABELS[entry.os] ?? entry.os}</td>
                  <td>
                    <select
                      aria-label={`${OS_LABELS[entry.os] ?? entry.os} default layer`}
                      value={entry.value}
                      disabled={isLoading || !state.osDetectionAvailable}
                      onChange={(e) =>
                        setOsLayer(entry.os, parseInt(e.target.value, 10))
                      }
                    >
                      {layerValueOptions(state, false, entry.value).map(
                        (opt) => (
                          <option
                            key={opt.value}
                            value={opt.value}
                            disabled={opt.disabled}
                          >
                            {opt.label}
                          </option>
                        )
                      )}
                    </select>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}
      <button
        className="btn btn-secondary"
        disabled={isLoading || (state === null && error === null)}
        onClick={refresh}
      >
        {isLoading ? "⏳ Refreshing..." : "🔄 Refresh"}
      </button>
    </section>
  );
}

export default App;
