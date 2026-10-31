import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { DefaultLayerPanel, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import { Response } from "../../web/src/proto/cormoran/default-layer/default_layer";
jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));
const reply = (layer: number) => ({
  custom: {
    call: {
      payload: Response.encode(
        Response.create({
          state: {
            resolvedLayer: layer,
            layerCount: 8,
            currentOs: 1,
            endpoints: [{ index: 1, isUsb: true, value: layer }],
          },
        }),
      ).finish(),
    },
  },
});
function deferred() {
  let resolve!: (v: ReturnType<typeof reply>) => void;
  let reject!: (e: Error) => void;
  const promise = new Promise<ReturnType<typeof reply>>((r, f) => {
    resolve = r;
    reject = f;
  });
  return { promise, resolve, reject };
}
beforeEach(() => jest.resetAllMocks());
it.each(["connection", "index"])(
  "ignores a retired failed save's runtime state after %s replacement",
  async (kind) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    if (kind === "index") second.state.connection = first.state.connection;
    second.findSubsystem = () => ({
      index: 4,
      identifier: SUBSYSTEM_IDENTIFIER,
    });
    const old = deferred();
    (call_rpc as jest.Mock)
      .mockResolvedValueOnce(reply(1))
      .mockReturnValueOnce(old.promise)
      .mockResolvedValueOnce(reply(3));
    const view = render(
      <ZMKAppProvider value={first}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    const endpoint = await screen.findByLabelText("USB default layer");
    fireEvent.change(endpoint, { target: { value: "2" } });
    await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(2));
    view.rerender(
      <ZMKAppProvider value={second}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await waitFor(() =>
      expect(screen.getByLabelText("USB default layer")).toHaveValue("3"),
    );
    await act(async () =>
      old.resolve({
        custom: {
          call: {
            payload: Response.encode(
              Response.create({
                error: {
                  message: "Retired save failed",
                  state: {
                    resolvedLayer: 7,
                    layerCount: 8,
                    endpoints: [{ index: 1, isUsb: true, value: 7 }],
                  },
                },
              }),
            ).finish(),
          },
        },
      }),
    );
    expect(screen.getByLabelText("USB default layer")).toHaveValue("3");
    expect(screen.queryByText(/Retired save failed/)).not.toBeInTheDocument();
    expect(
      screen.queryByRole("button", { name: "Retry last change" }),
    ).toBeNull();
  },
);
it.each(["connection", "index"])(
  "clears old mappings immediately when the %s changes",
  async (kind) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    if (kind === "index") second.state.connection = first.state.connection;
    second.findSubsystem = () => ({
      index: 4,
      identifier: SUBSYSTEM_IDENTIFIER,
    });
    const pending = deferred();
    (call_rpc as jest.Mock)
      .mockResolvedValueOnce(reply(2))
      .mockReturnValueOnce(pending.promise);
    const view = render(
      <ZMKAppProvider value={first}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await screen.findByLabelText("USB default layer");
    view.rerender(
      <ZMKAppProvider value={second}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    expect(
      screen.queryByLabelText("USB default layer"),
    ).not.toBeInTheDocument();
    await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(2));
    await act(async () => pending.resolve(reply(3)));
    expect(screen.getByLabelText("USB default layer")).toHaveValue("3");
  },
);
it.each(["connection", "index"])(
  "retires old refresh success and loading on %s replacement",
  async (kind) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    if (kind === "index") second.state.connection = first.state.connection;
    second.findSubsystem = () => ({
      index: 4,
      identifier: SUBSYSTEM_IDENTIFIER,
    });
    const old = deferred(),
      next = deferred();
    (call_rpc as jest.Mock)
      .mockResolvedValueOnce(reply(1))
      .mockReturnValueOnce(old.promise)
      .mockResolvedValueOnce(reply(2))
      .mockReturnValueOnce(next.promise);
    const view = render(
      <ZMKAppProvider value={first}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await screen.findByLabelText("USB default layer");
    fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
    await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(2));
    view.rerender(
      <ZMKAppProvider value={second}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await waitFor(() =>
      expect(screen.getByLabelText("USB default layer")).toHaveValue("2"),
    );
    fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
    await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(4));
    await act(async () => old.resolve(reply(7)));
    expect(screen.getByLabelText("USB default layer")).toHaveValue("2");
    expect(screen.getByLabelText("USB default layer")).toBeDisabled();
    await act(async () => next.resolve(reply(3)));
    expect(screen.getByLabelText("USB default layer")).toHaveValue("3");
  },
);
it.each(["connection", "index"])(
  "retires old refresh errors on %s replacement",
  async (kind) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    if (kind === "index") second.state.connection = first.state.connection;
    second.findSubsystem = () => ({
      index: 4,
      identifier: SUBSYSTEM_IDENTIFIER,
    });
    const old = deferred();
    (call_rpc as jest.Mock)
      .mockResolvedValueOnce(reply(1))
      .mockReturnValueOnce(old.promise)
      .mockResolvedValueOnce(reply(2));
    const view = render(
      <ZMKAppProvider value={first}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await screen.findByLabelText("USB default layer");
    fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
    await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(2));
    view.rerender(
      <ZMKAppProvider value={second}>
        <DefaultLayerPanel />
      </ZMKAppProvider>,
    );
    await waitFor(() =>
      expect(screen.getByLabelText("USB default layer")).toHaveValue("2"),
    );
    await act(async () => old.reject(new Error("retired request failed")));
    expect(
      screen.queryByText(/retired request failed/),
    ).not.toBeInTheDocument();
  },
);
