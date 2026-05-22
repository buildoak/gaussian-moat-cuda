# Source Propagation Band Stitching

Updated: 2026-05-22.

## Purpose

The current TileOp campaign answers a static-annulus question:

```text
Does any component connect geo_I to geo_O inside this annulus?
```

That is `ANY-SPAN` / `ANY-SHELL-MOAT` evidence. It is not an origin-component
or source-connected claim.

The lower-bound source-propagation campaign answers a different question:

```text
Given a certified source, does that same source-connected component survive
through this band into the next band?
```

The implementation should reuse the existing TileOp and band machinery as the
local connectivity engine, but it needs a separate protocol for what crosses
between bands.

## Source Model

For squared step bound `K`, let `G_K` be the graph whose vertices are Gaussian
primes and whose edges connect primes at squared Euclidean distance at most
`K`.

For origin mode, add a virtual source vertex `Omega`. `Omega` is adjacent to
every Gaussian prime whose squared norm is at most `K`. The origin component is
the component of `Omega` in this augmented graph.

A source seed is accepted only if it is certified:

- `ORIGIN_SOURCE`: source begins at `Omega` and its first Gaussian-prime
  attachments are checked by the origin rule.
- `WIRED_SOURCE`: source begins from an explicitly declared wired set. This can
  prove wired-source claims, not origin claims.
- `CERTIFIED_SEED`: source begins from a coordinate or frontier that is
  hash-linked to a prior accepted source certificate.

Static `geo_I` membership is not source certification.

## Band And Carry Window

For a radial cut at radius `R`, any edge crossing from one side of the cut to
the other has both endpoints within `sqrt(K)` of the cut. The carry window must
therefore use integer width:

```text
rho = ceil_sqrt(K)
```

For a band ending at `R_j`, the guard/carry window is:

```text
B_j = { p : R_j - sqrt(K) <= |p| <= R_j }
```

Implementations should use exact integer norm predicates for this window, not
floating comparisons.

For non-square `K`, the acceptance-side carry predicate is the conservative
integer shell:

```text
R_j - ceil_sqrt(K) <= |p| <= R_j
```

This may carry extra atoms, but must not under-carry. A proof layer may later
promote a tighter exact irrational-bound predicate only if the verifier and BZ
contract accept it explicitly.

## Separator State

At a seam, the next band must not receive all of `geo_I` as source. It may only
receive the connectivity information proved by the processed prefix.

