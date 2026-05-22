#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_phase1_diff_scope.sh [--base REF] [--head REF]

Validate that the Phase 1 LB source-propagation branch diff is confined to the
new sidecar plus the explicit source verifier/schema exceptions named in
reference/lb-source-propagation-goal-plan-20260522.md.

Defaults:
  --base  2720059
  --head  HEAD
USAGE
}

base_ref="${LB_SOURCE_DIFF_BASE:-2720059}"
head_ref="${LB_SOURCE_DIFF_HEAD:-HEAD}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base)
      base_ref="$2"
      shift 2
      ;;
    --head)
      head_ref="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

repo_root="$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "PHASE1_DIFF_SCOPE_REJECT: not inside a git repository" >&2
  exit 2
}
cd "$repo_root"

if ! git rev-parse --verify --quiet "${base_ref}^{commit}" >/dev/null; then
  echo "PHASE1_DIFF_SCOPE_REJECT: base ref not found: $base_ref" >&2
  exit 2
fi
if ! git rev-parse --verify --quiet "${head_ref}^{commit}" >/dev/null; then
  echo "PHASE1_DIFF_SCOPE_REJECT: head ref not found: $head_ref" >&2
  exit 2
fi

merge_base="$(git merge-base "$base_ref" "$head_ref")"
head_short="$(git rev-parse --short "$head_ref")"
bad_paths=()
changed_count=0

while IFS= read -r path; do
  [[ -z "$path" ]] && continue
  changed_count=$((changed_count + 1))
  case "$path" in
    tiles-maxxing/lb-source-propagation/* | \
    methodology/source-propagation-band-stitching.md | \
    reference/lb-source-propagation-goal-plan-20260522.md | \
    verification/CMakeLists.txt | \
    verification/README.md | \
    verification/fixtures/source_prop/* | \
    verification/schemas/*source* | \
    verification/src/source_prop_oracle.cpp | \
    verification/src/source_dead_cert_check.cpp | \
    verification/src/source_dead_gap_check.cpp)
      ;;
    *)
      bad_paths+=("$path")
      ;;
  esac
done < <(git diff --name-only "$merge_base" "$head_ref")

if [[ "${#bad_paths[@]}" -ne 0 ]]; then
  echo "PHASE1_DIFF_SCOPE_REJECT base=${merge_base} head=${head_short}" >&2
  printf 'unexpected path: %s\n' "${bad_paths[@]}" >&2
  exit 1
fi

echo "PHASE1_DIFF_SCOPE_PASS base=${merge_base} head=${head_short} files=${changed_count}"
