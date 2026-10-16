# Release Notes — sx1255-multimode v1.0.0-rc4

## Co to je

`sx1255-multimode` je multimódový radioamatérský hotspot/repeater framework pro
Raspberry Pi Zero 2 W a SX1255 HAT. Jeden RF frontend, jeden společný IQ stream
a režimy (FM+CTCSS, D-Star, M17) jako nezávislé pluginy.

## Stav release

**v1.0.0-rc4** — dokončený D-Star řetězec včetně lokálního RX→TX převaděče,
DSRP gatewaye pro ircDDBGateway/DStarGateway a hotspot režimu pro DCS/XLX
reflektory. Dashboard už nezobrazuje confidence v exponenciálním tvaru.

Release obsahuje i instalační assety:

- `rpi-zero2-kernel-6.18.54-myzero2+.deb`
- `genericstereoaudiocodec.dtbo`

## Hlavní schopnosti

- Multimódová detekce a arbitráž nad jedním IQ streamem (M17 > D-Star > FM).
- FM/NFM převaděč s CTCSS a volitelným SVXLink backendem.
- D-Star: GMSK RX, FEC hlavička, lokální RX→TX převaděč, DSRP gateway a
  hotspot linking na DCS/XLX.
- M17: 1:1 port DSP z m17-gateway včetně reflektorového klienta.
- IQ capture/replay, automatický capture, spektrum, waterfall a web dashboard.
- Watchdog, logování, persistované statistiky, soak test a benchmark.

## Instalace

Viz `docs/DEPLOYMENT.md`. Zkráceně:

```text
sudo apt install ./sx1255-multimode_1.0.0-rc4_arm64.deb
sudo systemctl status sx1255-multimode
```

Kernel a overlay se instalují z assetů tohoto release:

```text
sudo apt install ./rpi-zero2-kernel-6.18.54-myzero2+.deb
sudo install -m 0644 genericstereoaudiocodec.dtbo \
  /boot/firmware/overlays/genericstereoaudiocodec.dtbo
```

Dashboard pak běží na `http://<ip-pi>:8080/`.

## D-Star gateway a hotspot

- DSRP gateway běží nad UDP: repeater → gateway na portu 20010, gateway →
  repeater na portu 20011.
- Pro hotspot na DCS019/XLX019 se v DStarGateway používá
  `reflector=DCS019 B` (česká národní skupina).
- Statická poloha hotspotu se nastavuje v `[Gateway]` a `[Repeater_1]`
  (`latitude`/`longitude`) a `[APRS] positionSource=fixed`.

## Známé problémy

- 7denní soak test na reálném hardware je stále PENDING.
- D-Star DV rámce se re-modulují po demodulaci bez AMBE FEC regenerace.
- DStarGateway 0.7 hlásí neškodnou chybu `Cannot find the AMBE index for
  *linkedto*` při linkování; DCS data fungují.

## Autor

Karel Kadlec (OK1LBC) <xkk@post.cz>
