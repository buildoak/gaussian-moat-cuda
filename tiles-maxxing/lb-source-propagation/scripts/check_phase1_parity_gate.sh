#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_phase1_parity_gate.sh [--repo DIR] [--verify-build-dir DIR]

Run the narrow pre-Vast source-propagation parity gate:
git diff whitespace check, source-prop fixture schema contract, a fresh
verification build, and the 5/10/20 stitched-band-vs-big-band oracle tests.

This script performs no Vast API actions, no remote smoke, no K26 run, and no
certificate bundle acceptance.
USAGE
}

repo_dir=""
verify_build_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
      shift 2
      ;;
    --verify-build-dir)
      verify_build_dir="$2"
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

if [[ -z "$repo_dir" ]]; then
  repo_dir="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "not inside a git repository; pass --repo DIR" >&2
    exit 2
  }
fi
repo_dir="$(cd "$repo_dir" && pwd)"

verification_dir="$repo_dir/verification"
if [[ ! -f "$verification_dir/CMakeLists.txt" ]]; then
  echo "verification CMakeLists.txt not found at $verification_dir" >&2
  exit 2
fi

if [[ -z "$verify_build_dir" ]]; then
  verify_build_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-parity-verify.XXXXXX")"
fi

git -C "$repo_dir" diff --check
python3 "$verification_dir/test_source_prop_schema_contract.py"

cmake -S "$verification_dir" -B "$verify_build_dir"
cmake --build "$verify_build_dir" --target source_prop_oracle -j
ctest --test-dir "$verify_build_dir" \
  -R 'source_prop_fixture_composed-(vs-big|ten-vs-big|twenty-vs-big)-equivalence|source_prop_schema_contract_py' \
  --output-on-failure

echo "PHASE1_PARITY_GATE_PASS verify_build_dir=$verify_build_dir"
