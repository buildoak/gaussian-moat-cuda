# LB Source Propagation Optimization Plan

Status: historical/conditional execution runbook, non-claim.
Date: 2026-05-24.
Branch context: `ttc/lb-source-propagation`.
Current path:
`tiles-maxxing/lb-source-propagation/docs/archive/lb-source-propagation-optimization-plan-20260524.md`.

This file records the overnight 4090 optimization plan and acceptance shape. It
does not certify a lower-bound result and does not grant standing permission to
rent remote compute. Use it only when paid remote execution is explicitly
authorized in the current session.

## Objective

Make the sqrt(26) LB source-continuation path fast and bounded enough to run
serious composed campaigns, while preserving the exact live frontier protocol.

The immediate optimization target is not a full proof and not a W-scale claim.
The target is:

```text
correctness gates pass
producer and telemetry boundaries are explicit
CPU materialized baseline is measured
CUDA TileOp ingestion has byte-parity gates
R~60M and R~400M have safe non-claim preflights/probes
```

The work must not promote any diagnostic result to a source-death proof.

## Current Ground Truth

Implemented and locally/remote-smoke verified:

- `LiveHandoffV1` hot continuation state.
- `process_band_live(...)`.
- `source_tileop_port_runner` live handoff input/output.
- first-plus-rolling-last runner state.
- materialized last-band summaries and diagnostic death output shape.
- K26 local sidecar/verification gates and remote diagnostic gates.

Observed remote diagnostic evidence:

- wide-vs-stitched K26 live handoff equivalence passed for `8192 -> 49152`;
  final live handoff was byte-identical.
- continuation `49152 -> 90112` passed as diagnostic resume.
- high-radius diagnostic profiles passed up to `999424 -> 1007616`.
- K26 timing probe completed the first 5 continuation bands in `134s` in the
  bundle harness, then rejected the `1800s` runtime budget projection.
- largest high-radius single-band profile at `999424 -> 1007616`:
  `102483` tiles, `8250164` port atoms, `7501014` edges, `256322ms` total,
  with `219141ms` in TileOp build.

Important limitation:

The current runner is still a materialized CPU diagnostic runner. It builds
the full `coords` vector, full `tileops` vector, full port graph, and then
calls live source processing. This is not the W-scale substrate.

## Authority And Paths

Use repo-root-relative paths. Do not create root-level `docs/`, `artifacts/`,
`results/`, or old campaign authority surfaces.

Active paths for this plan:

- `tiles-maxxing/lb-source-propagation/docs/archive/lb-source-propagation-implementation-plan-20260524.md`
- `tiles-maxxing/lb-source-propagation/docs/lb-handoff-redesign.md`
- `tiles-maxxing/lb-source-propagation/apps/source_tileop_port_runner.cpp`
- `tiles-maxxing/lb-source-propagation/include/lb_source/`
- `tiles-maxxing/lb-source-propagation/src/`
- `tiles-maxxing/lb-source-propagation/scripts/`
- `tiles-maxxing/lb-source-propagation/artifacts/`
- `tiles-maxxing/cpp-campaign-v2/`
- `tiles-maxxing/cuda-campaign-v2-sqrt-36/`
- `verification/`
- `methodology/source-propagation-band-stitching.md`

## TTC Frame

Objective:

```text
Reduce LB source-propagation runtime/RSS and prepare CUDA-backed continuation
without weakening live-handoff, source, bridge, and terminal-summary semantics.
```

Gate:

```text
Fresh local correctness gates, wide-vs-stitched byte equivalence, no source
revival, non-claim artifact labeling, structured timings/RSS, and explicit
R~60M/R~400M non-claim gates.
```

Budget:

```text
12h overnight 4090 benchmarking/profiling campaign.
One primary Vast RTX 4090 instance below the price cap.
Expected spend at $0.75/h is <= $9 for 12h; hard cap should be set in the
goal/runbook, normally $12 including startup slack.
Local work is a launch gate, not the main payload.
Remote benchmarking/profiling is the main payload.
5-8 focused workers maximum for setup, monitoring, analysis, and audit.
No overlapping writes unless ownership is disjoint.
```

Verifier:

