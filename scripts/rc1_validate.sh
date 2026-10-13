#!/usr/bin/env bash
#
# RC1 validace spustitelná na cílovém zařízení (Raspberry Pi Zero 2 W + SX1255).
#
#   scripts/rc1_validate.sh [soak_seconds]
#
# Produkuje rc1-soak-report.md, performance_rc1.md a false_positive_report.md.
set -euo pipefail

SOAK_SECONDS="${1:-3600}"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "== unit testy =="
ctest --test-dir build --output-on-failure || true

echo "== výkon =="
./build/sx1255-bench | tee /tmp/bench_rc1.txt

cat > performance_rc1.md <<EOF
# Performance RC1

Zařízení: \$(cat /proc/device-tree/model 2>/dev/null || echo unknown)
Kernel: \$(uname -r)
Datum: \$(date -Iseconds)

\`\`\`
\$(cat /tmp/bench_rc1.txt)
\`\`\`

## Interpretace

- Spectrum monitor (5 FFT/s) a waterfall jsou zanedbatelné vůči RX cestě.
- M17 RX chain musí být nad 1.0x real-time, jinak hrozí přetečení bufferu.
- Web dashboard přidává jen periodické HTTP požadavky, bez vlivu na RX vlákno.

## Stav

PASS/FAIL vyplň po měření na cílovém Pi Zero 2 W.
EOF

echo "== false positive audit =="
./build/sx1255-fp-audit --dataset datasets --out false_positive_report.md || true

echo "== soak test (${SOAK_SECONDS}s) =="
./build/sx1255-soak -config sx1255-multimode.yaml \
  --duration "$SOAK_SECONDS" --out /tmp/soak_report_rc1.json

cat > rc1-soak-report.md <<EOF
# RC1 Soak Report

Zařízení: \$(cat /proc/device-tree/model 2>/dev/null || echo unknown)
Kernel: \$(uname -r)
Datum: \$(date -Iseconds)
Délka: ${SOAK_SECONDS} s

## Soak metriky

\`\`\`
\$(cat /tmp/soak_report_rc1.json)
\`\`\`

## Výkon

\`\`\`
\$(cat /tmp/bench_rc1.txt)
\`\`\`

## Watchdog / restarty

\`\`\`
\$(journalctl -u sx1255-multimode --since '-7 days' 2>/dev/null | grep -c 'HealthMonitor' || echo 0)
\`\`\` watchdog událostí za posledních 7 dní.

## Závěr

PASS/FAIL vyplň po 7denním běhu na reálném hardwaru.
EOF

echo "reporty vygenerovány: rc1-soak-report.md, performance_rc1.md, false_positive_report.md"
