# LB Code Alignment Goal

Updated: 2026-05-26.

## Goal

Make the LB implementation a lightweight resumable CUDA-band workbench: exact,
restartable, replayable band-to-band continuation that reuses the battle-tested
TileOp/CUDA band engine and keeps hot state bounded by live frontier state,
checkpoints, and harvest ledgers.

Source/origin propagation is an opt-in overlay for certified source survival,
death, last-live/first-dead refinement, and certificates. It must not become
the default burden for static-annulus, high-K, or prolonged detector campaigns.

## Modes

Every code-alignment task must declare one mode before touching files:

| Mode | Code objective | Source overlay |
|---|---|---|
| `resumable-band` | Checkpoint/restart/harvest/replay for prolonged CUDA-band campaigns. | Not required. |
| `source-origin` | Exact source/origin survival through stacked bands. | Required. |
| `proof-refinement` | Promote a diagnostic row toward a stronger claim. | Required when the claim names source/origin. |
| `static-annulus` | CUDA detector row execution or verification. | Not required. |

If the task does not explicitly need source/origin semantics, default to
`resumable-band` or `static-annulus`, not `source-origin`.

## Impact Area

### Core LB Surfaces

These are the expected code-alignment surfaces:

- `include/lb_source/source_propagation.h`
- `src/source_propagation.cpp`
- `tests/test_source_propagation.cpp`
- `include/lb_source/stream_checkpoint.h`
- `src/stream_checkpoint.cpp`
- `tests/test_stream_checkpoint.cpp`
- `include/lb_source/resumable_band_checkpoint.h`
- `src/resumable_band_checkpoint.cpp`
- `tests/test_resumable_band_checkpoint.cpp`
- `apps/source_tileop_port_stream_runner.cpp`
- `tests/test_tileop_port_stream_runner.sh`
- `include/lb_source/tileop_port_stream.h`
- `src/tileop_port_stream.cpp`
- `tests/test_tileop_port_stream.cpp`
- `include/lb_source/tileop_port_graph.h`
- `src/tileop_port_graph.cpp`
- `tests/test_tileop_port_graph.cpp`

### Verifier And Schema Surfaces

Touch these only when a code change changes an artifact contract:

- `verification/schemas/source-prop-fixture.schema.json`
- `verification/schemas/source-dead-cert-draft.schema.json`
- `verification/schemas/source-dead-gap.schema.json`
- `verification/src/source_prop_oracle.cpp`
- `verification/src/source_dead_cert_check.cpp`
- `verification/src/source_dead_gap_check.cpp`
- `verification/fixtures/source_prop/`
- `verification/test_source_prop_schema_contract.py`

### Remote/Vast Surfaces

Touch these only for execution plumbing, artifact collection, or remote gate
guardrails:

- `scripts/vast_sidecar_smoke_guard.sh`
- `scripts/remote_sidecar_smoke.sh`
- `scripts/remote_k26_timing_probe.sh`
- `scripts/remote_overnight_4090_campaign.sh`
- `scripts/check_remote_smoke_artifacts.sh`
- `scripts/check_remote_k26_timing_artifacts.sh`
- `scripts/check_overnight_4090_acceptance.py`
- `../vast-ai/README.md`

### Out Of Scope Unless Explicit

- CUDA TileOp kernel rewrites.
- Static-annulus verdict semantics.
- K36/K38/K40 campaign claim language.
- `MOAT_PROOF_PASS` or accepted source-death proof claims.
- K26 full-bundle proof promotion.
- Whole-band historical inventory as hot state.

## Alignment Targets

1. `resumable-band` has a small, explicit checkpoint artifact that binds
   geometry, command/build/oracle identity, progress, artifacts, and replay
   status.
2. `source-origin` continues from `LiveHandoffV1` and never reseeds from
   `geo_I` after an incoming handoff exists.
3. The streaming path is honest about its current status: diagnostic MVP until
   certified/incoming live handoffs and death artifacts are implemented.
4. Last-live/first-dead refinement is targeted. Coarse runs do not carry
   terminal inventories, full slabs, or whole-band port graphs unless a
   proof-tier gate explicitly requires them.
5. All generated rows remain `DIAGNOSTIC_NON_CLAIM` unless an independent gate
   accepts the stronger status.

## First Code Slice

The smallest useful alignment slice is:

```text
ResumableBandCheckpointV1
-> stream runner can write/read it
-> resume-equivalence test passes
-> artifact checker rejects stale/mismatched checkpoint context
-> remote smoke pulls and validates the checkpoint artifacts
```

That slice should not introduce source-death certificates, K26 proof promotion,
or new CUDA kernel behavior.

## Done Gate

A code-alignment loop is done only when:

- the declared mode is visible in the report;
- changed code matches the mode boundary above;
- local relevant tests pass;
- generated artifacts are ignored or deliberately pulled into the right
  artifact path;
- claim status is correctly labeled;
- any Vast instance used is listed in the Gaussian Moat ownership ledger before
  cleanup is attempted.
