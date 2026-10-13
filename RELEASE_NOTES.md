# Release Notes — sx1255-multimode v1.0.0-rc1

## Co to je

`sx1255-multimode` je multimódový radioamatérský hotspot/repeater framework pro
Raspberry Pi Zero 2 W a SX1255 HAT. Jeden RF frontend, jeden společný IQ stream
a režimy (FM+CTCSS, D-Star, M17) jako nezávislé pluginy.

## Stav release

**v1.0.0-rc1** — software je funkčně kompletní a buildovatelný, ale **RC1_READY
= false**, dokud neproběhne 7denní validace na reálném hardwaru (SX1255 HAT +
Pi Zero 2 W + reálný provoz). Viz `RELEASE_CHECKLIST.md`.

## Hlavní schopnosti

- Multimódová detekce a arbitráž nad jedním IQ streamem.
- M17 s 1:1 portem DSP z m17-gateway, D-Star detekce, FM s CTCSS.
- IQ capture/replay + automatický capture při podezřelých událostech.
- Spektrum, waterfall a web dashboard v prohlížeči.
- Watchdog s restartem subsystémů, logování, persistované statistiky.
- systemd služba, soak test, benchmark, dataset workflow, false positive audit.

## Instalace

Viz `docs/DEPLOYMENT.md`. Zkráceně:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cp deploy/systemd/sx1255-multimode.service /etc/systemd/system/
sudo systemctl enable --now sx1255-multimode
```

Dashboard pak běží na `http://<ip-pi>:8080/`.

## Známé problémy

- FM plugin má zatím jen detekci CTCSS; kompletní NFM audio/TX řetězec je
  plánován v další iteraci.
- Hardwarová validace na reálném SX1255 je PENDING.

## Autor

Karel Kadlec (OK1LBC) <xkk@post.cz>