The separator state is:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component)
```

Where:

- `carry_atoms` are the seam/carry objects that can participate in future
  edges.
- `component_partition` records which carry atoms are already connected inside
  the processed prefix.
- `source_bit_per_component` records which partition classes are connected to
  the declared source.

This is stronger than exporting only source-connected boundary atoms and weaker
than wiring every boundary atom as source. It is the exact information a future
band needs from the past.

`carry_atoms` must be stable, edge-recomputable identifiers. In the first CPU
sidecar they are explicit atom ids with exact norm payloads; in the TileOp
integration they should become stable coordinate or canonical port atoms, not
transient union-find roots, dense component ids, or compacted frontier labels.
The separator partition is canonicalized over these stable ids before it is
persisted or compared.

Reachability only needs `H_i`. Certificates need one more payload: per-class
inventory for already-ingested vertices represented by each carry component.
This payload does not grant new connectivity. It exists so a component that is
neutral at seam `i` but later merges into the source can still contribute its
retired vertices to a terminal certificate.

## Coordinate-To-Port Seam Bridge

The coordinate sidecar and the TileOp port graph use different stable atoms.
Coordinate atoms identify Gaussian-prime coordinates. Port atoms identify
canonical TileOp face/ordinal positions. Moving a prefix manifest from
coordinate atoms into a TileOp-port band is therefore not a free relabeling.

A coordinate carry atom below a radial seam may influence the first TileOp-port
band only through a first-band prime within squared distance `K`. That
first-band prime then bridges to every canonical port atom carrying its
TileOp-visible local component.

The safe diagnostic handoff is hybrid: keep the original coordinate separator
as `incoming`, add bridge edges from coordinate carry atoms to canonical port
atoms in the first TileOp-port band, and let the normal source propagator decide
which port atoms survive into the next carry window. This avoids turning the
handoff into a lossy pre-projection of source state.

This seam bridge is diagnostic until accepted by an explicit seam lemma or
verifier gate. The runner must report:

- coordinate carry atoms consumed from the manifest,
- coordinate carry atoms that bridged to at least one TileOp port,
- coordinate carry atoms that had no first-band port bridge,
- resulting canonical port carry atoms,
- bridge edges inserted between coordinate atoms and port atoms.

A seam bridge can be useful evidence for where source appears to die, but it is
not by itself a `SOURCE_DEAD_CERT`. If the first TileOp-port band reports
source death, the certificate layer must still prove that unbridged coordinate
carry atoms have no future continuation and that the port graph representation
preserves every possible source attachment needed by the terminal guard.

For the diagnostic TileOp-port runner, "unbridged" is not a single logical
state. The runner must distinguish source-connected coordinate carry atoms that:

- bridge into at least one first-band TileOp port;
- have no legal next-band Gaussian-prime candidate within squared distance
  `K`;
- have only dead-end next-band candidates whose local TileOp components have no
  encoded face ports; or
- have an unsafe next-band candidate that could carry source reachability
  farther.

A claim-grade source-death certificate cannot require every coordinate carry
atom to bridge, because that would reject legitimate dead-end source death. It
also cannot ignore bridge failures. The required stop condition is:

```text
source_unbridged_unsafe_candidate_atoms == 0
```

The current K26 bridge evidence is therefore accepted only as non-claim
diagnostic evidence unless a verifier-accepted seam lemma binds the coordinate
carry atoms, TileOp-port components, dead-end classification, terminal
inventory, and BZ schedule into one certificate contract.

## Lemma 1: Local TileOp Equivalence

For a fixed band, the existing TileOp construction and port stitching represent
the same connectivity relation among visible Gaussian-prime components as the
underlying prime graph restricted to that band, subject to the existing TileOp
methodology assumptions.

This source-propagation layer does not modify that local claim. It treats the
current TileOp/band machinery as the local connectivity engine.

## Lemma 2: Separator Sufficiency

Given a processed prefix `P` and an unprocessed suffix `S`, future
source-reachability in `S` depends on `P` only through `H_i`.

Proof sketch:

Any path from the source in `P` to a future vertex in `S` must cross the seam
through a carry atom. The prefix contributes only two facts about seam atoms:
which seam atoms are equivalent through prefix paths, and which of those
equivalence classes are source-connected. These are exactly the partition and
source bits in `H_i`.

## Lemma 3: No-Rewire

Replacing `H_i` with all `geo_I` atoms marked source can create source paths
that do not exist in `G_K`.

Replacing `H_i` with only currently source-marked atoms can destroy future
paths, because non-source carry atoms may later merge with source atoms in the
suffix.

Therefore the correct handoff is the full separator partition plus source bits,
not a boolean span verdict and not a raw boundary set.

## Lemma 4: Composed-Band Equivalence

Processing a large band in one pass is equivalent to processing it as composed
sub-bands if each seam passes the exact separator state `H_i` and each sub-band
uses a carry window of width at least `ceil_sqrt(K)`.

The equivalence check must compare:

- final source reachability,
- source-reached outer/carry atoms,
- separator partition at each cut,
- source bit per separator partition class,
- rejection behavior for malformed or ambiguous seams.

Matching only final `SPANNING` / `MOAT` verdicts is insufficient.

## Lemma 5: Terminal Guard

If, after full guarded ingestion, no source-connected component intersects the
final guard band `[R - sqrt(K), R]`, then no future Gaussian prime with radius
greater than `R` can attach to the source component.

Proof sketch:

Assume a future prime `q` with `|q| > R` first attaches to the processed source
component by an edge to some source-connected prime `p` in the processed region.
Since the edge length is at most `sqrt(K)`, `|p| >= |q| - sqrt(K) > R -
sqrt(K)`. Thus `p` lies in the final guard band, contradicting the assumption
that no source-connected component intersects it.

## Claim Vocabulary

- `ANY-SPAN`: current static-annulus claim; some component connects `geo_I` to
  `geo_O`.
- `ANY-SHELL-MOAT`: current static-annulus full-ingest detector claim; no
  component connects `geo_I` to `geo_O`.
- `CERTIFIED_SEED`: a declared source coordinate/frontier is accepted by an
  origin rule, wired-source declaration, or prior certificate chain.
- `SOURCE_CARRY`: a certified source component reaches the next carry window.
- `SOURCE_TERMINAL`: a detector state where source no longer reaches the next
  carry window and terminal source-component inventory has been emitted.
- `SOURCE_DEAD_CERT`: an accepted terminal proof that source cannot continue
  beyond the guard radius.

`SOURCE_TERMINAL` is not enough for an endpoint claim by itself. Exact endpoint
claims need both a positive source path to the endpoint and a
`SOURCE_DEAD_CERT`.

A `SOURCE_DEAD_CERT` contains two logically separate objects:

1. a negative guard proof: after full guarded ingestion, no source-connected
   class intersects the final guard window; and
2. a positive terminal inventory: the complete source-connected payload retained
   before carry/frontier compaction dropped retired vertices.

The inventory records at least the stable component/member digest, count,
maximum norm, max-coordinate tie set, and enough witness linkage to audit the
claimed endpoint against the certified source chain. If the source merges into a
previously neutral carry class, that class's prior inventory becomes source
inventory at the merge.

## Engineering Consequences

- Geometric `inner_flags` and `outer_flags` are not source flags.
- Source state must not depend on transient compacted component IDs.
- Carry exports need stable carry atoms, their partition, and source bits.
- Terminal mode must inventory retired source components before compaction drops
  them.
- Overflow is a hard reject for source/origin claims. Conservative overflow
  `SPANNING` is valid only as current detector safety behavior.
- Non-square `K` requires exact external BZ evidence until the acceptance layer
  fully promotes non-square BZ support.
- Full-octant/static-annulus closure rules do not automatically transfer to
  arbitrary source claims. Origin mode can use the origin's symmetry, but
  `WIRED_SOURCE` and arbitrary `CERTIFIED_SEED` claims must either process the
  full required domain, carry side-boundary separator state, or state an
  explicit symmetry restriction.

## First Verification Gates

1. Compare 5 to 10 small composed-band fixtures against one big band.
2. Include fixtures for false welding, source-only carry loss, non-source
   partition merges, terminal death, overflow reject, and `K=32` carry width
   `ceil_sqrt(32)=6`.
3. Compare separator partitions and source bits, not only final verdicts.
