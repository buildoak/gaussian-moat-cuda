# LB Source Propagation Sidecar

This is the Phase 1 CPU sidecar for lower-bound source propagation. It is a
protocol scaffold, not yet a replacement for the existing TileOp/CUDA campaign.

The sidecar models a band handoff as:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component)
```

For certificate inventory it also carries per-component payloads. Those payloads
do not create connectivity; they preserve retired vertices so terminal source
death can report where the source component ended.

## Local Gate

From the repo root:

```bash
cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp
cmake --build /tmp/gm-lbsp -j
ctest --test-dir /tmp/gm-lbsp --output-on-failure
```

The tests cover composed bands versus one big band, false welding,
source-only-carry loss, neutral partition merges, terminal inventory, hard
overflow rejection, `K=32` carry width, and associativity across band groupings.

## Integration Boundary

Phase 2 should feed this protocol from the existing CPU TileOp/band machinery
without changing current static-annulus verdict semantics. Stable carry atoms
must be coordinates or canonical port atoms, not transient union-find roots.
