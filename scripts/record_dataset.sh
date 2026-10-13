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
RSSI="$(echo "$STATUS" | sed -n 's/.*"rssi":\([-0-9]*\).*/\1/p')"
DETECTED="$(echo "$STATUS" | sed -n 's/.*"mode":"\([^"]*\)".*/\1/p')"

kill -TERM "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
trap - EXIT

METADATA="${OUTPUT%.iq}.metadata.json"
cat > "$METADATA" <<EOF
{
  "date": "$(date -Iseconds)",
  "rx_frequency_hz": $(grep -E '^\s*rx_freq' "$CONFIG" | sed -n 's/.*rx_freq:\s*\([0-9]*\).*/\1/p'),
  "rssi_dbm": ${RSSI:--120},
  "detected_mode": "${DETECTED:-unknown}",
  "expected_mode": "${MODE}",
  "duration_seconds": ${SECONDS_REC},
  "source": "SX1255"
}
EOF

echo "zaznamenáno: $OUTPUT"
echo "metadata:     $METADATA"
