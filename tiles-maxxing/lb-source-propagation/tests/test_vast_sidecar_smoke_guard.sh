#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: test_vast_sidecar_smoke_guard.sh GUARD" >&2
  exit 2
fi

guard="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin"

cat > "$tmp/bin/vastai" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
echo "$*" >> "$VAST_MOCK_LOG"
if [[ "$1" == "search" && "$2" == "offers" ]]; then
  if [[ "${VAST_MOCK_NO_OFFERS:-0}" == "1" ]]; then
    echo '[]'
  else
    echo '[{"id":12345,"dph_total":0.29}]'
  fi
  exit 0
fi
echo "unexpected vastai call: $*" >&2
exit 97
SH
chmod +x "$tmp/bin/vastai"

export VAST_MOCK_LOG="$tmp/vast.log"
PATH="$tmp/bin:$PATH" "$guard" \
  --run-remote-smoke \
  --destroy-on-exit \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  > "$tmp/dry-run.log"

grep -q 'QUALIFYING_OFFER id=12345 dph=0.29' "$tmp/dry-run.log"
grep -q 'ONE_SHOT_REMOTE_SMOKE:' "$tmp/dry-run.log"
grep -q -- '--run-remote-smoke' "$tmp/dry-run.log"
grep -q -- '--destroy-on-exit' "$tmp/dry-run.log"
grep -q -- '--wait-ssh-seconds 600' "$tmp/dry-run.log"
grep -q 'ACCEPTANCE_CHECK:' "$tmp/dry-run.log"
grep -q 'DRY_RUN_ONLY' "$tmp/dry-run.log"
grep -q '^search offers ' "$VAST_MOCK_LOG"
if grep -q '^create instance ' "$VAST_MOCK_LOG"; then
  echo "dry-run unexpectedly created a Vast instance" >&2
  exit 1
fi

: > "$VAST_MOCK_LOG"
if PATH="$tmp/bin:$PATH" VAST_MOCK_NO_OFFERS=1 "$guard" \
    --max-dph 0.37 \
    --max-budget 1.50 \
    --k-sq 26 \
    > "$tmp/no-offer.log" 2>&1; then
  echo "guard accepted an empty offer list" >&2
  exit 1
fi
grep -q 'NO_QUALIFYING_OFFER' "$tmp/no-offer.log"
grep -q '^search offers ' "$VAST_MOCK_LOG"

echo "vast sidecar smoke guard self-test PASS"
