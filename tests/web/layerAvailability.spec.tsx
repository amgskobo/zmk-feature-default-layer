import {
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { DefaultLayerPanel, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import {
  Request,
  Response,
} from "../../web/src/proto/cormoran/default-layer/default_layer";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));
function reply(indices: number[], value: number, pending = false) {
  return {
    custom: {
      call: {
        payload: Response.encode(
          Response.create({
            state: {
              layerCount: 3,
              configurableRange: { minIndex: 1, maxIndex: 3 },
              availableLayers: { indices },
              applicationPending: pending,
              resolvedLayer: value,
              endpoints: [{ index: 1, isUsb: true, value }],
              osLayers: [{ os: 1, value }],
              osDetectionAvailable: true,
            },
          }),
        ).finish(),
      },
    },
  };
}
function panel() {
  const app = createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] });
  return (
    <ZMKAppProvider value={app}>
      <DefaultLayerPanel />
    </ZMKAppProvider>
  );
}
function values(select: HTMLElement) {
  return within(select)
    .getAllByRole("option")
    .map((option) => (option as HTMLOptionElement).value);
}
beforeEach(() => jest.resetAllMocks());

it("uses live indices with holes and retains an invalid stored choice in both tables", async () => {
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([1, 3], 2));
  render(panel());
  const endpoint = await screen.findByLabelText("USB default layer");
  const os = screen.getByLabelText("Windows default layer");
  expect(values(endpoint)).toEqual(["-1", "-2", "1", "3", "2"]);
  expect(values(os)).toEqual(["-1", "1", "3", "2"]);
  for (const select of [endpoint, os]) {
    expect(select).toHaveValue("2");
    expect(
      within(select).getByRole("option", { name: "Unavailable layer 2" }),
    ).toBeDisabled();
    expect(
      within(select).getByRole("option", { name: "Layer 3" }),
    ).toBeEnabled();
  }
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([1, 3], 3));
  fireEvent.change(endpoint, { target: { value: "3" } });
  await waitFor(() => expect(endpoint).toHaveValue("3"));
  const request = Request.decode(
    (call_rpc as jest.Mock).mock.calls[1][1].custom.call.payload,
  );
  expect(request.setEndpointLayer?.value).toBe(3);
  expect(
    within(endpoint).queryByRole("option", { name: "Unavailable layer 2" }),
  ).not.toBeInTheDocument();
});

it("treats an empty availability message as authoritative and permits clearing the saved value", async () => {
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([], 2));
  render(panel());
  const endpoint = await screen.findByLabelText("USB default layer");
  expect(values(endpoint)).toEqual(["-1", "-2", "2"]);
  expect(values(screen.getByLabelText("Windows default layer"))).toEqual([
    "-1",
    "2",
  ]);
  expect(
    within(endpoint).getByRole("option", { name: "Unavailable layer 2" }),
  ).toBeDisabled();
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([], -1));
  fireEvent.change(endpoint, { target: { value: "-1" } });
  await waitFor(() => expect(endpoint).toHaveValue("-1"));
  expect(values(endpoint)).toEqual(["-1", "-2"]);
  const request = Request.decode(
    (call_rpc as jest.Mock).mock.calls[1][1].custom.call.payload,
  );
  expect(request.setEndpointLayer?.value).toBe(-1);
});

it("excludes positions below the moved global default and makes pending application explicit", async () => {
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([2, 3], 1, true));
  render(panel());
  const endpoint = await screen.findByLabelText("USB default layer");
  expect(values(endpoint)).toEqual(["-1", "-2", "2", "3", "1"]);
  expect(
    within(endpoint).getByRole("option", { name: "Unavailable layer 1" }),
  ).toBeDisabled();
  expect(
    within(endpoint).queryByRole("option", { name: "Layer 1" }),
  ).not.toBeInTheDocument();
  expect(screen.getByRole("status")).toHaveTextContent(
    "Layer application is pending",
  );
  (call_rpc as jest.Mock).mockResolvedValueOnce(reply([2, 3], 1));
  fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
  await waitFor(() =>
    expect(screen.queryByRole("status")).not.toBeInTheDocument(),
  );
  expect(endpoint).toHaveValue("1");
});
