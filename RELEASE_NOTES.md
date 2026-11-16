# Release Notes — sx1255-multimode

## v1.0.0-rc7 — stock kernel a čistý sedmidenní soak

RC7 je první release, který na Raspberry Pi Zero 2 W používá oficiální
bezpečnostní Raspberry Pi kernel místo vlastního `myzero2` jádra.

- Ověřený stock kernel: `6.18.60+rpt-rpi-v8`.
- Podpora 125 kSa/s pro SX1255 je v DKMS modulech `sx1255-spdif/1.0`.
- Release už neobsahuje vlastní kernel image ani zranitelný
  `rpi-zero2-kernel-6.18.54-myzero2+.deb`.
- D-Star RX a TX opravy: správná polarita, kanálová šířka, sample-and-hold
  převod 48 na 125 kSa/s a oddělený síťový TX stav.
- D-Star síťový přenos znovu otevírá nový stream po ukončení předchozího
  a nečeká zbytečně 1,6 s.
- DStarBridge potlačuje falešné mikro-talkery `OK1LBC-DS` pomocí
  třírámcového startovního filtru a jednorámcového look-behind bufferu.
- Čistý sedmidenní soak: 168 h, 20 075 vzorků, 0 restartů aplikace,
  0 změn boot ID, stabilní počet 17 vláken.

Release assety:

- `sx1255-multimode_1.0.0-rc7_arm64.deb`
- `sx1255-spdif-1.0.tar.gz`
- `genericstereoaudiocodec.dtbo`

## v1.0.0-rc6 — M17 MER a úspora CPU

Release rc6 zpřesňuje kvalitu M17 příjmu a výrazně snižuje zatížení
Raspberry Pi v době, kdy je kanál klidný:

- M17 falešné LSF/packet syncy uvnitř streamu se už nepočítají jako platné
  rámce a nemohou skokově změnit hlavní BER.
- Při odmítnutí falešného kandidáta se uvnitř otevřeného streamu obnoví celé
  okno, takže se neztratí skutečný hlasový rámec. V klidu se okno nevrací,
  aby se falešný šumový sync nezacyklil a nezastavil RX/FFT.
- Na kontrolním 141s IQ záznamu DM-1701 se počet platných stream rámců zvýšil
  z 2326 na 2398 a počet chybějících rámců klesl ze 17 na 0.
- Dashboard zobrazuje jako hlavní ukazatel **symbol MER**. Ověřená BER
  LSF/paketu zůstává jako doplňková hodnota. MER se exportuje také přes API
  a do rámcového auditu.
- Nový volitelný `power_save` režim pozastaví plugin detektory v `DETECTING`
  pod RSSI prahem. IQ příjem, FFT, waterfall a capture běží dál a první blok
  nad probouzecím prahem okamžitě obnoví detekci.
- Na cíli klesla klidová zátěž z přibližně 52–56 % na 16–17 % CPU.
- Low-IF rotace −90° používá přímou výměnu I/Q složek bez trigonometrie.

Konfigurace klidové úspory:

```yaml
power_save:
  enabled: true
  sleep_below_dbfs: -57.0
  wake_above_dbfs: -55.0
```

Detaily: `docs/POWER_SAVE.md`, `docs/M17_FRAME_AUDIT.md`
a `docs/M17_BER_JUMP_ANALYSIS.md`.

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

**v1.0.0-rc7** — stock kernel, stabilní D-Star streamy a čistý sedmidenní
operational soak. M17 MER dashboard
a klidová CPU úspora. Zachovává D-Star RX→TX převaděč, DSRP gateway
a hotspot režim pro DCS/XLX.

Release obsahuje i instalační assety:

- `rpi-zero2-kernel-6.18.54-myzero2+.deb`
- `genericstereoaudiocodec.dtbo`

> **Bezpečnostní upozornění:** kernel `6.18.54-myzero2+` z tohoto staršího
> release už nepoužívej. Aktuální postup používá oficiální
> `linux-image-rpi-v8` s bezpečnostními opravami a DKMS moduly
> `kernel/sx1255-spdif`.

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
sudo apt install ./sx1255-multimode_1.0.0-rc7_arm64.deb
sudo systemctl status sx1255-multimode
```

Kernel a overlay se instalují podle aktuálního `docs/DEPLOYMENT.md`:

```text
sudo apt install linux-image-rpi-v8 linux-headers-rpi-v8 dkms
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

- První sedmidenní soak běh byl dokončen, ale obsahoval 46 restartů způsobených
  testováním. Čistý PASS zatím není potvrzen; aktuální běh bez plánovaných
  restartů byl spuštěn 2026-11-04 a končí 2026-11-11. Podrobnosti jsou
  v `rc1-soak-report.md`.
- D-Star DV rámce se re-modulují po demodulaci bez AMBE FEC regenerace.
- DStarGateway 0.7 hlásí neškodnou chybu `Cannot find the AMBE index for
  *linkedto*` při linkování; DCS data fungují.

## Autor

Karel Kadlec (OK1LBC) <xkk@post.cz>
