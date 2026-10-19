#!/usr/bin/env bash
#
# Compare M17 frame intermediates with the original Go m17-gateway.
#
#   scripts/m17_reference_compare.sh INPUT.iq [OUTPUT.json] [--raw]
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${1:?usage: m17_reference_compare.sh INPUT.iq [OUTPUT.json] [--raw]}"
OUTPUT="${2:-reference_compare.json}"
RAW="${3:-}"
GATEWAY_SRC="${M17_GATEWAY_SRC:-$ROOT/../m17}"

if [[ ! -f "$GATEWAY_SRC/go.mod" ]]; then
    echo "m17-gateway source not found: $GATEWAY_SRC" >&2
    exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP" 2>/dev/null || true' EXIT
CACHE="${M17_AUDIT_CACHE:-/tmp/sx1255-m17-audit-cache}"
mkdir -p "$CACHE/gocache" "$CACHE/gomodcache" "$TMP/gotmp" "$TMP/reference"

cmake --build "$ROOT/build" --target m17-frame-audit
"$ROOT/build/m17-frame-audit" "$INPUT" \
    --output "$TMP/frame_audit.json" \
    --reference-trace "$TMP/trace.jsonl" $RAW

cp "$ROOT/tools/m17_frame_reference.go" "$TMP/reference/main.go"
cat > "$TMP/reference/go.mod" <<EOF
module m17-frame-reference

go 1.26

require github.com/jancona/m17 v0.0.0

replace github.com/jancona/m17 => $GATEWAY_SRC
EOF

(
    cd "$TMP/reference"
    GOCACHE="$CACHE/gocache" GOMODCACHE="$CACHE/gomodcache" \
        GOTMPDIR="$TMP/gotmp" GOFLAGS="-mod=mod" go run . \
        -trace "$TMP/trace.jsonl" \
        -output "$TMP/reference_compare.json"
)

cp "$TMP/reference_compare.json" "$OUTPUT"
python3 "$ROOT/tools/m17_reference_report.py" \
    --compare "$OUTPUT" \
    --output "$ROOT/docs/M17_REFERENCE_COMPARISON.md" \
    --dataset "$INPUT"
echo "reference comparison: $OUTPUT"
