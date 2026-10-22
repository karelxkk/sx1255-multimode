# Release Checklist — v1.0.0-rc6

> Legenda: PASS = ověřeno, PENDING = vyžaduje reálný hardware, FAIL = selhání.

| položka | stav | důkaz |
|---|---|---|
| build | **PASS** | `cmake --build build` bez varování (`-Wall -Wextra -Wpedantic`) |
| unit testy | **PASS** | 37 prošlo, 6 skip v sandboxu; HTTP dashboard test mimo sandbox PASS |
| soak test | **PENDING** | vyžaduje 7 dní na Pi Zero 2 W + SX1255 (`scripts/rc1_validate.sh`) |
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
| reálný provoz 7 dní | **PENDING** | rc1-soak-report.md |

## Rozhodnutí

**RC6_READY = true pro release candidate** — softwarové testy, reálný
SX1255/DM-1701 provoz, MER a klidová úspora jsou ověřené. Sedmidenní soak
zůstává otevřenou validační položkou před finálním `v1.0.0`.

Softwarevá část release candidate je připravená a plně buildovatelná/testovatelná;
FM/SVXLink TX, M17 detekce a D-Star hotspot na DCS019 byly ověřeny na reálném
hardwaru. Po dokončení 7denního soak testu lze označit finální `v1.0.0`.