```text
CMake/CTest, independent verification CTest, wide-vs-stitched handoff
equivalence, artifact checkers, progress JSONL timing parser, RSS capture,
claim-token scan, and remote CPU/CUDA kind binding.
```

Candidate shape:

```text
Small branch-sized changes with one optimization axis each:
correctness guard, harness, producer boundary, snapshot replay, CUDA producer,
streaming port graph slice, or bounded high-radius probes.
```

Selection rule:

```text
Correctness beats speed. Accept the smallest diff that passes all gates and
either unlocks a required boundary or improves a fixed benchmark by >=15%
runtime or >=25% RSS/materialization pressure without semantic drift.
```

Stop rule:

```text
Stop on source revival, stale lineage acceptance, final-live byte mismatch
without a justified schema change, missing non-claim label, GPU validation
reported without CUDA execution, materialized large-R blowup, or repeated
candidate failures with the same root cause.
```

Report fields:

```text
changed paths, commands, artifact dir, timings, RSS, hashes, equivalence
status, rejected candidates, accepted candidate, residual risks.
```

## Overnight 4090 Campaign Contract

This is the intended full-scale campaign mode for a sleeping-user run. Setting
the corresponding `/goal` means the coordinator is authorized to rent one Vast
RTX 4090 instance under the declared cap, run the campaign unattended, harvest
artifacts, and clean up the instance according to the cleanup gate.

The campaign has three layers:

1. launch gate on the local machine;
2. remote 4090 benchmark/profiling loop;
3. harvest, acceptance pack, and cleanup.

### Authorization Boundary

Allowed under the overnight goal:

- rent one RTX 4090 instance if the price is `<= $0.75/h`;
- use up to 12h wall time on that instance;
- run builds, tests, benchmarks, profilers, and monitoring on that instance;
- pull artifacts repeatedly to the local repo artifact directory;
- destroy the instance after successful harvest verification.

Not allowed without a new user decision:

- rent more than one paid instance;
- exceed the declared price or spend cap;
- push branches, publish results, or claim a proof result;
- run a destructive cleanup before artifacts have been pulled or the instance
  has been stopped for rescue.

Cleanup rule:

```text
If final artifact harvest verifies, destroy the instance.
If harvest fails but SSH still works, retry harvest.
If SSH or harvest fails repeatedly, stop the instance to halt burn and report
the blocked recovery state instead of destroying unrecovered artifacts.
```

### 12h Remote Timeline

The local launch gate should be less than 45 minutes. The remote instance should
then keep running benchmarks/profiles until the 12h wall cap, until all gates
finish, or until a hard stop condition fires.

| Window | Work | Evidence |
|---:|---|---|
| `0:00-0:45` | local path/status preflight, build command prep, Vast offer scan | local preflight log, chosen offer or no-offer block |
| `0:45-1:45` | provision 4090, deploy repo, build K26 LB sidecar, build verification, build CUDA campaign if available | environment, build logs, `nvidia-smi`, full SHA |
| `1:45-2:45` | correctness smoke: sidecar CTest, verification CTest, wide-vs-stitched equivalence | CTest logs, equivalence artifact hashes |
| `2:45-4:30` | baseline LB materialized profiles: K26 prefix/continuation probe, resume probe, high-radius `~1M` profile | progress JSONL, timing/RSS, runtime projection |
| `4:30-6:00` | exact high-radius probes around `R=60M` and `R=400M`: count/sample if implemented, otherwise thin real materialized probes only | R60/R400 artifact dirs, RSS, overflow/counter checks |
| `6:00-8:00` | CUDA campaign profiling and CPU/CUDA TileOp parity where available | CUDA build logs, parity result, CUDA timing, GPU utilization |
| `8:00-10:30` | sweep matrix: threads, band widths, chunk sizes, and K26 runtime-budget projections | comparison table, best config, rejected configs |
| `10:30-11:30` | long K26 continuation attempt under best safe config, with checkpoints and live handoff outputs | chunk ledger, budget check, live handoff hashes |
| `11:30-12:00` | harvest, artifact hash ledger, local verification of pulled artifacts, instance cleanup | acceptance pack, cleanup status |

The runner must emit and pull artifacts incrementally after each major window.
Do not wait until hour 12 to copy the only evidence.

### Remote Benchmark Matrix

