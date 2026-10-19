#!/usr/bin/env bash
#
# Compare the AFC trajectory of sx1255-multimode with the original Go
# m17-gateway on identical 125 kSa/s IQ.
#
#   scripts/m17_afc_audit.sh INPUT.iq [OUTPUT.json] [STRIDE] [--raw]
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${1:?usage: m17_afc_audit.sh INPUT.iq [OUTPUT.json] [STRIDE]}"
OUTPUT="${2:-afc_audit.json}"
STRIDE="${3:-25}"
RAW="${4:-}"
GATEWAY_SRC="${M17_GATEWAY_SRC:-$ROOT/../m17}"

if [[ ! -d "$GATEWAY_SRC/modem" ]]; then
    echo "m17-gateway source not found: $GATEWAY_SRC" >&2
    echo "set M17_GATEWAY_SRC to the original m17-gateway checkout" >&2
    exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP" 2>/dev/null || true' EXIT
AUDIT_CACHE="${M17_AUDIT_CACHE:-/tmp/sx1255-m17-audit-cache}"

cmake --build "$ROOT/build" --target m17_afc_audit
"$ROOT/build/m17_afc_audit" "$INPUT" \
    --baseband "$TMP/baseband.f64" \
    --csv "$TMP/cpp_afc.csv" \
    --summary "$TMP/cpp_summary.json" \
    --stride "$STRIDE" $RAW

mkdir -p "$TMP/reference"
mkdir -p "$AUDIT_CACHE/gocache" "$AUDIT_CACHE/gomodcache" "$TMP/gotmp"
cp "$ROOT/tools/m17_afc_reference.go" "$TMP/reference/main.go"
cp "$GATEWAY_SRC/go.sum" "$TMP/reference/go.sum"
cat > "$TMP/reference/go.mod" <<EOF
module m17-afc-reference

go 1.26

require github.com/jancona/m17 v0.0.0

replace github.com/jancona/m17 => $GATEWAY_SRC

replace github.com/yobert/alsa => github.com/jancona/alsa v0.0.0-20260224125037-816157b04153
EOF

(
    cd "$TMP/reference"
    GOCACHE="$AUDIT_CACHE/gocache" GOTMPDIR="$TMP/gotmp" \
        GOMODCACHE="$AUDIT_CACHE/gomodcache" \
        GOFLAGS="-mod=mod" go run . \
        -input "$TMP/baseband.f64" \
        -output "$TMP/reference_afc.csv" \
        -stride "$STRIDE"
)

DECIMATED="$(sed -n 's/.*"decimated_samples":\([0-9]*\).*/\1/p' "$TMP/cpp_summary.json")"
CPP_COMMIT="$(git -C "$ROOT" rev-parse --short=12 HEAD 2>/dev/null || true)"
GATEWAY_COMMIT="$(git -C "$GATEWAY_SRC" rev-parse --short=12 HEAD 2>/dev/null || true)"

python3 "$ROOT/tools/m17_afc_compare.py" \
    --cpp "$TMP/cpp_afc.csv" \
    --reference "$TMP/reference_afc.csv" \
    --output "$OUTPUT" \
    --input "$INPUT" \
    --stride "$STRIDE" \
    --decimated-samples "${DECIMATED:-0}" \
    --cpp-commit "$CPP_COMMIT" \
    --gateway-commit "$GATEWAY_COMMIT"

echo "AFC audit: $OUTPUT"
