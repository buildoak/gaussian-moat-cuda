# Gaussian Moat CUDA

CUDA/C++ implementation work for Gaussian moat computation.

Current status has two active-but-separate fronts.

The accepted baseline is still static-annulus evidence. The compact static
verification spine accepts campaign evidence through four gates only: Exact
Profile, Independent Tile Sample, SPANNING Cert, and MOAT Hardening. Current
K38/K40 rows remain local `ANY-SPAN` / `ANY-SHELL-MOAT` detector evidence, not
origin-component moat proofs.

The active lower-bound front is source propagation. It carries a certified
source signal across stacked bands by exact frontier handoff:

```text
H_i = carry_atoms + component_partition + source_bit_per_component
```

The LB hot path should reuse TileOp as the local connectivity oracle, stitch
frontier state between bands, and fine-ify only near the last-live /
first-dead transition. Static-reach, high-radius, overnight, and K26 bundle
machinery are diagnostic/proof-debt surfaces unless explicitly promoted by an
independent gate.

The current K36 hardening anchor is the full-ingest matrix at
`R_inner=80,000,000`, especially the `17k` to `20k` widths, with a wider
`32,768` confirmation row. All current K36 matrix rows postflight as `MOAT`
with exact BZ, zero overflow, stats_v2 telemetry, and persisted 512-tile sample
audit. The adjacent Tsuchimura pair remains useful as a calibration note:
`R_outer=80,015,782` is `SPANNING`; `R_outer=80,015,790` is `MOAT`.

On an RTX 4090, the current full CUDA pipeline runs at about `40k tiles/s`
end-to-end. The GPU TileOp stage is around `70k tiles/s`; the full pipeline is
currently bounded by CUDA work plus CPU streaming composition.

Paper writeup, further performance optimization, and larger moat campaigns are
work in progress. K37-K39 rows are telemetry-only until promoted through the
compact spine.

See:

- `AGENTS.md` for project rules and correctness hierarchy.
- `methodology/source-propagation-band-stitching.md` for the LB source/frontier
  protocol.
- `tiles-maxxing/lb-source-propagation/README.md` for current LB implementation
  surfaces, gates, and non-claim boundaries.
- `tiles-maxxing/lb-source-propagation/docs/lb-handoff-redesign.md` for the
  live handoff and last-band refinement design.
- `tiles-maxxing/lb-source-propagation/docs/tile-frontier-streaming-redesign.md`
  for the partial streaming implementation contract.
- `reference/current-verification-spine.md` for static-annulus verification gates.
- `reference/attached-static-annulus-moats.md` for attached lower-K36 and
  local K34 static-annulus moat evidence.
- `reference/k26-static-annulus-diagnostics.md` for the K26 Tsuchimura-endpoint
  static-annulus diagnostic archive; it is not the active LB source-propagation
  first-read path.
- `methodology/tile-operator-definition-v-claude.md` for the mathematical
  implementation contract.
