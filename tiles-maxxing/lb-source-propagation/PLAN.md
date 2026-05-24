# LB Source Propagation Implementation Plan

Status: execution plan, not implementation.
Date: 2026-05-24.
Branch context: `ttc/lb-source-propagation`.

## Objective

Implement the first-plus-rolling-last lower-bound source propagation redesign.

The target hot campaign state is:

```text
first_source_artifact
current_live_handoff
previous_live_handoff
active_last_band_transfer_summary
terminal_summary_if_dead
```

The implementation must make W-scale execution possible by removing historical
component inventory from the hot continuation path. It must not promote any
diagnostic result to `SOURCE_DEAD_CERT`.

## Current Code Audit

Current core state is still inventory-carrying:

- `SeparatorState` has `carry_atoms`, `component_partition`,
  `source_bit_per_component`, and `component_inventory`.
- `process_band` merges incoming `component_inventory`, emits outgoing
  inventories, and writes inventories into `LB_SOURCE_CARRY_MANIFEST_V1`.
- `source_tileop_port_runner` materializes full TileOp-port bands and calls
  `process_band`.
- `source_tileop_port_runner` has a duplicated v1 manifest parser even though
  `lb_source::read_carry_manifest` exists.

Useful compatibility hook:

- `process_band` already tolerates incoming separators with empty
  `component_inventory` by falling back to the partition. This makes
  `LiveHandoff` adapters low-risk for reachability.

Main semantic trap:

- legacy `process_band` accepts fresh `certified_source` atoms even when an
  incoming handoff exists. Live continuation mode must reject fresh source
  seeding when incoming state is present.

Hard compatibility rule:

- `LB_SOURCE_CARRY_MANIFEST_V1` byte behavior must not break.

## Authority

Math/protocol:

- `methodology/source-propagation-band-stitching.md`
- `docs/lb-handoff-redesign.md`

Current implementation:

- `include/lb_source/source_propagation.h`
- `src/source_propagation.cpp`
- `src/tileop_port_graph.cpp`
- `apps/source_tileop_port_runner.cpp`
- `tests/test_source_propagation.cpp`
- `tests/test_tileop_port_graph.cpp`
- `tests/test_tileop_port_runner_resume.sh`
- `CMakeLists.txt`

Remote validation harness:

- `scripts/vast_sidecar_smoke_guard.sh`
- `scripts/check_phase1_local_gates.sh`
- `scripts/check_phase1_parity_gate.sh`
- `scripts/check_tileop_port_wide_band_equivalence.sh`

## Execution Model

Implementation work should be run by native Codex subagents using GPT-5.5 high.
The coordinator owns phase gates, merge order, synthesis, and acceptance.

Worker rules:

- one phase per worker unless the coordinator explicitly fuses phases;
- disjoint write ownership whenever possible;
- every worker reports changed files, commands run, and residual risks;
- no worker may change CUDA kernels, static-annulus verdict semantics, or
  verification authority unless its phase explicitly owns that surface;
- every phase ends with a commit-sized diff, not a sprawling mixed rewrite.

Validators:

- local validators run after every implementation phase;
- remote validators use Vast only after local gates pass and the user explicitly
  authorizes paid runtime;
- remote offers must be below `$0.75/h`, and instances must use
  `--destroy-on-exit` unless the user says otherwise.

Auditors:

- audit against the math spec and the redesign docs;
- reject progress-row-only terminal evidence;
- reject source revival/reseeding;
- reject coordinate claims backed by TileOp port atoms only;
- keep all outputs non-claim unless independent proof gates pass.

## Phases

### Phase 0: Baseline Gate

Owner: validation-only.

Files: none.

Done means:

- worktree state recorded;
- existing sidecar and verification gates pass, or failures are captured before
  edits begin.

Commands:

```bash
git status --short --untracked-files=all
tiles-maxxing/lb-source-propagation/scripts/check_phase1_parity_gate.sh --repo "$PWD"
tiles-maxxing/lb-source-propagation/scripts/check_phase1_local_gates.sh --repo "$PWD" --k-sq 26
```

Stop if:

- baseline cannot be reproduced;
- there are unrelated dirty files in implementation surfaces;
- existing v1 manifest/resume/parity tests fail without explanation.

### Phase 1: Manifest Parser Centralization

Owner: manifest worker.

Files:

- `apps/source_tileop_port_runner.cpp`
- focused tests if needed.

Work:

- replace the duplicated runner v1 manifest parser with
  `lb_source::read_carry_manifest`;
- preserve existing behavior and diagnostics where possible;
- do not change v1 output.

Done means:

- v1 roundtrip tests are unchanged;
- runner resume tests still pass;
- no schema changes.

### Phase 2: Live Handoff Core

Owner: core state worker.

Files:

