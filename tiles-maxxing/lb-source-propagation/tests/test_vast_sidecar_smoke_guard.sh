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
    echo '[{"id":12345,"dph_total":0.29,"host_id":777},{"id":23456,"dph_total":0.31,"host_id":888}]'
  fi
  exit 0
fi
if [[ "$1" == "create" && "$2" == "instance" ]]; then
  if [[ "$3" == "12345" ]]; then
    echo "{'success': True, 'new_contract': 90001, 'instance_api_key': 'secret-a'}"
  elif [[ "$3" == "23456" ]]; then
    echo "{'success': True, 'new_contract': 90002, 'instance_api_key': 'secret-b'}"
  else
    echo "unexpected offer id: $3" >&2
    exit 98
  fi
  exit 0
fi
if [[ "$1" == "show" && "$2" == "instances" ]]; then
  echo '[{"id":90001,"cur_state":"loading","intended_status":"running","actual_status":null},{"id":90002,"cur_state":"loading","intended_status":"running","actual_status":null}]'
  exit 0
fi
if [[ "$1" == "destroy" && "$2" == "instance" ]]; then
  echo "destroying instance $3."
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
grep -q -- '--max-create-attempts 1' "$tmp/dry-run.log"
grep -q -- '--wait-ssh-seconds 600' "$tmp/dry-run.log"
grep -q 'ACCEPTANCE_CHECK:' "$tmp/dry-run.log"
grep -q 'DRY_RUN_ONLY' "$tmp/dry-run.log"
grep -q '^search offers ' "$VAST_MOCK_LOG"
if grep -q '^create instance ' "$VAST_MOCK_LOG"; then
  echo "dry-run unexpectedly created a Vast instance" >&2
  exit 1
fi

: > "$VAST_MOCK_LOG"
PATH="$tmp/bin:$PATH" "$guard" \
  --exclude-offer-id 12345 \
  --exclude-host-id 777 \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  > "$tmp/excluded.log"
grep -q 'QUALIFYING_OFFER id=23456 dph=0.31' "$tmp/excluded.log"
grep -q 'EXCLUDED_OFFER_IDS 12345' "$tmp/excluded.log"
grep -q 'EXCLUDED_HOST_IDS 777' "$tmp/excluded.log"
grep -q -- '--exclude-offer-id 12345' "$tmp/excluded.log"
grep -q -- '--exclude-host-id 777' "$tmp/excluded.log"

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

: > "$VAST_MOCK_LOG"
set +e
PATH="$tmp/bin:$PATH" "$guard" \
  --execute \
  --run-remote-smoke \
  --destroy-on-exit \
  --max-create-attempts 2 \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  --wait-ssh-seconds 1 \
  --ssh-poll-seconds 1 \
  > "$tmp/retry.log" 2>&1
retry_status="$?"
set -e
if [[ "$retry_status" != "5" ]]; then
  echo "expected retry run to exit with SSH timeout status 5, got $retry_status" >&2
  cat "$tmp/retry.log" >&2
  exit 1
fi
grep -q 'RETRYING_AFTER_SSH_TIMEOUT offer_id=12345 next_attempt=2' "$tmp/retry.log"
grep -q 'EXCLUDED_OFFER_IDS 12345' "$tmp/retry.log"
grep -q '^create instance 12345 ' "$VAST_MOCK_LOG"
grep -q '^create instance 23456 ' "$VAST_MOCK_LOG"
grep -q '^destroy instance 90001 -y' "$VAST_MOCK_LOG"
grep -q '^destroy instance 90002 -y' "$VAST_MOCK_LOG"

echo "vast sidecar smoke guard self-test PASS"
