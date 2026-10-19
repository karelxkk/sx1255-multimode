# Release Notes — sx1255-multimode

## v1.0.0-rc5 — M17 packet RX bez ztrát

Příjem M17 packetů (SMS, poloha) je přepracovaný tak, aby žádný packet nezmizel:

- dekodér se vždy znovu synchronizuje na platný LSF (i po přenosu, který
  neskončil koncovou značkou),
- více-rámcové zprávy se skládají podle čísla rámce (dřív fungovaly jen
  jedno-rámcové SMS do ~22 znaků),
- synchronizátor už nemaže buffer při startu/konci relace, takže se neztratí
  rámec následující hned po LSF,
- uvnitř packet přenosu stačí na rozpoznání rámce jediný sync word,
- M17 se dekóduje i během relace jiného módu (FM/D-Star a jejich hang time),
- packety ze sítě čekají na volný kanál ve frontě místo zahození,
- arbitráž vysílače rozlišuje data a audio: packet se nezablokuje jen proto, že
  je „síťový mód“ (network.mode auto) zrovna FM nebo D-Star,
- každý packet, chybné CRC i nepoužitelný rámec je v logu a v telemetrii
  (`packets_rx`, `packets_bad_crc`, `frames_dropped`, `net_packets_*`).
- M17 má RSSI gate `-55 dBFS`; pod ním mód nepřevezme kanál, ale dekodér
  dostává IQ dál, takže se packet nezahazuje.
- Platný LSF s CRC a rozumným volacím znakem přebírá kanál okamžitě, takže
  krátká M17 relace neprohraje s FM.
- Falešné packet hlavičky už nedrží M17 relaci navěky; při timeoutu se čistí
  i `PacketSync`.
- FM používá RSSI squelch s 700ms potvrzením, polyfázový 25k→16k resampler,
  de-emfázi 100 us a audio LPF 2600 Hz.
- Lokální relace mají timeout 300 s; síťový `ACTIVE_SVXLINK` a
  `ACTIVE_M17_NETWORK` se globálním lokálním timeoutem neukončují.

Detaily a ladění prahů: `docs/M17_PACKET_RX.md`.

## Obecné informace

## Co to je

`sx1255-multimode` je multimódový radioamatérský hotspot/repeater framework pro
Raspberry Pi Zero 2 W a SX1255 HAT. Jeden RF frontend, jeden společný IQ stream
a režimy (FM+CTCSS, D-Star, M17) jako nezávislé pluginy.

## Stav release

**v1.0.0-rc5** — dokončený D-Star řetězec včetně lokálního RX→TX převaděče,
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
sudo apt install ./sx1255-multimode_1.0.0-rc5_arm64.deb
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
