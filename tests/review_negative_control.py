"""Restore one default-layer review defect in an isolated owner source copy."""

import argparse
from pathlib import Path


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise SystemExit("expected exactly one reviewed source boundary")
    return source.replace(before, after)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("owner", type=Path)
    parser.add_argument(
        "fault",
        choices=(
            "default-floor",
            "missing-global",
            "os-change",
            "structural-trigger",
            "direct-apply",
            "unbounded-coalescing",
            "availability-wire",
            "pending-state",
            "web-availability",
            "web-pending",
        ),
    )
    args = parser.parse_args()
    root = args.owner.resolve()
    owner = Path(__file__).resolve().parents[1]
    if root.is_relative_to(owner) or not (root / "src/default_layer.c").is_file():
        raise SystemExit("negative control requires an isolated owner source copy")
    if args.fault.startswith("web-"):
        target = root / "web/src/App.tsx"
    elif args.fault in ("availability-wire", "pending-state"):
        target = root / "src/studio/default_layer_handler.c"
    else:
        target = root / "src/default_layer.c"
    source = target.read_text()
    if args.fault == "default-floor":
        source = replace_once(source, "index >= floor", "true")
    elif args.fault == "missing-global":
        source = replace_once(
            source,
            "/* Never turn a missing stable default into an unrelated layer at zero. */\n"
            "    return ZMK_DEFAULT_LAYER_UNSET;",
            "/* Fault: substitute the unrelated live layer at index zero. */\n"
            "    return 0;",
        )
    elif args.fault == "os-change":
        source = replace_once(
            source, "if (as_zmk_os_changed(eh))", "if (false && as_zmk_os_changed(eh))"
        )
    elif args.fault == "structural-trigger":
        source = replace_once(
            source, "if (notification &&", "if (false && notification &&"
        )
    elif args.fault == "direct-apply":
        source = replace_once(
            source,
            "static void request_application(void) {",
            'static void request_application(void) {\n    apply_current("inline-control");\n    return;',
        )
    elif args.fault == "unbounded-coalescing":
        source = replace_once(
            source, "for (unsigned pass = 0; pass < 4; ++pass)", "for (;;)"
        )
    elif args.fault == "availability-wire":
        source = replace_once(
            source,
            "out->has_available_layers = true;",
            "out->has_available_layers = false;",
        )
    elif args.fault == "pending-state":
        source = replace_once(
            source,
            "out->application_pending = zmk_default_layer_application_pending();",
            "out->application_pending = false;",
        )
    elif args.fault == "web-availability":
        source = replace_once(
            source,
            "? state.availableLayers.indices",
            "? Array.from({ length: Math.max(0, max - min + 1) }, (_, i) => min + i)",
        )
    else:
        source = replace_once(
            source,
            "{state.applicationPending && (",
            "{false && state.applicationPending && (",
        )
    target.write_text(source)
    print("Restored only the selected reviewed defect in isolated owner:", args.fault)


if __name__ == "__main__":
    main()