- `include/lb_source/source_propagation.h`
- `src/source_propagation.cpp`
- `tests/test_source_propagation.cpp`

Work:

- add `LiveSeparator`;
- add adapters:

```text
live_separator_from_separator
separator_from_live_separator
canonicalize_live_separator
validate_live_separator
```

- add `LiveHandoffV1` / envelope structs only as much as needed for local
  tests;
- add `LiveProcessResult`;
- add `process_band_live(...)` as an additive public API;
- keep `process_band(...)` legacy behavior intact.

Done means:

- live and legacy agree on outgoing `carry_atoms`, `component_partition`, and
  `source_bit_per_component`;
- live path does not grow historical inventory;
- incoming live handoff plus fresh `certified_source` is rejected;
- `LB_SOURCE_CARRY_MANIFEST_V1` byte tests still pass.

### Phase 3: Live Manifest Boundary

Owner: manifest worker.

Files:

- `include/lb_source/source_propagation.h`
- `src/source_propagation.cpp`
- `tests/test_source_propagation.cpp`

Work:

- add opt-in `LB_SOURCE_LIVE_HANDOFF_V1` reader/writer;
- keep v1 carry manifest separate and byte-compatible;
- canonicalize and validate live handoffs;
- bind `k_sq`, cut radius, carry width, source identity, oracle/build identity,
  schedule digest, and overflow summary.

Done means:

- v1 and live-handoff roundtrips both pass;
- stale cut radius, wrong `K`, wrong carry width, duplicate atom, missing
  partition coverage, and unstable atom ids are rejected.

### Phase 4: Last-Band Summary Core

Owner: summary worker.

Files:

- `include/lb_source/source_propagation.h`
- `src/source_propagation.cpp`
- `tests/test_source_propagation.cpp`

Work:

- add `LastBandReachabilitySummaryV1`;
- add `apply_last_band_summary(...)`;
- keep it source-free: source bits are applied only from incoming
  `LiveHandoffV1`;
- split coordinate maxima from port/support diagnostics;
- aggregate bridge safety after applying incoming source bits.

Done means:

- `previous_live_handoff + active_summary` reproduces terminal/death state on
  synthetic fixtures;
- neutral carry welding into source inside the last band is handled;
- port overhang can keep source alive but never becomes coordinate max evidence;
- a progress row alone is insufficient terminal evidence.

### Phase 5: Materialized First+Rolling-Last Runner

Owner: runner worker.

Files:

- `apps/source_tileop_port_runner.cpp`
- `tests/test_tileop_port_runner_resume.sh`
- `CMakeLists.txt`

Work:

- keep current materialized TileOp-port conversion;
- add optional live output artifacts;
- maintain runner hot state:

```text
first_source_artifact
previous_live_handoff
current_live_handoff
active_band_summary
terminal_summary_if_dead
```

- on death, persist/report `previous_live_handoff + active_band_summary`;
- reject terminal evidence that is only a progress row.

Done means:

- existing materialized runner tests and resume tests pass;
- full run and chunked/resumed run produce equivalent live handoffs;
- terminal/dead artifact binds previous handoff hash and active summary hash;
- output remains `DIAGNOSTIC_NON_CLAIM`.

### Phase 6: Independent Rolling-Last Verification

Owner: verifier worker.

Files:

- `verification/src/source_prop_oracle.cpp`
- `verification/schemas/*source*`
- `verification/fixtures/source_prop/**`
- `verification/test_source_prop_schema_contract.py`

Work:

- add fixtures for:
  - progress-row-only rejection;
  - neutral carry welding into source and owning furthest coordinate max;
  - previous-handoff plus summary replay;
  - shared-cut coarse/fine equivalence;
  - coordinate-vs-port max separation;
  - no fresh `geo_I` reseeding.

Done means:

- independent verifier reproduces same terminal state and coordinate furthest
  candidate as all-band replay on hostile fixtures;
- verifier rejects stale envelope, missing previous handoff, malformed summary,
  source revival, and unsafe bridge cases.

### Phase 7: Accumulator And Bridge Claim Barriers

Owner: proof-audit worker.

Files:

- `verification/src/source_dead_cert_check.cpp`
- `verification/src/source_dead_gap_check.cpp`
- relevant fixtures and schemas.

Work:

- ensure producer booleans cannot create claim-grade inventory acceptance;
- require independently checked accumulator/replay evidence for claim-grade
  terminal inventory;
- check bridge safety after closure, because neutral classes can become source;
- preserve summary-only non-claim behavior.

Done means:

- hostile accumulator fixtures fail;
- summary-only artifacts stay non-claim;
- `SOURCE_DEAD_CERT` remains blocked unless path, bridge, negative guard, BZ,
  and accumulator obligations all pass.

### Phase 8: Streaming Runner Prototype