The overnight run should prefer breadth of trustworthy measurements over one
fragile mega-run.

Required rows:

- local-then-remote sidecar CTest and independent verification CTest;
- K26 wide-vs-stitched equivalence at `8192 -> 49152`;
- K26 resume continuation `49152 -> 90112` or current equivalent;
- K26 timing probe with runtime-budget checker;
- high-radius materialized profile near `R=999424`;
- R~60M thin real probe:

```bash
source_tileop_port_runner \
  --r-start 60000000 --r-final 60000128 --band-width 128 \
  --schedule-radii 60000000,60000128 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out r60-w128.progress.jsonl \
  > r60-w128.json
```

- R~400M thin real probe:

```bash
source_tileop_port_runner \
  --r-start 400000000 --r-final 400000016 --band-width 16 \
  --schedule-radii 400000000,400000016 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out r400-w16.progress.jsonl \
  > r400-w16.json
```

- if preflight/sample tools are implemented during the run, count-only and
  sampled TileOp pressure probes at full `8192` width for both `R=60M` and
  `R=400M`;
- CUDA TileOp/campaign profiling and CPU/CUDA byte parity where the CUDA build
  surface supports it;
- thread sweep, at least `16`, `32`, and auto threads for current CPU sidecar;
- band/chunk sweep for K26 continuation, at least current default, smaller
  chunk, and one wider option if memory/RSS permits.

Every row must declare:

```text
proof_status
remote_kind: remote_cpu_sidecar or cuda_kernel
k_sq
r_start/r_final
band_width or schedule_radii
source mode
tileop source
tile count
port atoms
edges
source carry/frontier counts
tileop_overflows
unsafe bridge counters
wall time
phase timings
max RSS
live handoff hash when present
```

### Overnight Stop Conditions

Stop the remote campaign early, harvest, and clean up if:

- price cap is exceeded or the wrong instance type is provisioned;
- sidecar or verification smoke fails on a fresh remote build;
- live handoff equivalence fails;
- a run uses fresh source seeding after a live handoff;
- any artifact claims proof status beyond diagnostic/non-claim;
- RSS exceeds the safe host cap or the process OOMs twice;
- R60/R400 thin probes exceed their planned port caps by more than 25%;
- CUDA validation is requested but only CPU sidecar code is running;
- the instance becomes unreachable after repeated SSH retries.

### Overnight Acceptance Pack

The sleeping-run is successful if it returns with:

```text
artifacts/lb-opt-YYYYMMDD-4090-overnight/
  remote-environment.txt
  vast-instance.json
  build/
  correctness/
  k26-baseline/
  high-radius/
  r60-r400/
  cuda-profile/
  sweep-matrix/
  k26-long-run/
  artifact-ledger.sha256
  campaign-summary.json
  campaign-summary.md
  cleanup-status.txt
```

Minimum acceptance criteria:

- artifacts are pulled and hashed;
- remote kind is explicit for each row;
- K26 correctness gates are reported pass/fail with logs;
- R60/R400 probes ran or are explicitly blocked with a reason;
- at least one K26 runtime-budget projection is produced from remote data;
- instance cleanup status is recorded;
- no diagnostic output is described as proof.

## Work Breakdown

### Phase 0: Path And Baseline Lock - 0.5h

Owner: coordinator or validation worker.

Work:

- confirm branch, full SHA, dirty state, and ignored generated outputs;
- confirm all paths are under the active surfaces above;
- parse existing livecut and K26 timing artifacts into a small baseline note.

Gate:

```bash
git status --short --untracked-files=all
git rev-parse --abbrev-ref HEAD
git rev-parse HEAD
test -d tiles-maxxing/lb-source-propagation
test -d tiles-maxxing/cpp-campaign-v2
test -d tiles-maxxing/cuda-campaign-v2-sqrt-36
```

Stop if:

- implementation surfaces are dirty before work starts;
- a worker prompt points at root-level `docs/`, `artifacts/`, or `src/` as
  authority;
- existing remote artifacts are summarized without their diagnostic/non-claim
  status.

Named gate: `PATH_AUTHORITY_PREFLIGHT`.

### Phase 1: Correctness Hardening Before Speed - 1.5h

Owner: core state worker plus verifier/auditor.

Work:

