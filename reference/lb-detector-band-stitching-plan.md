# LB Detector Band Stitching Plan

Status: operational contract for implementing Layer 1 `resumable-band`
detector continuation.

This document is Layer 1 only: static-annulus detector continuation across
bands. It does not introduce source-origin proof semantics, source-death
certificates, proof-status promotion, or full tile inventories in the hot path.

## Objective

Build detector band stitching so CUDA/TileOp static-annulus campaigns can stop,
resume, and stitch bands by exact mathematical continuation:

```text
D_i = carry ports + component_partition + reach_flags_per_component
```

`D_i` is detector/workbench evidence only. It is not `LiveHandoffV1`, not
source-origin state, and not a proof certificate.

## Corrected Architecture

- Add `DETECTOR_BAND_HANDOFF_V1` as a standalone deterministic binary blob.
- Keep `ResumableBandCheckpointV1` operational. It references only handoff
  `path`, `sha256`, `schema`, and `bytes`.
- Resume loads the checkpoint, then loads exactly the referenced handoff blob.
  The referenced blob is the mathematical source of truth.
- Default runtime keeps at most one durable live detector handoff per run.
- Static reach seeding is global-annulus aware:
  - first band may seed `INNER_REACHED`;
  - final band may seed `OUTER_REACHED`;
  - interior bands seed neither;
  - imported reach comes only from `DETECTOR_BAND_HANDOFF_V1`.
- CUDA alignment is host-side only. Do not change CUDA kernels for this layer.

The seam invariant is:

```text
geo_O(N-1) == geo_I(N)
```

under the same grid, TileOp oracle, port identity scheme, support envelope,
schedule digest, and boundary ownership rules.

## Atomic Blob And Checkpoint Protocol

Checkpoint wins. Resume never scans for "latest"; it follows the handoff blob
referenced by the checkpoint or rejects.

1. Process the band using the incoming blob iff `schedule_index > 0`.
2. Canonicalize the outgoing detector separator.
3. Serialize `DETECTOR_BAND_HANDOFF_V1` bytes to a temp path in the same
   filesystem.
4. Validate the temp blob by read-back, context, byte count, and SHA-256.
5. Atomically rename the temp blob to the single live handoff path.
6. Write the checkpoint only after the live blob is verified.
7. Checkpoint references `detector_handoff_path`,
   `detector_handoff_sha256`, `detector_handoff_schema`, and
   `detector_handoff_bytes`.
8. Fresh first-band start may omit incoming handoff only at the documented
   initial schedule position.
9. Any resume after a completed cut rejects if the blob is missing, wrong hash,
   wrong byte count, wrong schema, or wrong context.

Power-loss durability claims require file and directory `fsync`; otherwise the
contract is crash-consistent for normal process interruption, not power-loss
certified.

## One-Live-Handoff Invariant

Default run state keeps one durable live detector handoff:

```text
checkpoint -> detector_handoff.current.bin
```

Optional history is allowed only for explicit debug/equivalence/proof-tier
modes and must be labeled non-default. Pruning must never delete the blob
referenced by the current checkpoint. Temporary files and stale blobs are
cleanup state, not mathematical history.

## Binary Format Requirements

`DETECTOR_BAND_HANDOFF_V1` must be deterministic and host-independent:

- fixed magic/schema/version;
- little-endian integers;
- explicit counts and offsets;
- no raw C++ struct dumps, `size_t`, or padding;
- length-prefixed ASCII manifest tokens for geometry/oracle/build/support/
  schedule identities;
- canonical hash computed over canonical binary bytes.

Records:

```text
CarryRecord(atom_id:int64, norm_sq:uint64)
ComponentRecord(first_atom_index:uint64, atom_count:uint64, reach_flags:uint8)
component_atom_table[]: sorted int64 carry atom ids
```

Canonical order:

- carry atoms sorted by `(atom_id, norm_sq)`;
- component atoms sorted unique;
- components sorted lexicographically by atom ids.

Validation rejects duplicate carry atoms, non-covering partitions, empty
components, invalid reach bits, unstable atom ids, wrong carry width, wrong cut
radius, wrong schedule digest, wrong oracle/build/geometry/support ids,
truncation, trailing bytes, and count overflow. The handoff contains no source
fields, no inventories, no full-band graphs, and no transient component ids.

## Worker Waves

Use `gpt-5.5 high` workers sequentially. Do not start a later wave until the
prior wave's gate passes.

### Wave 1: Binary Handoff Surface

Files:

- `tiles-maxxing/lb-source-propagation/include/lb_source/detector_band_handoff.h`
- `tiles-maxxing/lb-source-propagation/src/detector_band_handoff.cpp`
- `tiles-maxxing/lb-source-propagation/tests/test_detector_band_handoff.cpp`
- CMake wiring.

Non-goals: checkpoint, runner resume, CUDA, source handoff.

Gate:

- canonical round-trip;
- stable hash;
- reordered equivalent separators hash identically;
- malformed blob rejection;
- no `LiveHandoffV1`, source bits, or inventory fields.

### Wave 2: Global Boundary Seeding

Files:

- `tiles-maxxing/lb-source-propagation/include/lb_source/tileop_static_reach.h`
- `tiles-maxxing/lb-source-propagation/src/tileop_static_reach.cpp`
- possibly `tileop_port_stream.*` if seed policy must move there;
- `tiles-maxxing/lb-source-propagation/tests/test_tileop_static_reach.cpp`.

Non-goals: durable IO, checkpoint, CUDA.

Gate:

- explicit one-band/first/interior/final seed policy;
- three-band false-span regression passes;
- interior seed counters are zero;
- one-band and stitched fixtures agree;
- source-origin tests keep existing semantics.

