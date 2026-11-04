import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { DefaultLayerPanel, SUBSYSTEM_IDENTIFIER } from "../src/App";
import {
  Response,
  StateResponse,
} from "../src/proto/cormoran/default-layer/default_layer";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn(),
}));

import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";

function mockRpcResponse(state: StateResponse) {
  const payload = Response.encode(Response.create({ state })).finish();
  (call_rpc as jest.Mock).mockResolvedValueOnce({
    custom: { call: { payload } },
  });
}

const sampleState: StateResponse = {
  endpoints: [
    { index: 1, isUsb: true, bleProfileIndex: 0, value: -1 },
    { index: 2, isUsb: false, bleProfileIndex: 0, value: 2 },
  ],
  osLayers: [
    { os: 0, value: -1 },
    { os: 1, value: 1 },
    { os: 2, value: -1 },
    { os: 3, value: -1 },
    { os: 4, value: -1 },
    { os: 5, value: -1 },
  ],
  activeEndpointIndex: 2,
  currentOs: 1,
  resolvedLayer: 2,
  layerCount: 4,
  osDetectionAvailable: true,
  configurableRange: undefined,
  availableLayers: undefined,
  applicationPending: false,
};

describe("DefaultLayerPanel", () => {
  beforeEach(() => {
    (call_rpc as jest.Mock).mockReset();
  });

  describe("Without subsystem", () => {
    it("shows a warning when the subsystem is not found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );

      expect(
        screen.getByText(
          new RegExp(`Subsystem "${SUBSYSTEM_IDENTIFIER}" not found`, "i")
        )
      ).toBeInTheDocument();
    });
  });

  describe("With subsystem", () => {
    it("can retry an initial state read without reconnecting", async () => {
      (call_rpc as jest.Mock).mockRejectedValueOnce(new Error("Read failed"));
      const app = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });
      render(
        <ZMKAppProvider value={app}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );
      await screen.findByText(/Read failed/);
      const refresh = screen.getByRole("button", { name: /Refresh/ });
      expect(refresh).toBeEnabled();
      expect(
        screen.queryByRole("button", { name: "Retry last change" })
      ).toBeNull();
      mockRpcResponse(sampleState);
      await userEvent.setup().click(refresh);
      await screen.findByLabelText("USB default layer");
      expect(call_rpc).toHaveBeenCalledTimes(2);
      expect((call_rpc as jest.Mock).mock.calls[1]).toEqual(
        (call_rpc as jest.Mock).mock.calls[0]
      );
      expect(screen.queryByText(/Read failed/)).toBeNull();
    });

    it.each([
      [{ minIndex: 1, maxIndex: 2 }, ["-1", "-2", "1", "2"]],
      [{ minIndex: 0, maxIndex: 0 }, ["-1", "-2", "0"]],
      [{ minIndex: 2, maxIndex: 9 }, ["-1", "-2", "2", "3"]],
      [undefined, ["-1", "-2", "0", "1", "2", "3"]],
    ])(
      "offers the advertised range %j in both tables",
      async (range, values) => {
        mockRpcResponse({
          ...sampleState,
          configurableRange: range,
          osLayers: sampleState.osLayers.map((entry) => ({
            ...entry,
            value: -1,
          })),
        });
        const app = createConnectedMockZMKApp({
          subsystems: [SUBSYSTEM_IDENTIFIER],
        });
        render(
          <ZMKAppProvider value={app}>
            <DefaultLayerPanel />
          </ZMKAppProvider>
        );
        const endpoint = await screen.findByLabelText("USB default layer");
        expect(
          within(endpoint)
            .getAllByRole("option")
            .map((option) => (option as HTMLOptionElement).value)
        ).toEqual(values);
        expect(
          within(screen.getByLabelText("Windows default layer"))
            .getAllByRole("option")
            .map((option) => (option as HTMLOptionElement).value)
        ).toEqual(values.filter((value) => value !== "-2"));
      }
    );

    it.each([
      [5, "Unavailable layer 5"],
      [0, "Unavailable layer 0"],
      [-3, "Unsupported value -3"],
    ])(
      "shows stored value %s until a valid replacement succeeds",
      async (value, label) => {
        const stored = {
          ...sampleState,
          layerCount: 6,
          configurableRange: { minIndex: 1, maxIndex: 3 },
          endpoints: [{ index: 1, isUsb: true, bleProfileIndex: 0, value }],
          osLayers: [{ os: 1, value }],
        };
        mockRpcResponse(stored);
        const app = createConnectedMockZMKApp({
          subsystems: [SUBSYSTEM_IDENTIFIER],
        });
        render(
          <ZMKAppProvider value={app}>
            <DefaultLayerPanel />
          </ZMKAppProvider>
        );
        const endpoint = await screen.findByLabelText("USB default layer");
        const os = screen.getByLabelText("Windows default layer");
        for (const select of [endpoint, os]) {
          expect(select).toHaveValue(String(value));
          expect(
            within(select).getByRole("option", { name: label })
          ).toBeDisabled();
        }
        expect(call_rpc).toHaveBeenCalledTimes(1);
        mockRpcResponse({
          ...stored,
          endpoints: [{ ...stored.endpoints[0], value: 1 }],
        });
        await userEvent.setup().selectOptions(endpoint, "Layer 1");
        await waitFor(() => expect(endpoint).toHaveValue("1"));
        expect(
          within(endpoint).queryByRole("option", { name: label })
        ).toBeNull();
        expect(os).toHaveValue(String(value));
        expect(call_rpc).toHaveBeenCalledTimes(2);
      }
    );

    it("retains an unavailable stored value when its replacement fails", async () => {
      mockRpcResponse({
        ...sampleState,
        configurableRange: { minIndex: 0, maxIndex: 1 },
        endpoints: [{ index: 1, isUsb: true, bleProfileIndex: 0, value: 3 }],
      });
      const app = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });
      render(
        <ZMKAppProvider value={app}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );
      const endpoint = await screen.findByLabelText("USB default layer");
      expect(endpoint).toHaveValue("3");
      (call_rpc as jest.Mock).mockRejectedValueOnce(new Error("Write failed"));
      await userEvent.setup().selectOptions(endpoint, "Layer 1");
      await screen.findByText(/Write failed/);
      expect(endpoint).toHaveValue("3");
      expect(
        within(endpoint).getByRole("option", { name: "Unavailable layer 3" })
      ).toBeDisabled();
      expect(call_rpc).toHaveBeenCalledTimes(2);
    });

    it("displays runtime state returned with a failed save and retains the error", async () => {
      mockRpcResponse(sampleState);
      const app = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });
      render(
        <ZMKAppProvider value={app}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );
      const endpoint = await screen.findByLabelText("USB default layer");
      const state = {
        ...sampleState,
        resolvedLayer: 1,
        endpoints: [{ ...sampleState.endpoints[0], value: 1 }],
      };
      (call_rpc as jest.Mock).mockResolvedValueOnce({
        custom: {
          call: {
            payload: Response.encode(
              Response.create({ error: { message: "Save failed", state } })
            ).finish(),
          },
        },
      });
      await userEvent.setup().selectOptions(endpoint, "Layer 1");
      await screen.findByText(/Save failed/);
      expect(endpoint).toHaveValue("1");
      expect(screen.getByText(/Resolved layer:/)).toHaveTextContent(
        "Resolved layer: 1"
      );
      expect(call_rpc).toHaveBeenCalledTimes(2);
      mockRpcResponse(state);
      await userEvent
        .setup()
        .click(screen.getByRole("button", { name: "Retry last change" }));
      await waitFor(() => expect(call_rpc).toHaveBeenCalledTimes(3));
      expect((call_rpc as jest.Mock).mock.calls[2]).toEqual(
        (call_rpc as jest.Mock).mock.calls[1]
      );
      await waitFor(() => expect(screen.queryByText(/Save failed/)).toBeNull());
      expect(endpoint).toHaveValue("1");
      expect(
        screen.queryByRole("button", { name: "Retry last change" })
      ).toBeNull();
    });

    it("fetches and renders endpoint and OS state", async () => {
      mockRpcResponse(sampleState);
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );

      await waitFor(() => {
        expect(screen.getByText(/Current OS:/i)).toBeInTheDocument();
      });

      expect(screen.getByText(/Current OS:/i)).toHaveTextContent("Windows");
      expect(screen.getByText(/Current OS:/i)).toHaveTextContent(
        "Resolved layer: 2"
      );
      expect(screen.getByText("USB")).toBeInTheDocument();
      expect(screen.getByText(/BLE profile 0 \(active\)/)).toBeInTheDocument();

      const usbSelect = screen.getByLabelText(
        "USB default layer"
      ) as HTMLSelectElement;
      expect(usbSelect.value).toBe("-1");

      const bleSelect = screen.getByLabelText(
        "BLE profile 0 default layer"
      ) as HTMLSelectElement;
      expect(bleSelect.value).toBe("2");

      const windowsSelect = screen.getByLabelText(
        "Windows default layer"
      ) as HTMLSelectElement;
      expect(windowsSelect.value).toBe("1");

      expect(screen.getByLabelText("iOS default layer")).toBeInTheDocument();
      expect(
        screen.getByLabelText("Android default layer")
      ).toBeInTheDocument();
    });

    it("sends a set_endpoint_layer request when a connection's layer changes", async () => {
      mockRpcResponse(sampleState);
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );

      const usbSelect = await screen.findByLabelText("USB default layer");

      mockRpcResponse({
        ...sampleState,
        endpoints: [
          { index: 1, isUsb: true, bleProfileIndex: 0, value: 0 },
          sampleState.endpoints[1],
        ],
      });

      const user = userEvent.setup();
      await user.selectOptions(usbSelect, "Layer 0");

      await waitFor(() => {
        expect((usbSelect as HTMLSelectElement).value).toBe("0");
      });

      expect(call_rpc).toHaveBeenCalledTimes(2);
    });

    it("disables the per-OS table when OS detection is unavailable", async () => {
      mockRpcResponse({ ...sampleState, osDetectionAvailable: false });
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <DefaultLayerPanel />
        </ZMKAppProvider>
      );

      const windowsSelect = (await screen.findByLabelText(
        "Windows default layer"
      )) as HTMLSelectElement;
      expect(windowsSelect.disabled).toBe(true);
      expect(
        screen.getByText(/OS detection is not enabled in this firmware build/i)
      ).toBeInTheDocument();
    });
  });
});
