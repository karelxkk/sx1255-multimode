#!/usr/bin/env bash
#
# Záznam reálného IQ do datasetu s automatickým metadata.json.
#
#   scripts/record_dataset.sh <mode> <output.iq> [seconds]
#
# <mode>   fm | dstar | m17 | unknown
# <output> cesta k .iq souboru (typicky datasets/<mode>/...)
# [seconds] délka záznamu (výchozí 30)
#
set -euo pipefail

MODE="${1:?usage: record_dataset.sh MODE OUTPUT.iq [SECONDS]}"
OUTPUT="${2:?usage: record_dataset.sh MODE OUTPUT.iq [SECONDS]}"
SECONDS_REC="${3:-30}"
CONFIG="${SX1255_CONFIG:-sx1255-multimode.yaml}"

OUT_DIR="$(dirname "$OUTPUT")"
mkdir -p "$OUT_DIR"

./build/sx1255-multimode -config "$CONFIG" --capture "$OUTPUT" --log-level info &
DAEMON_PID=$!

trap 'kill -TERM "$DAEMON_PID" 2>/dev/null || true' EXIT

sleep "$SECONDS_REC"

STATUS="$(curl -s --max-time 3 http://127.0.0.1:8080/api/status || echo '{}')"
M17DIAG="$(curl -s --max-time 3 http://127.0.0.1:8080/api/m17/diagnostics || echo '{}')"
RSSI="$(echo "$STATUS" | sed -n 's/.*"rssi":\([-0-9]*\).*/\1/p')"
DETECTED="$(echo "$STATUS" | sed -n 's/.*"mode":"\([^"]*\)".*/\1/p')"
read -r AFC SYNC RSSI_AT DETECTED_BOOL < <(
    M17DIAG="$M17DIAG" DETECTED="$DETECTED" python3 - <<'PY'
import json
import os

try:
    diag = json.loads(os.environ.get("M17DIAG", "{}"))
except json.JSONDecodeError:
    diag = {}
print(
    diag.get("afc_correction_hz", 0),
    diag.get("sync_distance", 0),
    diag.get("rssi_at_sync", -120),
    "true" if os.environ.get("DETECTED") == "M17" else "false",
)
PY
)

kill -TERM "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
trap - EXIT

METADATA="${OUTPUT%.iq}.metadata.json"
cat > "$METADATA" <<EOF
{
  "date": "$(date -Iseconds)",
  "rx_frequency_hz": $(grep -E '^\s*rx_freq' "$CONFIG" | sed -n 's/.*rx_freq:\s*\([0-9]*\).*/\1/p'),
  "rssi_dbm": ${RSSI:--120},
  "rssi_dbfs": ${RSSI_AT:--120},
  "afc_correction_hz": ${AFC:-0},
  "sync_distance": ${SYNC:-0},
  "detected": ${DETECTED_BOOL:-false},
  "detected_mode": "${DETECTED:-unknown}",
  "expected_mode": "${MODE}",
  "duration_seconds": ${SECONDS_REC},
  "source": "SX1255"
}
EOF

echo "zaznamenáno: $OUTPUT"
echo "metadata:     $METADATA"
