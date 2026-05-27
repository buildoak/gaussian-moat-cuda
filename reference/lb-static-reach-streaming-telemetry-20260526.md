# LB Static-Reach Streaming Telemetry - 2026-05-26

Status: diagnostic Layer 1 `resumable-band` implementation evidence.

This note records the CUDA static-reach stitching telemetry from the
resident-width / streaming-DSU implementation. It is detector workbench
evidence only: it verifies static-annulus `ANY-SPAN` / `ANY-SHELL-MOAT`
continuation and handoff equality where enabled. It is not source-origin proof
evidence.

## Implementation Under Test

Branch: `goal/lb-resumable-band-code-alignment`.

Commits:

- `70fae20` - `Bound resident static reach stitching`
- `f840f65` - `Avoid ordered duplicate set in streaming reach`
- `480b6ae` - `Reserve streaming reach atom tables`

The implementation keeps the external detector schedule unchanged but splits
each official stitched segment into resident subsegments using
`--static-reach-resident-width`. Inside each resident subsegment, TileOp ports
are folded directly into a DSU by
`process_tileop_static_reach_microband_streaming`, avoiding the materialized
`StaticReachBandInput.edges` vector.

Important code surfaces:

- `tiles-maxxing/lb-source-propagation/src/tileop_static_reach.cpp`
- `tiles-maxxing/lb-source-propagation/include/lb_source/tileop_static_reach.h`
- `tiles-maxxing/cuda-campaign-v2-sqrt-36/apps/cuda_static_reach_equivalence.cpp`
- `tiles-maxxing/cuda-campaign-v2-sqrt-36/scripts/remote_cuda_static_reach_equivalence_campaign.sh`

## Artifact Pointers

Artifacts are intentionally ignored by git.

- Baseline materialized Phase0:
  `tiles-maxxing/cuda-campaign-v2-sqrt-36/artifacts/vast-ai-pull/lb-phase0-telemetry-20260526T1855Z/`
- Streaming/resident-width run:
  `tiles-maxxing/cuda-campaign-v2-sqrt-36/artifacts/vast-ai-pull/lb-streaming-telemetry-20260526T212525Z/`

Remote hardware for the streaming/resident-width run:

- Vast instance `37951435`
- RTX 4090, 24 GiB VRAM, 515.8 GiB host RAM
- actual price shown by Vast: `$0.6667/hr`
- instance destroyed after artifacts were pulled

## Correctness Gates

The full-static equality row passed:

```text
status=CUDA_STATIC_REACH_EQUIVALENCE_PASS
verdict_equal=true
handoff_equal=true
full_static_handoff_sha256=7d2f4cde13db7bf13f9aca7742b7111ade4eb2e8384ef8a330b964df38f2b19e
final_handoff_sha256=7d2f4cde13db7bf13f9aca7742b7111ade4eb2e8384ef8a330b964df38f2b19e
interior_inner_seed_ports=0
interior_outer_seed_ports=0
overflow counters=0
```

This is the load-bearing correctness result: the streaming/resident stitched
path produced the same canonical detector handoff as the materialized
full-static oracle on the bounded equality row.

## Result Table

All rows are `DIAGNOSTIC_NON_CLAIM`.

| Row | Mode | Resident width | Resident subsegments | Status | Peak RSS | Total wall | Handoff equality |
|---|---:|---:|---:|---|---:|---:|---|
| `R=60M`, `W=8192`, production baseline | materialized band | n/a | 4 | pass | 76.4 GB / 71.2 GiB | 28.97 min | n/a |
| `R=60M`, `W=8192`, streaming | streaming DSU | 4096 | 8 | pass | 39.3 GB / 36.6 GiB | 25.11 min | n/a |
| `R=80M`, `W=8192`, production baseline | materialized band | n/a | 4 | pass | 101.1 GB / 94.1 GiB | 41.52 min | n/a |
| `R=80M`, `W=8192`, streaming | streaming DSU | 4096 | 8 | pass | 55.1 GB / 51.3 GiB | 33.71 min | n/a |
| `R=60M`, `W=4096`, full-static oracle row | streaming stitched + materialized oracle | 4096 | 2 | pass | 69.4 GB / 64.7 GiB | 11.78 min | true |

Interpretation:

- `R=60M` production peak RSS improved by about `48.6%`.
- `R=80M` production peak RSS improved by about `45.5%`.
- `R=60M` production wall time improved by about `13.3%`.
- `R=80M` production wall time improved by about `18.8%`.
- The full-static equality row still peaks near the old memory level because
  the full-static oracle is deliberately materialized. That row is a comparator,
  not the production hot path.

