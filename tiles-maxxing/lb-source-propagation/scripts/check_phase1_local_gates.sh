#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_phase1_local_gates.sh [--repo DIR] [--k-sq N]
                              [--sidecar-build-dir DIR]
                              [--verify-build-dir DIR]
                              [--out-dir DIR]

Run the local pre-Vast Phase 1 LB source-propagation gates from one command:
git diff whitespace check, Phase 1 diff-scope guard, fresh sidecar CMake/CTest,
fresh independent verification CMake/CTest, and local remote-smoke artifact
acceptance. This script performs no Vast API actions and starts no long K26 run.

Defaults:
  --repo              current git repository root
  --k-sq              26
  --sidecar-build-dir fresh /tmp directory
  --verify-build-dir  fresh /tmp directory
  --out-dir           fresh /tmp directory
USAGE
}

repo_dir=""
k_sq="26"
sidecar_build_dir=""
verify_build_dir=""
out_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo_dir="$2"
      shift 2
      ;;
    --k-sq)
      k_sq="$2"
      shift 2
      ;;
    --sidecar-build-dir)
      sidecar_build_dir="$2"
      shift 2
      ;;
    --verify-build-dir)
      verify_build_dir="$2"
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
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

if [[ ! "$k_sq" =~ ^[0-9]+$ || "$k_sq" == "0" ]]; then
  echo "--k-sq must be a positive integer" >&2
  exit 2
fi

sidecar_dir="$repo_dir/tiles-maxxing/lb-source-propagation"
if [[ ! -f "$sidecar_dir/CMakeLists.txt" ]]; then
  echo "sidecar CMakeLists.txt not found at $sidecar_dir" >&2
  exit 2
fi

if [[ -z "$sidecar_build_dir" ]]; then
  sidecar_build_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-local-sidecar.XXXXXX")"
fi
if [[ -z "$verify_build_dir" ]]; then
  verify_build_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-local-verify.XXXXXX")"
fi
if [[ -z "$out_dir" ]]; then
  out_dir="$(mktemp -d "${TMPDIR:-/tmp}/gm-lbsp-local-gates.XXXXXX")"
fi

mkdir -p "$out_dir"

{
  echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "repo=$repo_dir"
  echo "branch=$(git -C "$repo_dir" branch --show-current 2>/dev/null || echo unknown)"
  echo "head=$(git -C "$repo_dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "k_sq=$k_sq"
  echo "sidecar_build_dir=$sidecar_build_dir"
  echo "verify_build_dir=$verify_build_dir"
  echo "out_dir=$out_dir"
} > "$out_dir/environment.txt"

git -C "$repo_dir" diff --check | tee "$out_dir/git-diff-check.log"
"$sidecar_dir/scripts/check_phase1_diff_scope.sh" \
  | tee "$out_dir/phase1-diff-scope.log"

"$sidecar_dir/scripts/remote_sidecar_smoke.sh" \
  --repo "$repo_dir" \
  --build-dir "$sidecar_build_dir" \
  --verify-build-dir "$verify_build_dir" \
  --k-sq "$k_sq" \
  --out-dir "$out_dir/remote-smoke" \
  | tee "$out_dir/remote-sidecar-smoke.log"

cat > "$out_dir/status.txt" <<'STATUS'
PHASE1_LOCAL_GATES_PASS
Scope: git diff whitespace check, Phase 1 diff-scope guard, local sidecar CMake/CTest, independent verification CMake/CTest, and remote-smoke artifact acceptance.
Non-claim: this is a local pre-Vast gate, not a sqrt(26) source/origin run and not a moat result.
STATUS

echo "PHASE1_LOCAL_GATES_PASS out_dir=$out_dir"