- add direct live API tests that reject unstable atom ids in
  `process_band_live(...)`, not only serialized `LiveHandoffV1`;
- add last-band summary replay tests rejecting wrong `geometry_id`, `build_id`,
  schedule digest algorithm/hex, `overflow_summary`, and `bridge_policy`;
- add a bridge fixture where neutral incoming carry welds into source after
  closure and owns an unsafe bridge candidate;
- keep `LB_SOURCE_CARRY_MANIFEST_V1` compatibility coverage, but do not use
  legacy carry manifests in the LB hot continuation protocol.

Gate:

```bash
cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp-opt-k26 -DK_SQ=26
cmake --build /tmp/gm-lbsp-opt-k26 -j
/tmp/gm-lbsp-opt-k26/source_prop_tests
ctest --test-dir /tmp/gm-lbsp-opt-k26 --output-on-failure
```

Stop if:

- fresh source seeding is accepted with an incoming live handoff;
- neutral carry can be dropped without a failing test;
- a terminal summary replay accepts mismatched lineage/envelope fields;
- in-memory live processing can ingest transient roots, TileOp group labels, or
  dense component ids as stable atom ids.

Named gates:

- `LIVE_HANDOFF_LINEAGE_BINDING`
- `CPU_TILEOP_RECONSTRUCTION_PARITY`
- `PHASE6_7_REQUIRED_FOR_TERMINAL`

### Phase 2: Optimization Harness And Telemetry - 1.5h

Owner: harness worker.

Work:

- add or script a repeatable attempt directory;
- wrap benchmark commands with wall time and max RSS capture;
- hash stdout, progress JSONL, live handoff, and summary artifacts even on
  blocked runs;
- emit machine identity, full git SHA, branch, dirty state, build config,
  runner kind, remote kind, GPU kind when present, and command argv.

Artifact layout:

```text
tiles-maxxing/lb-source-propagation/artifacts/lb-opt-YYYYMMDD-12h/
  baseline/
  candidates/<attempt_id>/
    attempt.json
    command.argv.txt
    environment.txt
    cmake-configure.log
    cmake-build.log
    ctest.log
    stdout.json
    progress.jsonl
    time.txt
    sha256sums.txt
    compare.json
    decision.json
```

Minimum attempt schema:

```json
{
  "schema": "lb_opt_attempt_v1",
  "attempt_id": "baseline",
  "git": {"sha_full": "", "branch": "", "dirty": false},
  "machine": {"hostname": "", "cpu": "", "ram_gb": 0,
    "gpu": {"present": false, "name": "", "driver": "", "cuda": ""}},
  "build": {"k_sq": 26, "cmake_args": ["-DK_SQ=26"], "compiler": ""},
  "input": {"runner": "source_tileop_port_runner", "mode": "diagnostic",
    "r_start": 8192, "r_final": 49152, "band_width": 8192,
    "max_atoms": 50000000, "tileop_threads": 32},
  "command": {"cwd": "", "argv": [], "exit_code": 0},
  "timing": {"wall_ms": 0, "max_rss_kb": 0, "grid_ms": 0,
    "tileop_ms": 0, "graph_ms": 0, "target_bridge_ms": 0,
    "process_ms": 0, "total_ms": 0},
  "counts": {"campaign_tiles_processed": 0, "port_atoms": 0,
    "internal_edges": 0, "seam_edges": 0, "source_carry_atoms": 0,
    "source_frontier_count": 0},
  "correctness": {"accepted": true, "tileop_overflows": 0,
    "terminal_source_dead": false,
    "source_unbridged_unsafe_candidate_atoms": 0},
  "outputs": {"stdout_sha256": "", "progress_sha256": "",
    "live_handoff_sha256": "", "source_frontier_digest_hex": ""},
  "equivalence": {"baseline_attempt_id": "", "status": "", "field_mismatch": {}},
  "decision": {"accepted": false, "reason": ""}
}
```

Gate:

- one fixed baseline can be rerun and parsed into the schema;
- RSS is recorded;
- partial/blocked artifacts still get hashes.

Named gates:

- `REMOTE_KIND_BINDING`
- `SEMANTIC_MODE_BINDING`

### Phase 3: TileOp Producer Boundary - 2.0h