Owner: streaming worker.

Files:

- new `apps/source_tileop_port_stream_runner.cpp`;
- possible new streaming helpers beside `tileop_port_graph`;
- minimal tests and CMake additions.

Work:

- stream TileOps into a bounded-memory port accumulator;
- release raw TileOps after consumption;
- fuse output into rolling live state;
- do not persist raw slabs by default;
- keep current materialized runner as correctness oracle.

Done means:

- small materialized-vs-streaming outputs match;
- synthetic long-chain probe shows memory bounded by active streaming window,
  two live frontiers, and one active summary;
- no W-scale claim is made yet.

### Phase 9: Remote Vast Validation

Owner: remote validator.

Precondition:

- local gates pass;
- user explicitly authorizes paid runtime;
- selected offer is below `$0.75/h`.

Dry-run market check:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --max-dph 0.75 \
  --max-budget 1.50 \
  --k-sq 26 \
  --offer-wait-seconds 900 \
  --offer-poll-seconds 30 \
  --failure-ledger tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-failures.tsv
```

Paid smoke, only after authorization:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --execute \
  --run-remote-smoke \
  --destroy-on-exit \
  --max-dph 0.75 \
  --max-budget 1.50 \
  --k-sq 26 \
  --offer-wait-seconds 900 \
  --offer-poll-seconds 30 \
  --max-create-attempts 3 \
  --wait-ssh-seconds 600 \
  --ssh-poll-seconds 10 \
  --failure-ledger tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-failures.tsv \
  --pull-dir tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull
```

Bounded K26 timing/profile probe, only after smoke passes:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --execute \
  --run-k26-timing-probe \
  --destroy-on-exit \
  --max-dph 0.75 \
  --max-budget 3.25 \
  --k-sq 26 \
  --k26-timing-chunk-bands 1 \
  --k26-tileop-threads 0 \
  --offer-wait-seconds 900 \
  --offer-poll-seconds 30 \
  --max-create-attempts 3 \
  --wait-ssh-seconds 600 \
  --ssh-poll-seconds 10 \
  --failure-ledger tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-failures.tsv \
  --pull-dir tiles-maxxing/lb-source-propagation/artifacts/vast-k26-timing-pull
```

Done means:

- artifacts are pulled before destroy;
- artifact checkers pass;
- head/branch/K_SQ/build identity are recorded;
- all outputs remain non-claim unless independent proof gates explicitly pass.

## Validator Gates

Local:

```bash
cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp -DK_SQ=36
cmake --build /tmp/gm-lbsp -j
ctest --test-dir /tmp/gm-lbsp --output-on-failure

cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp-k26 -DK_SQ=26
cmake --build /tmp/gm-lbsp-k26 -j
ctest --test-dir /tmp/gm-lbsp-k26 --output-on-failure
```

Parity:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_parity_gate.sh --repo "$PWD"
tiles-maxxing/lb-source-propagation/scripts/check_tileop_port_wide_band_equivalence.sh \
  /tmp/gm-lbsp-k26/source_tileop_port_runner \
  --r-start 8192 \
  --segments 6 \
  --segment-width 8192 \
  --out-dir /tmp/gm-lbsp-wide-equivalence-k26
```

Independent verification:

```bash
cmake -S verification -B /tmp/gm-verify
cmake --build /tmp/gm-verify -j
ctest --test-dir /tmp/gm-verify --output-on-failure
```

Full local gate:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_local_gates.sh \
  --repo "$PWD" \
  --k-sq 26
```

## Hard Stop Conditions

Stop and repair before proceeding if:

- v1 manifest byte tests change unintentionally;
- carry partition/source bits differ between live and legacy connectivity;
- live mode accepts fresh source seeding with incoming handoff;
- neutral carry can be dropped without failing a test;
- port support norm appears as coordinate furthest evidence;
- a progress row alone is accepted as terminal evidence;
- remote validation produces missing/stale head, branch, build, or K_SQ
  provenance;
- any non-claim artifact contains `SOURCE_DEAD_CERT_PASS`, `MOAT_PROOF_PASS`,
  or `SPAN_PROOF_PASS`;
- memory still scales with historical component inventory after live mode is in
  the hot path.

## Suggested `/goal`

Objective:

```text
Implement first-plus-rolling-last LB source propagation through Phase 5,
preserving v1 compatibility, proving live frontier parity locally, and
producing a materialized diagnostic death/summary artifact shape. Do not run
paid Vast validation unless explicitly authorized during the goal.
```

Initial stop gate:

```text
Phase 0 through Phase 5 pass locally, work is committed in scoped commits, and
PLAN.md is updated with any deviations.
```

Second goal:

```text
Add independent rolling-last verification and claim barriers, then run local
and Vast validation gates.
```
