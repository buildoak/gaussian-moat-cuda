# LB Code Quality Guidelines

Updated: 2026-05-26.

The LB codebase is an agentic workbench. Code should be easy for future agents
to inspect, modify, test, and verify without rebuilding a private mental model
from scratch. Prefer small files, narrow modules, explicit contracts, and
comments that explain invariants.

## Default Shape

New implementation should be organized around small, named surfaces:

```text
one concept
-> one header contract
-> one implementation file
-> one focused test file
-> one artifact/checker contract if it writes durable output
```

Do not add broad "manager", "pipeline", or "campaign" files unless there is no
smaller domain object. A file should normally be understood in one sitting.

## Size Targets

These are guidelines, not hard compiler rules:

| File type | Target | Requires justification above |
|---|---:|---:|
| Header | 50-180 lines | 250 lines |
| Library `.cpp` | 100-450 lines | 700 lines |
| CLI/app `.cpp` | 150-500 lines | 700 lines |
| Test file | 150-600 lines | 900 lines |
| Shell script | 80-350 lines | 500 lines |

Existing oversized files are not a license to add more mass. When touching an
oversized file, prefer one of these moves:

- extract a pure helper into a small module;
- add a local section comment that names the invariant being preserved;
- move artifact parsing/checking into a checker module;
- split CLI argument parsing from the computational core;
- add a focused regression test before changing behavior.

Do not perform a broad file split merely for aesthetics. Extract when it
reduces active complexity, clarifies ownership, or creates a useful test seam.

## Module Boundaries

Keep these boundaries visible:

- `source_propagation`: certified source/origin live handoff semantics.
- `stream_checkpoint`: restart/replay envelopes and checkpoint validation.
- `tileop_port_stream`: bounded streaming TileOp/port ingestion primitives.
- `tileop_port_graph`: materialized diagnostic port graph conversion.
- `tileop_live_bridge`: coordinate-to-port handoff bridge evidence.
- app runners: CLI parsing, orchestration, artifact emission; no hidden math.
- verification code: independent readers/checkers, not campaign imports.

If a function needs knowledge from three or more of these domains, it is likely
orchestration and should not also own math, parsing, and artifact policy.

## Comments

Use comments to preserve reasoning that is hard to recover from code:

- mathematical invariants;
- proof-status boundaries;
- source vs geometric boundary distinctions;
- why a stricter-looking condition is wrong;
- why a diagnostic path is not claim evidence;
- artifact compatibility constraints;
- non-obvious performance or memory tradeoffs.

Avoid comments that restate the next line of code. Prefer comments at the
boundary of a block or type, not on every assignment.

Good comment shape:

```cpp
// Neutral carry is retained because it can weld into the source component in a
// later band; dropping it preserves current source bits but corrupts future
// continuation.
```

Bad comment shape:

```cpp
// Increment i.
++i;
```

## Function Design

Prefer functions that:

- take typed inputs instead of loosely related parallel vectors;
- return a structured result with diagnostic text on rejection;
- separate validation from mutation where practical;
- keep serialization/deserialization out of core math;
- make proof status explicit in names or result fields.

Avoid functions that:

- infer source state from `geo_I`;
- silently downgrade claim-grade failures to diagnostics;
- combine CLI parsing, file IO, graph construction, and verification;
- mutate incoming handoffs in place;
- use transient union-find roots or TileOp group labels as durable ids.

## Artifact Contracts

Any durable artifact must answer:

- schema/version;
- mode;
- branch/commit/build identity;
- geometry and schedule;
- oracle/support policy;
- proof status;
- producer command;
- whether it is restart state, diagnostic evidence, or claim evidence.

If an artifact is not ready for claim use, write that into the artifact or the
checker output. Silence invites accidental promotion.

## Tests

Every behavior change needs at least one hostile or equivalence test.

Prefer:

- one-big-band vs stitched equivalence;
- interrupted vs resumed equivalence;
- stale context rejection;
- source reseeding rejection;
- neutral-carry weld cases;
- overflow rejection;
- malformed artifact rejection.

Do not rely only on happy-path smoke tests when changing handoff, checkpoint,
bridge, or proof-status behavior.

## Agentic Loop Rule

An autonomous worker may make code changes only when it can name:

```text
mode
owned files
new or changed invariant
local gate
proof-status boundary
```

If a change makes a file larger than the guideline threshold, the worker must
state why extraction was not the smaller safe move.