### Wave 3: Checkpoint Binding

Files:

- `tiles-maxxing/lb-source-propagation/include/lb_source/resumable_band_checkpoint.h`
- `tiles-maxxing/lb-source-propagation/src/resumable_band_checkpoint.cpp`
- `tiles-maxxing/lb-source-propagation/tests/test_resumable_band_checkpoint.cpp`.

Non-goals: embedding separator contents.

Gate:

- checkpoint contains only handoff reference fields;
- checkpoint round-trips handoff `path`, `sha256`, `schema`, and `bytes`;
- validation rejects missing/bad/stale/wrong-context handoff references;
- fresh first-band start remains allowed at the documented initial schedule
  position;
- checkpoint contains no separator, source handoff, or source-origin state.

### Wave 4: CPU Handoff And Resume Equivalence

Files:

- `tiles-maxxing/lb-source-propagation/apps/tileop_static_reach_equivalence.cpp`
- focused shell/unit tests.

Non-goals: CUDA.

Gate:

- one-big vs stitched requires verdict equality and canonical final handoff
  equality;
- interrupted/resumed equals uninterrupted by final handoff hash and verdict;
- wrong checkpoint blob rejects before processing;
- JSON reports `handoff_equal`, `verdict_equal`, `seed_policy`, and interior
  seed counters.

### Wave 5: One-Live-Handoff Runtime Policy

Files:

- runner IO path in the CPU equivalence/runner surface;
- handoff IO helpers/tests.

Non-goals: handoff history by default.

Gate:

- default run leaves one durable live blob;
- optional debug retention is explicit and labeled non-default;
- cleanup never deletes the blob referenced by the current checkpoint;
- crash matrix covers partial blob, partial checkpoint, new blob with old
  checkpoint, new checkpoint missing blob, stale current ref, and cleanup.

### Wave 6: CUDA Host Alignment

Files:

- `tiles-maxxing/cuda-campaign-v2-sqrt-36/apps/cuda_static_reach_equivalence.cpp`
- CUDA CMake/script updates only if needed.

Non-goals: CUDA kernels, proof promotion.

Gate:

- same seed policy and telemetry as CPU;
- default width remains `8192` or at least `4096`;
- sub-`4096` requires diagnostic override;
- zero overflow counters for acceptance;
- zero interior global seed ports;
- verdict and handoff equality when comparator is enabled.

### Wave 7: Docs And Contract Sync

Files:

- this document;
- local README/docs only if runtime behavior changed.

Non-goals: methodology rewrite unless implementation exposes a real contract
change.

Gate:

- docs state handoff vs checkpoint split;
- docs state seeding rule, binary blob, one-live invariant, and detector-only
  claim status;
- no `SOURCE_DEAD_CERT_PASS`, `MOAT_PROOF_PASS`, or `SPAN_PROOF_PASS` claim
  promotion is introduced.

## Test Matrix

- Unit: binary round-trip, canonicalization, hash stability, hostile parse
  failures.
- Checkpoint: no embedded separator/source state, blob reference validation,
  wrong hash/bytes/schema/path/context rejection.
- Seeding: first/interior/final policy and local-middle-span false positive
  blocked.
- Equivalence: one-big vs stitched handoff equality, verdict equality, resumed
  vs uninterrupted equality.
- Runtime: one-live blob invariant and crash/cleanup matrix.
- CUDA: bounded equivalence rows, production-width guard, zero overflow.
- Regression: existing `ctest` for `lb-source-propagation`; source-origin tests
  unchanged.

## Telemetry

Emit JSON fields for:

```text
mode
proof_status
seed_policy
interior_inner_seed_ports
interior_outer_seed_ports
final_handoff_sha256
detector_handoff_bytes
handoff_equal
verdict_equal
resume_equal
checkpoint_handoff_source
max_live_frontier_ports
max_components
max_resident_microband_tiles
max_resident_edges
canonicalize_ms
handoff_write_ms
handoff_hash_ms
cuda_overflow_counters
```

## Overhead Guardrails

Hot state is frontier-sized:

```text
O(P + C)
P = carry ports
C = carry components
```

Approximate binary payload:

```text
16P carry records
+ 8P component atom refs
+ 24-32C component records
+ small manifest header
```

Checkpoint should stay tiny, normally under a few KB. Canonicalization is
`O(P log P)` per emitted cut. Hashing/serialization is `O(P + C)`. The hot path
must not carry historical inventories, whole-band graphs, old handoff chains,
or CUDA kernel overhead.

## Worker Prompt Skeleton

```text
WHAT: Implement Wave N for Layer 1 detector band stitching in gaussian-moat-cuda.
SCOPE: Own only the listed files for this wave.
NON-GOALS: No source-origin semantics, no proof promotion, no CUDA kernel
changes unless this is Wave 6.
CONSTRAINTS: Preserve detector/checkpoint split; binary handoff is the
mathematical truth; checkpoint references path/hash/schema/bytes only; one
live durable handoff by default.
GATE: Run the exact wave gate and report command/output evidence.
REPORT: Done, files changed, tests run, evidence, blocked, next wave readiness.
```

## Final Go/No-Go

Go only if Waves 1-5 pass locally before CUDA work, and Wave 6 passes on CUDA
hardware before campaign use.

No-go if any worker:

- embeds separator data in checkpoints;
- resumes without the referenced blob;
- seeds interior global reach from local `geo_I/geo_O`;
- uses text or host-layout handoff bytes;
- keeps multiple live handoffs by default;
- weakens handoff equality to verdict-only equality;
- touches source-origin semantics as implementation substrate;
- emits proof-pass claim tokens.
