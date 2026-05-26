# LB Source Propagation Goal Plan

Status: historical seed plan. This file preserves the May 22 kickoff contract.
It is no longer the LB first-read source of truth. For current work, read
`../methodology/source-propagation-band-stitching.md`,
`../tiles-maxxing/lb-source-propagation/README.md`, and
`../tiles-maxxing/lb-source-propagation/docs/lb-handoff-redesign.md`.

Updated: 2026-05-22.

## Objective

Build a CPU-first source-propagation sidecar for Gaussian moat lower-bound
campaigns. The sidecar should reuse the battle-tested TileOp / band machinery
as much as possible, but add a mathematically explicit protocol for stitching
independent bands without promoting static-annulus `ANY-SPAN` evidence into
source/origin evidence.

Target branch:

```text
ttc/lb-source-propagation
```

Recommended base:

```text
repair/k40-w262144-telemetry-audit @ 3196602
```

Primary new code folder:

```text
tiles-maxxing/lb-source-propagation/
```

Allowed verifier/schema exceptions:

```text
verification/CMakeLists.txt
verification/README.md
verification/test_source_prop_schema_contract.py
verification/schemas/*source*
verification/fixtures/source_prop/**
verification/src/source_prop_oracle.cpp
verification/src/source_dead_cert_check.cpp
verification/src/source_dead_gap_check.cpp
```

These exceptions are source-propagation verifier surfaces only. They must not
change existing TileOp layout, CUDA kernels, current campaign CLIs,
compositors, or static-annulus verdict semantics.

## Core Model

Current code answers:

```text
Does any component connect geo_I to geo_O inside this static annulus?
```

The new sidecar must answer:

```text
Does the certified source component survive from this band into the next band?
If not, where is the farthest source-connected component before death?
```

Seam state is not just `geo_O_connected`. The required handoff is:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component)
```

This preserves what the processed prefix actually proved:

- `carry_atoms`: all seam/carry objects that may affect future connectivity.
- `component_partition`: which carry atoms are already connected in the prefix.
- `source_bit_per_component`: which carry components are source-connected.

Carrying only source atoms can lose later merges. Carrying all atoms as source
can invent connectivity. Carrying the partition plus source bit is the intended
protocol.

## Methodology First

The first artifact should be a short methodology note, likely:

```text
methodology/source-propagation-band-stitching.md
```

It should state the proof obligations before implementation:

1. Local TileOp equivalence remains the local connectivity engine.
2. Separator state `H_i` is sufficient to replace the processed prefix.
3. The next band may seed only from `H_i`, never from all `geo_I`.
4. Composed bands equal one big band when separator states are exact.
5. Terminal guard: if no source-connected component intersects the final guard
   band `[R - sqrt(K), R]`, no future prime outside `R` can attach to source.

Engineering constraints should follow from these lemmas:

- Use radial carry width `ceil_sqrt(K)`.
- Keep geometric boundary flags separate from source reachability.
- Reject overflow for source/origin claims.
- Preserve complete terminal source-component inventory before compaction drops
  retired roots.
- Bind source certificates to seed, geometry, commit, build, BZ, and artifact
  hashes.

## Verification Gates

### Gate 0: Branch And Diff

- Create `ttc/lb-source-propagation`.
- Start from a clean working tree.
- Phase 1 diff is confined to the new sidecar plus the explicit verifier/schema
  exception above.
- Existing TileOp layout, CUDA kernels, current compositors, current campaign
  CLIs, and current verdict semantics remain untouched.

### Gate 1: Methodology Note

- Add the source-propagation stitching methodology note.
- It must define `CERTIFIED_SEED`, `SOURCE_CARRY`, `SOURCE_TERMINAL`, and
  `SOURCE_DEAD_CERT`.
- It must explicitly state that current `ANY-SPAN` / `ANY-SHELL-MOAT` evidence
  is not source/origin evidence.

### Gate 2: Tiny Oracle

Add independent source-propagation fixtures and checker support. Required
fixtures include:

- one-band identity,
- composed 5 to 10 small stitched bands versus one big band,
- false weld,
- carry-only-source loss,
- source merge through non-source carry partition,
- terminal death,
- overflow reject,
- K32-style `ceil_sqrt(K)` carry-width case.

Pass condition:

```text
composed band state == one big band state
```

Comparison must include separator partition and source bits, not only final
`SPANNING` / `MOAT`.

### Gate 3: Sidecar CPU Propagator

Implement the sidecar with:

- independent CMake/build/test surface,
- CPU TileOp producer path,
- source seed provider,
- source propagator,
- carry manifest writer/reader,
- terminal inventory,
- profile/cert draft output,
- CLI smoke command.

Pass condition:

```text
cmake --build ... && ctest --test-dir ... --output-on-failure
```

### Gate 4: Vast 4090 Smoke

After local tests pass, rent a Vast AI 4090 only under explicit budget:

```text
max price: 0.37 USD/hour
max budget: 1.50 USD unless user extends
```

Remote smoke should build and run the sidecar on small/medium rows. It should
not start a long K32 campaign.

### Gate 5: sqrt(26 Full-Run Preparation

Prepare, then if runtime is reasonable execute, a `sqrt(26)` source/origin
comparison to Tsuchimura:

```text
expected endpoint: 943460 + 376039i
expected radius: about 1015638.765
expected component size: 14,542,615,005
```

This is the meaningful end target for the first source-propagation campaign.
It is cheaper and more diagnostic than K32.

Pass condition:

- source/origin run is labeled separately from static-annulus diagnostics,
- endpoint comparison to Tsuchimura is explicit,
- terminal/death certificate draft exists or the gap to one is named,
- non-square BZ uses external per-row BZ logs and remains diagnostic unless
  fully accepted by the new cert layer.

## Stop Rules

Stop and report instead of pushing through if:

- seam state cannot be made equivalent to one big band,
- source state depends on transient component IDs,
- overflow appears in a source/origin proof row,
- non-square BZ evidence is missing or mismatched,
- terminal certificate cannot inventory retired source components,
- Vast price exceeds the cap,
- K26 runtime or memory exceeds the agreed budget.

## Non-Goals

- Do not claim a new moat result.
- Do not claim K32 source/origin reproduction in this first goal.
- Do not promote current static-annulus `MOAT` rows to `MOAT_PROOF_PASS`.
- Do not mutate battle-tested CUDA or current campaign semantics in Phase 1.