Owner: TileOp ingestion worker.

Work:

- refactor `source_tileop_port_runner` around a small producer boundary:

```text
Grid + coords -> vector<campaign::TileOp> + ProducerStats
```

- first producer is CPU and wraps current behavior;
- keep normal LB CMake free of CUDA requirements;
- add producer metadata to progress/output JSON without changing live
  connectivity semantics;
- keep `make_tileop_port_band(...)` as the correctness oracle.

Acceptance gate:

- refactored CPU producer and pre-refactor behavior produce byte-identical
  final live handoffs for the fixed equivalence gate;
- `port_atoms`, `internal_edges`, `seam_edges`, overflow counters,
  source frontier count, and source frontier digest match.

Command:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_tileop_port_wide_band_equivalence.sh \
  /tmp/gm-lbsp-opt-k26/source_tileop_port_runner \
  --r-start 8192 \
  --segments 5 \
  --segment-width 8192 \
  --max-atoms 50000000 \
  --tileop-threads 32 \
  --out-dir /tmp/gm-lbsp-opt-wide-equivalence
```

Stop if:

- refactor changes live handoff bytes;
- target bridging starts seeding source;
- coordinate-to-port bridge reconstruction is removed or weakened.

Named gate: `CPU_TILEOP_RECONSTRUCTION_PARITY`.

### Phase 4: Snapshot Replay Producer - 1.0h

Owner: TileOp ingestion worker.

Work:

- add a `SnapshotTileOpProducer` for CMV2 snapshot replay;
- validate magic, version, `bytes_per_tile == 256`, `k_sq`, radii/grid hash,
  constants hash, MR hash, and tile count before replay;
- use snapshots only as replay/debug artifacts, not W-scale default storage.

Gate:

- CPU producer vs snapshot replay produces byte-identical live handoff;
- snapshot order matches `Grid::flat_index`/column-major order exactly;
- mismatched grid/constants/MR metadata is rejected.

Stop if:

- replay requires trusting producer-supplied booleans;
- snapshot persistence becomes required for normal continuation.

### Phase 5: CUDA In-Memory Producer - 2.0h

Owner: CUDA ingestion worker plus remote validator.

Work:

- add CUDA-enabled producer/runner under the CUDA campaign surface or a
  CUDA-only build target, not the default Mac LB build;
- feed byte-compatible `campaign::TileOp` records from CUDA host dispatcher
  into existing `make_tileop_port_band(...)`;
- emit CUDA producer stats separately from CPU sidecar stats.

Required metadata:

```text
tileop_source
producer_tiles
producer_chunks
producer_slabs
host_chunk_tiles
device_slab_tiles
cuda_device_name
cuda_driver
cuda_runtime
cuda_k1_k5_ms
d2h_ms
```

Gates:

- existing CUDA CPU-vs-GPU TileOp byte parity or snapshot SHA gate passes;
- CPU LB runner and CUDA LB runner over the same schedule/radii produce
  byte-identical live handoffs;
- progress JSON differs only in timing and producer metadata;
- artifacts declare `remote_kind=cuda_kernel` only when CUDA kernels actually
  ran.

Stop if:

- RTX 4090 host CPU timing is reported as GPU validation;
- normal LB CMake requires CUDA on the Mac;
- CUDA changes TileOp wire semantics or port ordering.

Named gate: `REMOTE_KIND_BINDING`.

### Phase 6: Streaming Or Early-Cap Slice - 1.5h

Owner: streaming worker.

Decision:

If producer/CUDA work is enough for K26 timing, defer full streaming. If
materialized `coords + tileops + full port graph` remains the limiter, implement
only the first streaming slice.

First streaming slice:

- add count-only/grid-ID preflight and early materialization caps;
- add sampled TileOp pressure probe;
- optionally add column/slab port graph folding that preserves materialized
  output on small fixtures.

Gate:

- materialized runner remains the oracle;
- streaming/preflight code proves bounded memory on synthetic long-chain or
  high-radius thin probes;
- no W-scale claim is made.

Stop if:

- source processing semantics need to be rewritten before the port graph slice
  can be checked;
- output comparison is only live/dead status instead of live handoff bytes and
  frontier fields.

Named gate: `NO_W_SCALE_MATERIALIZED`.

### Phase 7: R~60M And R~400M Non-Claim Gates - 1.0h

Owner: validation worker.

These gates are mandatory because the requested future W-scale target is
large, but they are not full-width acceptance runs in the current materialized
runner.

Gate 1: artifact sanity.

```bash
jq -e '.status=="PASS" and all(.high_radius_profiles[]; .accepted and .tileop_overflows==0)' \
  tiles-maxxing/lb-source-propagation/artifacts/vast-campaign-gates-livecut/campaign-gates-summary.json
