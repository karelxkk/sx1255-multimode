# Release Checklist — v1.0.0-rc1

> Legenda: PASS = ověřeno, PENDING = vyžaduje reálný hardware, FAIL = selhání.

| položka | stav | důkaz |
|---|---|---|
| build | **PASS** | `cmake --build build` bez varování (`-Wall -Wextra -Wpedantic`) |
| unit testy | **PASS** | 23/23 (2 HTTP testy SKIP v sandboxu, mimo sandbox PASS) |
| soak test | **PENDING** | vyžaduje 7 dní na Pi Zero 2 W + SX1255 (`scripts/rc1_validate.sh`) |
| watchdog | **PASS** (jednotkově) / **PENDING** (reálný HAT) | `test_health_monitor`, `/api/health` |
| dashboard | **PASS** (jednotkově) | `test_dashboard_json`, `GET /`, `/api/*` |
| capture | **PASS** | `test_iq_recording`, `--capture` |
| replay | **PASS** | `test_iq_replay`, `test_multimode_replay`, `--replay` |
| false positive audit | **PASS** (syntetika) / **PENDING** (reálná data) | `sx1255-fp-audit`, 0 FP |
| failure injection | **PASS** (jednotkově) | `test_failure_injection` |
| systemd | **PENDING** | unit soubor připraven, ověření startu na cíli |
| výkon na Pi Zero 2 W | **PENDING** | `sx1255-bench` na cíli |
| reálný provoz 7 dní | **PENDING** | rc1-soak-report.md |

## Rozhodnutí

**RC1_READY = false** — hardwarové položky (7denní soak, reálná data, reálné
poruchy, systemd na cíli, výkon na Zero 2 W) nelze ověřit bez připojeného
SX1255 HAT a Raspberry Pi Zero 2 W.

Softwarevá část release candidate je připravená a plně buildovatelná/testovatelná;
po doplnění PENDING položek na cílovém zařízení lze RC1_READY nastavit na true.
