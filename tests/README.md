# Owner tests

Run the complete native/board matrix inside a prepared Linux container and the
explicitly resolved west workspace:

```sh
python3 test.py
```

The root entry dispatches assertions owned by `tests/test_owner.py`, output
admission checks in `tests/test_runner_contract.py`, and native-result controls
in `tests/test_native_verifier.py`. The original build/configuration assertions
and native snapshots remain required. `OWNER_TEST_OUTPUT` selects a fresh
absolute output directory. An existing directory fails without deleting its
contents; the default is a fresh retained directory outside dependencies.
Commands retain combined ordered output, actual exits, UTC times and elapsed
nanoseconds under `commands/`.

The test graph pins DYA ZMK, Zephyr and the west command extension explicitly.
Resolve all transitive dependencies and record selected local module overrides
before execution. Ordinary runs must not fetch dependencies or update caches.
This custom Studio RPC module supports DYA; stock ZMK is NOT APPLICABLE.

`tests/native_verifier.py` runs each produced native binary three times in
independent empty initial-flash directories. It records the binary and fixture
hashes, raw output and actual process exit before comparing unchanged ordered
snapshots. `required_hooks.json` maps enabled test configurations to checked
initialization hooks. Missing requested configuration, missing hooks, missing or
nonzero completion, logged assertion failures and fatal output all fail. The
eleven verifier controls compile real synthetic ELF programs; these controls
verify result admission and do not execute ZMK firmware.

Test scenarios and private static-handler fixtures live under `tests/support/`.
Guarded production includes expose those scenarios only in test builds; public
samples and state-only diagnostics retain their production roles.

Run the locked Web schema generation, complete Jest selection, build and lint
from `web/` as documented by its package scripts. Keep generated output and
source identities in the evidence. Controlled DOM/native/compile checks do not
establish browser transport, physical USB/BLE/split, timing, power or reset
acceptance.

The `application` native fixture uses six layers and the configurable range
1..3. It checks both setter and generic-storage validation, retains unrelated
function layers, injects a real re-entrant mapping update during deactivation,
and verifies cleanup/fallback after layer reordering. A separate kernel thread
also updates the mapping while the first thread is applying a layer change.
Both checked completions and six ordered markers are required in each of the
three native runs. The original scenarios and assertions remain required.

The Web panel checks partial, single-layer and legacy unadvertised ranges in
both mapping tables. The RPC lock fixture also checks that GetState advertises
the configured range, including the zero-valued 0..0 sub-message.
Unavailable saved values remain displayed as disabled options. Web cases cover
both tables, successful replacement and refusal without losing the stored value.

Test-only link wrapping injects `-EIO` into real settings saves for the module's
namespace. The application fixture checks endpoint and OS runtime reconciliation
without masking the error; Studio checks a real nanopb error-state round trip.
Web cases check returned runtime state and reject retired error snapshots after
connection/subsystem replacement. The fault wrapper is excluded from production.

`tests/test_review_dependencies.py` validates the actual imported fork manifest
and refusal of missing/mismatched remote refs. These offline controls do not
fetch; `tools/verify_review_dependencies.py` checks the real named branch tips
only during explicit environment preparation, as used by the CI west-init action.

The owner command boundary limits `zmk-build` to one target at a time by
default. `OWNER_TEST_BUILD_PARALLELISM` selects an explicit positive target
count; account for compiler jobs and concurrent owners in the global budget.
`tests/test_build_budget.py` checks default/explicit forwarding and rejects
invalid budgets before launching west. The canonical owner entry includes
these controls without dropping any native or board scenario.

Native result admission requires every `native_sim.keymap` fixture to have a
matching binary. `required_hooks.json` names the expected checked initialization
hooks and required feature/role configuration. Fixture-bound declarations also
reject missing configuration when both the request and build omit it; excluded
roles cannot replace the requested role. The verifier retains all three raw
runs before comparing unchanged ordered snapshots and rejects logged failures,
fatal output and missing/nonzero completions. `tests/test_native_verifier.py`
owns eleven real-ELF admission controls, including a deliberately missing
native scenario, wrong-role and unrelated-fixture controls.