```

Gate 2: count-only grid/ID preflight, after the preflight tool exists.

```bash
source_tileop_port_preflight --mode grid-id --k-sq 26 \
  --r-start 60000000 --r-final 60008192

source_tileop_port_preflight --mode grid-id --k-sq 26 \
  --r-start 400000000 --r-final 400008192
```

Pass criteria:

- grid invariants pass;
- `R_outer^2` fits `uint64_t`;
- tile `i/j` encodes within port atom id limits;
- coordinate `a/b` fits coordinate atom id limits;
- exact tile count is within 10% of the estimate.

Expected full-width estimates for current 8192-wide bands:

| Radius | Tiles | Port atoms | Edges | CPU time extrapolation |
|---:|---:|---:|---:|---:|
| `60M` | `~6.1M` | `~0.42B-0.49B` | `~0.45B` | `~4.3-4.9h` |
| `400M` | `~40.8M` | `~2.8B-3.3B` | `~3.0B` | `~28-33h` |

Those current materialized full-width runs are no-go until streaming or a
different bounded ingestion path exists.

Gate 3: sampled TileOp pressure, after the sample tool exists.

```bash
source_tileop_port_sample_probe --k-sq 26 \
  --r-start 60000000 --r-final 60008192 --sample-count 8192

source_tileop_port_sample_probe --k-sq 26 \
  --r-start 400000000 --r-final 400008192 --sample-count 8192
```

Pass criteria:

- `tileop_overflows=0`;
- max tile ports `<=192`;
- max labels `<=128`;
- sampled port atom ids encode;
- mode is explicitly `sample_probe_non_claim`.

Gate 4: thin real materialized probes at target radii.

```bash
/usr/bin/time -v /tmp/gm-lbsp-opt-k26/source_tileop_port_runner \
  --r-start 60000000 --r-final 60000128 --band-width 128 \
  --schedule-radii 60000000,60000128 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out /tmp/lb-r60-w128.progress.jsonl \
  > /tmp/lb-r60-w128.json

/usr/bin/time -v /tmp/gm-lbsp-opt-k26/source_tileop_port_runner \
  --r-start 400000000 --r-final 400000016 --band-width 16 \
  --schedule-radii 400000000,400000016 \
  --seed-inner-flags \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --progress-out /tmp/lb-r400-w16.progress.jsonl \
  > /tmp/lb-r400-w16.json
```

Expected thin-probe scale:

| Probe | Expected ports | Expected time |
|---|---:|---:|
| `R60M W=128` | `~6.6M-7.7M` | `~4-5m` |
| `R400M W=16` | `~5.5M-6.4M` | `~3-4m` |
| `R400M W=32` | `~11M-12.9M` | `~7-8m` |

Pass criteria:

- accepted diagnostic output;
- `tileop_overflows=0`;
- RSS under host cap;
- R60 ports `<=10M`;
- R400 W16 ports `<=9M`;
- all rows declare `source_mode` and `proof_status`.

Gate 5: thin schedule-split equivalence at exact target radii.

```bash
tiles-maxxing/lb-source-propagation/scripts/check_tileop_port_wide_band_equivalence.sh \
  /tmp/gm-lbsp-opt-k26/source_tileop_port_runner \
  --r-start 60000000 \
  --segments 4 \
  --segment-width 32 \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --out-dir /tmp/lb-r60-thin-equivalence

tiles-maxxing/lb-source-propagation/scripts/check_tileop_port_wide_band_equivalence.sh \
  /tmp/gm-lbsp-opt-k26/source_tileop_port_runner \
  --r-start 400000000 \
  --segments 2 \
  --segment-width 8 \
  --max-atoms 20000000 \
  --tileop-threads 32 \
  --out-dir /tmp/lb-r400-thin-equivalence
