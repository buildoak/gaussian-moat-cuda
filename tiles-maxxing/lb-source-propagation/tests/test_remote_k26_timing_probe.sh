#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_remote_k26_timing_probe.sh REMOTE_K26_TIMING_PROBE" >&2
  exit 2
fi

probe="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

repo="$tmp/repo"
sidecar="$repo/tiles-maxxing/lb-source-propagation"
verification="$repo/verification"
fake_bin="$tmp/bin"
mkdir -p "$sidecar/scripts" "$verification" "$fake_bin"

cat > "$sidecar/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.22)
project(fake_sidecar CXX)
CMAKE

cat > "$verification/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.22)
project(fake_verification CXX)
CMAKE

cat > "$fake_bin/cmake" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == "-S" ]]; then
  build_dir=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -B)
        build_dir="$2"; shift 2 ;;
      *)
        shift ;;
    esac
  done
  [[ -n "$build_dir" ]] || exit 2
  mkdir -p "$build_dir"
  echo "fake cmake configure $build_dir"
  exit 0
fi
if [[ "$1" == "--build" ]]; then
  build_dir="$2"
  mkdir -p "$build_dir"
  if [[ "$build_dir" == *verify* ]]; then
    cat > "$build_dir/source_dead_gap_check" <<'CHECK'
#!/usr/bin/env bash
echo '{"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS"}'
CHECK
    cat > "$build_dir/source_dead_cert_check" <<'CHECK'
#!/usr/bin/env bash
echo '{"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"}'
CHECK
    chmod +x "$build_dir/source_dead_gap_check" "$build_dir/source_dead_cert_check"
  fi
  echo "fake cmake build $build_dir"
  exit 0
fi
echo "unexpected fake cmake args: $*" >&2
exit 2
SH
chmod +x "$fake_bin/cmake"

cat > "$sidecar/scripts/check_k26_runtime_budget.py" <<'PY'
#!/usr/bin/env python3
print('{"status":"K26_RUNTIME_BUDGET_PASS","claim_grade":false}')
PY
chmod +x "$sidecar/scripts/check_k26_runtime_budget.py"

cat > "$sidecar/scripts/run_k26_full_source_bundle.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
out_dir=""
source_dead_gap_checker=""
source_dead_checker=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-dir)
      out_dir="$2"; shift 2 ;;
    --source-dead-gap-checker)
      source_dead_gap_checker="$2"; shift 2 ;;
    --source-dead-checker)
      source_dead_checker="$2"; shift 2 ;;
    *)
      shift ;;
  esac
done
[[ -n "$out_dir" ]] || exit 2
mkdir -p "$out_dir"
printf '%s\n' "$*" > "$out_dir/harness.remaining-args"
{
  echo "source_dead_gap_checker=$source_dead_gap_checker"
  echo "source_dead_checker=$source_dead_checker"
} > "$out_dir/harness.checkers"
[[ -x "$source_dead_gap_checker" ]] || exit 11
[[ -x "$source_dead_checker" ]] || exit 12
cat > "$out_dir/status.txt" <<'STATUS'
K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM
STATUS
cat > "$out_dir/k26-continuation-progress.jsonl" <<'JSONL'
{"schema":"lb_source_tileop_port_progress_v1","accepted":true,"band_index":0,"r_start":8192,"r_outer":16384,"total_ms":1000,"has_source_carry":true,"terminal_source_dead":false}
JSONL
cat > "$out_dir/k26-continuation-chunks.jsonl" <<'JSONL'
{"schema":"lb_source_k26_continuation_chunk_v1","chunk_index":0}
JSONL
exit 3
SH
chmod +x "$sidecar/scripts/run_k26_full_source_bundle.sh"

out="$tmp/out"
PATH="$fake_bin:$PATH" "$probe" \
  --repo "$repo" \
  --build-dir "$tmp/sidecar-build" \
  --verify-build-dir "$tmp/verify-build" \
  --out-dir "$out" \
  --chunk-bands 1 \
  --timeout-seconds 2 \
  --max-runtime-seconds 10 \
  --tileop-threads 6 \
  > "$tmp/probe.out"

grep -q '^REMOTE_K26_TIMING_PROBE_PASS$' \
  "$out/remote-k26-timing-probe-status.txt"
grep -q 'source_dead_gap_checker=.*verify-build/source_dead_gap_check' \
  "$out/harness.checkers"
grep -q 'source_dead_checker=.*verify-build/source_dead_cert_check' \
  "$out/harness.checkers"
grep -q 'verification_build_dir=.*verify-build' "$out/environment.txt"
grep -q 'K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM' \
  "$out/harness-status-summary.txt"
test -f "$out/verification-cmake-configure.log"
test -f "$out/verification-cmake-build.log"
test -f "$out/k26-runtime-budget-check.manual.log"
grep -q 'remote K26 timing probe artifacts' "$tmp/probe.out"

echo "remote K26 timing probe self-test PASS"
