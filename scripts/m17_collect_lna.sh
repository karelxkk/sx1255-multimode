#!/usr/bin/env bash
#
# One controlled LNA capture group. Must run as root on the SX1255 target.
#
#   m17_collect_lna.sh GAIN SECONDS LABEL
#
set -euo pipefail

GAIN="${1:?usage: m17_collect_lna.sh GAIN SECONDS LABEL}"
DURATION="${2:?usage: m17_collect_lna.sh GAIN SECONDS LABEL}"
LABEL="${3:?usage: m17_collect_lna.sh GAIN SECONDS LABEL}"

case "$GAIN" in
    0|12|24|36|42|48) ;;
    *) echo "unsupported LNA gain: $GAIN" >&2; exit 2 ;;
esac

CONFIG="${SX1255_CONFIG:-/etc/sx1255-multimode/sx1255-multimode.yaml}"
BASE="${SX1255_DATASET_DIR:-/home/xkk/sx1255-diagnostics/datasets}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$BASE/lna${GAIN}"
IQ="$OUTDIR/${LABEL}_${STAMP}.iq"
LOG="$OUTDIR/${LABEL}_${STAMP}.log"
META="$OUTDIR/${LABEL}_${STAMP}.metadata.json"
BACKUP="$(mktemp /tmp/sx1255-config.XXXXXX)"
START_FILE="$OUTDIR/${LABEL}_${STAMP}.start"

mkdir -p "$OUTDIR"
cp "$CONFIG" "$BACKUP"
sed -i -E "s/^([[:space:]]*)lna_gain:.*/\\1lna_gain: ${GAIN}/" "$CONFIG"

systemctl stop sx1255-multimode
systemctl stop svxlink

PID=""
cleanup() {
    set +e
    if [[ -n "$PID" ]] && kill -0 "$PID" 2>/dev/null; then
        kill -TERM "$PID" 2>/dev/null
        wait "$PID" 2>/dev/null
    fi
    cp "$BACKUP" "$CONFIG"
    systemctl restart sx1255-multimode
    systemctl start svxlink
    chown -R xkk:xkk "$OUTDIR"
    rm -f "$BACKUP"
}
trap cleanup EXIT INT TERM

/usr/bin/sx1255-multimode -config "$CONFIG" --capture "$IQ" --log-level info >"$LOG" 2>&1 &
PID=$!
date +%s >"$START_FILE"

sleep "$DURATION"

STATUS="$(curl -s --max-time 3 http://127.0.0.1:8080/api/status || echo '{}')"
DIAG="$(curl -s --max-time 3 http://127.0.0.1:8080/api/m17/diagnostics || echo '{}')"
STATUS="$STATUS" DIAG="$DIAG" GAIN="$GAIN" DURATION="$DURATION" META="$META" \
    python3 - <<'PY'
import json
import os
from datetime import datetime, timezone

try:
    status = json.loads(os.environ.get("STATUS", "{}"))
except json.JSONDecodeError:
    status = {}
try:
    diag = json.loads(os.environ.get("DIAG", "{}"))
except json.JSONDecodeError:
    diag = {}

metadata = {
    "date": datetime.now(timezone.utc).isoformat(),
    "lna_gain_db": int(os.environ["GAIN"]),
    "duration_seconds": int(os.environ["DURATION"]),
    "detected_mode": status.get("mode", ""),
    "detected": status.get("mode") == "M17",
    "rssi_dbfs": diag.get("rssi_at_sync", status.get("rssi", -120)),
    "afc_correction_hz": diag.get("afc_correction_hz", 0),
    "sync_distance": diag.get("sync_distance", 0),
    "expected_mode": "m17",
    "source": "Baofeng DM-1701 / OpenRTX controlled LNA group",
}
with open(os.environ["META"], "w", encoding="utf-8") as handle:
    json.dump(metadata, handle, indent=2)
    handle.write("\n")
PY

echo "capture finished: $IQ"