```

Pass criteria:

- script passes;
- live handoff bytes are identical;
- frontier fields match;
- output remains diagnostic non-claim.

Named gate: `SEMANTIC_MODE_BINDING`.

### Phase 8: Acceptance Pack - 1.0h

Owner: coordinator plus auditor.

Work:

- select one integrated candidate or explicitly defer implementation;
- collect artifact hashes, timings, RSS, and equivalence results;
- update this file with deviations if needed;
- write the final goal-loop summary.

Gate:

```bash
git diff --check
cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp-final-k26 -DK_SQ=26
cmake --build /tmp/gm-lbsp-final-k26 -j
ctest --test-dir /tmp/gm-lbsp-final-k26 --output-on-failure
cmake -S verification -B /tmp/gm-verify-lb-final
cmake --build /tmp/gm-verify-lb-final -j
ctest --test-dir /tmp/gm-verify-lb-final --output-on-failure
```

Acceptance artifact:

```text
tiles-maxxing/lb-source-propagation/artifacts/lb-opt-YYYYMMDD-12h/acceptance/
  optimization-acceptance.json
  timing-before-after.md
  commands.txt
  artifact-ledger.sha256
  residual-risks.md
```

Stop if:

- final selected diff fails a correctness gate;
- measured improvement disappears on rerun;
- provenance lacks branch/head/build/K_SQ;
- the acceptance summary uses proof language for diagnostic output.

## Optimization Hypotheses

H1: Producer boundary is the highest-leverage first step.

The LB runner already consumes `coords + tileops` through
`make_tileop_port_band(...)`. Factoring TileOp production lets CPU, snapshot,
and CUDA producers share the same port graph and live-handoff oracle.

H2: CUDA ingestion can reduce TileOp build time before streaming exists.

The largest measured profile spent about `219s` of `256s` in CPU TileOp build.
CUDA TileOp production should attack the dominant cost first, while preserving
byte-compatible `campaign::TileOp` records.

H3: Snapshot replay is a parity tool, not the W-scale substrate.

CMV2 snapshots are useful to prove producer boundaries and CPU/CUDA parity.
They should not become required hot-state storage for W-scale continuation.

H4: Port graph materialization is the next memory bottleneck after TileOp build.

At R~60M and R~400M the full materialized edge/atom graph is not feasible.
Column/slab folding and early caps should come after producer parity.

H5: Ordered maps/sets are possible micro-optimization targets, but only behind
deterministic final ordering.

Replacing `std::set`/`std::map` with vectors, hash maps, and final sorting can
improve graph build time. It must not change byte-stable live handoffs.

H6: Band width is not monotonically better.

Existing evidence showed one wide band was slower than five stitched bands for
the same final live handoff. Width/chunk tuning must be measured, not assumed.

H7: Coordinate-to-port bridge reconstruction is correctness plumbing.

The first coordinate-to-port seam and target/path expansion may still need CPU
reconstruction or an equivalent witness format. Do not remove it as a speed
shortcut.

H8: R~60M/R~400M can be probed safely with thin real runs.

The current full 8192-width materialized bands are no-go, but count preflight,
sample probes, and thin real microbands at the exact radii are useful.

## Invariants Warning

These are hard constraints during optimization:

- `geo_I` and `geo_O` are static annulus surfaces, not source certificates.
- first-band source may come only from `ORIGIN_SOURCE`, `WIRED_SOURCE`, or
  `CERTIFIED_SEED`; later bands seed only from incoming live source bits.
- `--seed-inner-flags` must not be combined with `--live-manifest-in`.
- source bits attach to partition classes, not individual atoms.
- all carry atoms, including neutral classes, must be preserved through the
  carry window.
- dropping neutral carry can create false terminal death.
- treating all carry as source can invent source reachability.
- carry width is `ceil_sqrt(K)` and must not be under-carried.
- live handoff hot state must stay frontier-only and must not grow historical
  `component_inventory`.
- coordinate atom ids and port atom ids must be stable; transient UF roots,
  TileOp group labels, and dense component ids are forbidden as continuation
  atom ids.
- target bridging must not seed source.
- port atoms and support norms are continuation diagnostics, not coordinate
  furthest-component proof evidence.
- a progress row alone is not terminal evidence.
- `terminal_source_dead` and last-band summaries remain diagnostic until
  independent replay/accumulator/path/BZ/negative-guard gates exist.
- remote RTX 4090 execution is not GPU validation unless CUDA kernels actually
  run and artifacts say so.

## Risk Register

| Severity | Risk | Early detector | Mitigation | Gate |
|---|---|---|---|---|
| Critical | Materialized CPU path mistaken for W-scale | RSS, tile count, port atom/edge count, phase timings | no large-R full-width campaign until streaming/bounded ingestion exists | `NO_W_SCALE_MATERIALIZED` |
| Critical | CPU TileOp reconstruction shortcut creates false speedup | TileOp port graph tests, stale TileOp rejection, wide-vs-stitched equivalence | byte-stable TileOp/port parity before accepting speed | `CPU_TILEOP_RECONSTRUCTION_PARITY` |
| High | live handoff lineage breaks or source revives | stale envelope/source-revival fixtures | bind K, cut, carry width, source identity, build identity, schedule digest | `LIVE_HANDOFF_LINEAGE_BINDING` |
| High | R~60M/R~400M semantic drift | every row declares mode, K, seed/handoff source, verifier, status | treat high-radius probes as perf/diagnostic unless real source handoff exists | `SEMANTIC_MODE_BINDING` |
| High | Vast timing misreported as GPU validation | remote kind field plus CUDA/GPU telemetry | split remote CPU timing from CUDA validation | `REMOTE_KIND_BINDING` |
| High | K26 runtime projection overfits early bands | stratified early/mid/high-radius probes | budget projection after each chunk | `K26_PROJECTION_STRATIFIED` |
| High | terminal diagnostic promoted past boundary | claim-token scan and diagnostic status checks | block promotion until independent proof gates exist | `PHASE6_7_REQUIRED_FOR_TERMINAL` |
| Medium | path authority drift | path preflight | use active repo paths only | `PATH_AUTHORITY_PREFLIGHT` |

## Suggested Worker Split

Use GPT-5.5 high/xhigh workers with disjoint scopes:

1. correctness hardening worker:
   `source_propagation` tests and guards only.
2. harness worker:
   scripts/artifact telemetry only.
3. producer-boundary worker:
   `source_tileop_port_runner` producer refactor only.
4. snapshot replay worker:
   CMV2 reader/replay producer and parity tests only.
5. CUDA producer worker:
   CUDA-only build surface and remote parity gates only.
6. high-radius preflight worker:
   grid-ID/sample probes and R~60M/R~400M thin gates only.
7. auditor:
   checks this plan, no-claim semantics, and gate evidence.

Every worker report must include:

```text
Done:
Found:
Evidence:
Changed paths:
Commands:
Blocked:
Residual risk:
```

## Suggested `/goal`

```text
Execute the overnight 4090 full-scale benchmarking/profiling campaign in
tiles-maxxing/lb-source-propagation/docs/archive/lb-source-propagation-optimization-plan-20260524.md. Rent one Vast RTX
4090 instance only if the offer is <= $0.75/h, run for up to 12h wall time,
and use it for remote K26 LB correctness gates, baseline profiling, K26
runtime-budget probes, R~60M and R~400M thin high-radius probes, CUDA
TileOp/campaign profiling where available, and thread/band/chunk sweep
benchmarks. Pull and hash artifacts incrementally, produce the overnight
acceptance pack, and destroy the instance after successful harvest. If harvest
fails, stop the instance and report recovery state rather than destroying
unrecovered artifacts. Do not promote any diagnostic artifact to a
source-death proof.
```

Initial done gate:

```text
The 4090 campaign artifact pack exists under
tiles-maxxing/lb-source-propagation/artifacts/lb-opt-YYYYMMDD-4090-overnight/,
remote environment and Vast instance metadata are recorded, K26 correctness
gates and equivalence gates are reported with logs, R~60M/R~400M probes either
ran or have explicit blocked reasons, K26 runtime-budget projections are
computed from remote progress JSONL, CUDA-vs-CPU status is separated from
remote CPU sidecar timing, artifact hashes are recorded, cleanup status is
recorded, and every output remains diagnostic/non-claim.
```
