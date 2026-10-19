#!/usr/bin/env bash
#
# Record one labelled DM-1701 / OpenRTX transmission into the dedicated M17
# dataset.  Stop sx1255-multimode first: this script starts its own capture
# instance so the SX1255 is not opened twice.
#
#   scripts/m17_dm1701_record.sh LABEL [SECONDS]
#
# Example:
#   scripts/m17_dm1701_record.sh voice_01 12
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LABEL="${1:?usage: m17_dm1701_record.sh LABEL [SECONDS]}"
DURATION="${2:-12}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$ROOT/datasets/m17/dm1701/${LABEL}_${STAMP}.iq"

mkdir -p "$ROOT/datasets/m17/dm1701"
exec "$ROOT/scripts/record_dataset.sh" m17 "$OUT" "$DURATION"
