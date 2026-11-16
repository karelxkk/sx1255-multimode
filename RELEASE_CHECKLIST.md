# Release Checklist — v1.0.0-rc7

> Legenda: PASS = ověřeno, PENDING = vyžaduje reálný hardware, FAIL = selhání.

| položka | stav | důkaz |
|---|---|---|
| build | **PASS** | `cmake --build build` bez varování (`-Wall -Wextra -Wpedantic`) |
| unit testy | **PASS** | 43 prošlo, 1 sandbox-only PTY test skipped |
| soak test | **PASS** | 168 h, 20 075 vzorků, 0 restartů, 0 boot změn (`rc3-soak-report.md`) |
| watchdog | **PASS** (jednotkově) / **PENDING** (reálný HAT) | `test_health_monitor`, `/api/health` |
| dashboard | **PASS** | `test_dashboard_json`, MER + ověřená BER, `power_save` v `/api/status` |
| capture | **PASS** | `test_iq_recording`, `--capture` |
| replay | **PASS** | `test_iq_replay`, `test_multimode_replay`, `--replay` |
| false positive audit | **PASS** (syntetika) / **PENDING** (reálná data) | `sx1255-fp-audit`, 0 FP |
| failure injection | **PASS** (jednotkově) | `test_failure_injection` |
| systemd | **PASS** | `deploy_test.sh` + ověření na Pi Zero 2 W (`active`) |
| výkon na Pi Zero 2 W | **PASS** | klidová zátěž 52–56 % → 16–17 % CPU |
| M17 stream regrese | **PASS** | 141s DM-1701 IQ: 2398 rámců, 0 mezer |
| klidové probuzení | **PASS** (integrační test) | dispatcher práh + hystereze + okamžité probuzení |
| reálný provoz 7 dní | **PASS** | rc3-soak-report.md |
| stock kernel + DKMS | **PASS** | kernel `6.18.60+rpt-rpi-v8`, `sx1255-spdif/1.0`, 125 kSa/s |

## Rozhodnutí

**RC7_READY = true pro release candidate** — softwarové testy, reálný
SX1255/DM-1701 provoz, MER, klidová úspora a čistý sedmidenní soak jsou
ověřené.

Softwarevá část release candidate je připravená a plně buildovatelná a
testovatelná. FM/SVXLink TX, M17 detekce, D-Star hotspot na DCS019, stock
kernel s DKMS a čistý sedmidenní soak byly ověřeny na reálném hardwaru.