`bash tests/run-web.sh` is the canonical schema-generation/Web test entry.
It runs all configured Jest scenarios in one worker process. The pre-commit
Jest hook dispatches this same owner entry so nested workers do not multiply
the global container budget. Build and lint retain their separate package entries.

## Web connection ownership

`tests/web/lifecycle.spec.tsx` retains six provider/deferred-response cases for
connection and subsystem-index replacement, immediate mapping retirement, late
success/error and independent loading completion. The original Web scenarios
remain selected. Retired requests cannot replace the current state. These are
controlled SDK/DOM observations; physical transport remains NOT RUN.

## Ordering, dispatch and application queue regressions

The additional `review` native fixture is selected by the same canonical
`python3 test.py` entry. All original native scenarios, six application markers,
four board configurations and ordered snapshots remain unchanged. The review
fixture has its own checked completion and four independently expected markers;
the exact native binary runs three times with independent initial state.

The fixture edits the actual core ordering and emits the same unsaved-change
notification as core Studio. A moved global default rejects lower indices at
both setters and generic-storage resolution; real position/keycode dispatch
distinguishes the stable layer IDs with synthetic A/B/C/D bindings. OS fallback,
the current global index outside the configured range, an authoritative empty
availability message, and `DF_INC` skipping unavailable positions are covered.
Reordering follows the documented numeric position, retires the previous stable
ID and dispatches the newly mapped layer. Repeated removal retires applied IDs,
preserves an invalid stored number, reports the live count and handles an
interior invalid slot separately from live later indices. Restoration reconciles
that same saved index. Raw nanopb request/response round trips check optional
presence and exact availability lists while Studio remains locked for GetState.

The kernel boundary parks actual key work on the system queue. A distinct thread
performs a generic memory write or a direct setter with an injected save error.
Storage may change immediately, but layer events must remain on the system queue.
Release runs real position events and an `&mo` press/release before queued
application; temporary/base state and locks must remain coherent. A real GetState
inside a layer event must advertise the in-flight operation as pending. Every
controlled wait releases its gate on failure as well, retaining an assertion
instead of relying on a timeout. A finite 20-update reentrant stream additionally
requires ordinary queued key work to run before the stream ends; unbounded
coalescing fails that assertion without preventing completion.

These checks use the selected production core, event manager, behavior and
settings implementations. Test-only save faults, queue gates and notification
construction are controlled boundaries. They establish neither physical scan/
transport timing nor atomicity of unrelated core structural edits. No core source
or dependency pin is changed by these repairs, and no new Custom Settings API is
required. An explicit reviewed working dependency override may be selected for
integration testing; record its source identity separately from the fixed pin.

`tests/web/layerAvailability.spec.tsx` covers holes, removed saved choices in both
tables, an authoritative empty list, successful replacement/clearing, moved global
default positions and pending-versus-completed display. All legacy absent-range,
single-layer, error/retry and connection-lifetime cases remain selected.

Apply each fault only to a fresh isolated owner copy:

```sh
python3 tests/review_negative_control.py ISOLATED_OWNER FAULT
```

For `default-floor`, `structural-trigger`, `direct-apply`,
`unbounded-coalescing`, `availability-wire` and `pending-state`, build the same
`review` native fixture and execute the unchanged assertions three times from
independent empty initial-flash directories, retaining actual exits, ordered
output and the checked result. They must reject, respectively, the lower-floor,
reorder, foreign-thread application, queued-key fairness, authoritative availability
and in-flight state assertions. Run `web-availability` and `web-pending` through
`bash tests/run-web.sh`; the new DOM assertions must fail. Source selectors must
match exactly once. Compilation/setup errors, missing generated types, collection
failures or timeouts are not accepted as killed controls. The original native
admission, build-budget and review-dependency controls remain mandatory.