The streaming rows report `max_resident_edges=0`. This does not mean no edges
were processed. It means the resident edge vector was removed from the hot path;
internal and seam edges are applied directly as DSU union operations.

## Why RSS Is Still Tens Of GiB

The current optimized path is still an exact resident DSU over all port atoms in
one resident subband plus incoming carry. It no longer stores the materialized
edge list, but it still stores:

- full resident `coords`;
- full resident `TileOp` vector;
- every decoded port atom as `StaticReachBandAtom`;
- `index_by_id` from stable atom id to dense index;
- DSU parent and rank arrays;
- duplicate-detection byte array;
- face-port maps used to stitch neighboring TileOps;
- outgoing carry partition and reach state;
- temporary `root_reach`, `norm_by_id`, and carry grouping during finish.

The scale is already enough to rule out a 2-3 GiB RSS target for this algorithm
shape:

| Row | Max resident tiles | Max resident port atoms | Max carry atoms | Max components |
|---|---:|---:|---:|---:|
| `R=60M`, streaming production | 3,181,425 | 234,111,163 | 17,579,144 | 4,754,460 |
| `R=80M`, streaming production | 4,241,178 | 318,440,140 | 23,914,160 | 7,020,892 |

Some lower-bound intuition:

- At `R=80M`, port atom ids alone at 8 bytes each are about `2.37 GiB`.
- DSU parent ids alone at 8 bytes each are another about `2.37 GiB`.
- `StaticReachBandAtom` stores id, norm, reach, and carry policy; even before
  allocator overhead this is many GiB for hundreds of millions of atoms.
- `index_by_id` is an `unordered_map` over hundreds of millions of atom ids;
  bucket and node overhead is large by design.
- `TileOp` is locked at 256 bytes. At 4.24M resident tiles, the resident
  TileOp vector alone is about `1.01 GiB`.
- Face-port storage and finish-time maps add more multi-GiB pressure.

Therefore the current result is a successful removal of the materialized edge
graph, not a proof that the path is lightweight enough. The 2-3 GiB expectation
is a different implementation class.

## What Would Be Required For 2-3 GiB

To approach 2-3 GiB at these radii, the hot path must stop owning all resident
port atoms at once. V2 binary packaging or a more compact handoff blob will not
solve process RSS by itself, because the memory is dominated by live DSU/index
tables, not by serialized handoff bytes.

The handoff payload model in `lb-detector-band-stitching-plan.md` is roughly:

```text
16P carry records
+ 8P component atom refs
+ 24-32C component records
```

For the largest latest row, `P=23,914,160` carry atoms and `C=7,020,892`
components imply a serialized handoff on the order of hundreds of MiB, not
50+ GiB. Binary packaging is still important for deterministic checkpointing,
but it cannot remove the live resident atom/index/DSU tables.

The next real algorithmic move is bounded live-state retirement:

```text
stream TileOps in small spatial stripes
-> union only currently active neighboring ports
-> retire components that cannot touch future bands
-> compact the still-live seam/carry frontier
-> emit the same canonical detector handoff
```

Candidate design constraints:

- generate TileOps in chunks/columns instead of allocating a full resident
  subband `coords` and `tileops` vector;
- replace process-wide `unordered_map<AtomId, index>` with dense local ids,
  sort/merge lookup, or stripe-local maps;
- keep face-port state only for adjacent unprocessed neighbors;
- retire non-carry interior components once they can no longer connect to any
  future tile or seam;
- carry only the final guard window and its component partition into the next
  resident stripe;
- preserve the exact seed-policy rule: first global resident can seed
  `INNER_REACHED`, final global resident can seed `OUTER_REACHED`, interior
  residents import reach only from handoff state;
- keep the materialized full-static oracle as a bounded regression gate until
  the retirement path has handoff equality evidence.

This is a larger correctness project than the edge-streaming patch. It needs
explicit invariants for when a port/component is safe to retire and aggressive
handoff-equality gates. Without that retirement proof, reducing RSS by deleting
state would risk silently breaking detector continuation.

## Operational Guidance

- Treat the streaming/resident-width implementation as the current best
  diagnostic production path for large static-reach stitching.
- Do not call it lightweight enough for long unattended runs on small-memory
  hosts.
- Do not spend the next optimization cycle on handoff binary packaging for RSS.
  Packaging matters for checkpoint durability; it is not the current memory
  bottleneck.
- If the next target is 2-3 GiB RSS, open a new branch for a stripe-retirement
  static-reach fold and gate it first against small materialized rows, then the
  `R=60M` full-static handoff equality row.
