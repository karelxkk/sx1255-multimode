#!/usr/bin/env bash
#
# Post-install smoke test pro nasazený balíček na Raspberry Pi Zero 2 W.
#
#   scripts/deploy_test.sh [--quick|--soak-seconds N]
#
set -uo pipefail

QUICK=0
SOAK=30
while [ $# -gt 0 ]; do
  arg="$1"
  case "$arg" in
    --quick) QUICK=1 ;;
    --soak-seconds) SOAK="${2:-30}"; shift ;;
  esac
  shift
done

FAIL=0
ok(){ printf '  [PASS] %s\n' "$1"; }
bad(){ printf '  [FAIL] %s\n' "$1"; FAIL=1; }

echo "== binárky =="
for b in sx1255-multimode sx1255-soak sx1255-bench sx1255-fp-audit; do
  command -v "$b" >/dev/null && ok "$b" || bad "$b chybí"
done

echo "== konfigurace a služba =="
test -f /etc/sx1255-multimode/sx1255-multimode.yaml && ok "config" || bad "config chybí"
test -f /lib/systemd/system/sx1255-multimode.service && ok "systemd unit" || bad "systemd unit chybí"

echo "== služba =="
systemctl daemon-reload
systemctl is-enabled sx1255-multimode >/dev/null 2>&1 && ok "služba enabled" || bad "služba není enabled"
systemctl restart sx1255-multimode
sleep 3
systemctl is-active sx1255-multimode >/dev/null 2>&1 && ok "služba active" || bad "služba není active"

echo "== HTTP endpointy =="
BASE="http://127.0.0.1:8080"
curl -fsS --max-time 3 "$BASE/api/health" | grep -q '"healthy"' && ok "/api/health" || bad "/api/health"
curl -fsS --max-time 3 "$BASE/api/status" | grep -q '"state"' && ok "/api/status" || bad "/api/status"
curl -fsS --max-time 3 "$BASE/api/system" | grep -q '"memory_rss_kb"' && ok "/api/system" || bad "/api/system"
curl -fsS --max-time 3 "$BASE/api/spectrum" | grep -q '"spectrum"' && ok "/api/spectrum" || bad "/api/spectrum"

echo "== výkon =="
sx1255-bench || bad "sx1255-bench"

echo "== soak ($SOAK s) =="
if [ "$QUICK" -eq 0 ]; then
  sx1255-soak -config /etc/sx1255-multimode/sx1255-multimode.yaml \
    --duration "$SOAK" --out /tmp/soak_deploy.json || bad "sx1255-soak"
  test -s /tmp/soak_deploy.json && ok "soak_report.json" || bad "soak report"
else
  echo "  [SKIP] soak (--quick)"
fi

echo
if [ "$FAIL" -eq 0 ]; then
  echo "VÝSLEDEK: PASS"
  exit 0
else
  echo "VÝSLEDEK: FAIL"
  exit 1
fi
