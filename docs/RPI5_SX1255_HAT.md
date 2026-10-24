# Zprovoznění SX1255 HAT na Raspberry Pi 5 / Raspberry Pi 500

Tento postup vychází z ověřeného zapojení M17 SX1255 HAT na Raspberry Pi 500.
HAT používá 40pinový header, SPI0, I2S na GPIO18-21 a reset na GPIO25.

Oproti Raspberry Pi Zero 2 W se na Pi 5 a Pi 500 změnila cesta k I2S:
`SX1255` je clock master, takže Raspberry Pi musí použít RP1 I2S řadič
v režimu clock consumer. Pokus použít řadič `i2s0` (clock producer) skončí
chybou:

```text
designware-i2s ...: ASoC error (-22): at snd_soc_dai_set_fmt()
asoc-simple-card ...: error -EINVAL: parse error
```

Pro Pi 5 / Pi 500 je proto určen overlay `sx1255-pi5-duplex`, který cílí na
`i2s_clk_consumer` (`i2s@a4000`).

## Ověřená konfigurace

| Součást | Ověřená hodnota |
|---|---|
| Hardware | Raspberry Pi 500 Rev 1.0, BCM2712 + RP1 |
| Operační systém | Debian GNU/Linux 13 (trixie), arm64 |
| Kernel | `6.18.50+rpt-rpi-2712` |
| SPI | `/dev/spidev0.0`, 500 kHz, mode 0 |
| GPIO | `/dev/gpiochip0`, reset GPIO25 |
| SX1255 | verze `0x11` (V1A) |
| ALSA karta | `SX1255-I2S`, ID `SX1255I2S` |
| I2S capture | `hw:SX1255I2S,1,0` |
| I2S playback | `hw:SX1255I2S,0,0` |

## 1. Příprava systému

Nainstaluj nástroje pro kompilaci device-tree overlaye a diagnostiku ALSA:

```bash
sudo apt-get update
sudo apt-get install -y device-tree-compiler alsa-utils gpiod
```

SPI a I2S ověř později po restartu. Pokud v systému není `/boot/firmware`,
použij místo něj `/boot`.

## 2. Kompilace a instalace overlaye

Zdroj overlaye je
[deploy/overlays/sx1255-pi5-duplex.dts](../deploy/overlays/sx1255-pi5-duplex.dts).

```bash
cd sx1255-multimode
dtc -@ -H epapr -O dtb \
  -o /tmp/sx1255-pi5-duplex.dtbo \
  -Wno-unit_address_vs_reg deploy/overlays/sx1255-pi5-duplex.dts
sudo install -m 0644 /tmp/sx1255-pi5-duplex.dtbo \
  /boot/firmware/overlays/sx1255-pi5-duplex.dtbo
```

Overlay vytvoří jednu duplexní ASoC kartu. SX1255 je v obou směrech I2S clock
master a RP1 je clock consumer.

## 3. Boot konfigurace

Nejdřív zálohuj stávající konfiguraci:

```bash
sudo cp -a /boot/firmware/config.txt \
  /boot/firmware/config.txt.bak-sx1255
```

Do sekce `[all]` v `/boot/firmware/config.txt` přidej:

```ini
[all]
dtparam=spi=on
dtparam=i2s=on
dtoverlay=sx1255-pi5-duplex
```

Nepřidávej současně `dtoverlay=genericstereoaudiocodec`. Ten na Pi 5 cílí na
clock-producer řadič a vytvoří konflikt nebo chybu `-EINVAL`.

Potom restartuj:

```bash
sudo reboot
```

## 4. Ověření rozhraní a čipu

Po restartu:

```bash
ls -l /dev/spidev0.0
aplay -l | grep -A3 SX1255
arecord -l | grep -A3 SX1255
gpioinfo /dev/gpiochip0 | grep GPIO25
```

Očekávaný výsledek:

```text
/dev/spidev0.0
card N: SX1255I2S [SX1255-I2S], device 0: ... playback
card N: SX1255I2S [SX1255-I2S], device 1: ... capture
line 25: "GPIO25" input
```

Číslo karty `N` není pevné. V aplikaci proto používej stabilní jméno
`SX1255I2S`, ne číselné `hw:2`.

Přímé SPI ověření lze provést nástrojem ze staršího integračního pracovního
adresáře:

```bash
sudo ./sx1255-spi-fixed -G 0x07
```

Správná odpověď je:

```text
Detected SX1255 chip version V1A - OK
Register 0x07 value: 0x11
```

## 5. Konfigurace multimode

Při použití `.deb` balíčku se konfigurace nachází v:

```text
/etc/sx1255-multimode/sx1255-multimode.yaml
```

Pro ruční instalaci ji zkopíruj ze
[sx1255-multimode.yaml.example](../sx1255-multimode.yaml.example).

Minimální konfigurace hardwaru pro Pi 5 / Pi 500:

```yaml
frontend:
  type: sx1255
  rx_freq: 430975000
  tx_freq: 438575000
  spi:
    device: /dev/spidev0.0
    speed: 500000
  gpio:
    chip: /dev/gpiochip0
    reset: 25
    tx_enable: -1
    rx_enable: -1
    ptt: -1
  alsa:
    capture: hw:SX1255I2S,1,0
    playback: hw:SX1255I2S,0,0
```

Volačky a frekvence uprav podle svého oprávnění:

```yaml
modes:
  dstar:
    callsign: <TVUJ_CALL>
  m17:
    callsign: <TVUJ_CALL>
```

Starší balíčky mohou v `/etc` obsahovat cesty
`hw:GenericStereoAu,...`. Na Pi 5 je změň na:

```text
capture:  hw:SX1255I2S,1,0
playback: hw:SX1255I2S,0,0
```

## 6. Instalace a spuštění služby

Doporučená instalace z balíčku:

```bash
sudo apt-get install -y ./sx1255-multimode_1.0.0-rc6_arm64.deb
```

Alternativa z aktuálního zdrojového stromu:

```bash
sudo cmake --install build
sudo systemctl daemon-reload
```

Před prvním startem zkontroluj volačku, RX/TX frekvence a ALSA cesty. Služba
spouští FM, D-Star i M17 pluginy a může vysílat, jakmile příslušný plugin
získá kanál.

```bash
sudo systemctl enable --now sx1255-multimode
```

Ověření:

```bash
systemctl status sx1255-multimode --no-pager
journalctl -u sx1255-multimode -n 100 --no-pager
curl -s http://127.0.0.1:8080/api/health
curl -s http://127.0.0.1:8080/api/status
```

Dashboard je dostupný na `http://<adresa-pi>:8080/`.

Zdravá služba hlásí `frontend`, `dispatcher`, `http`, `spectrum` a
`session_manager` jako `healthy`. Log `SX1255 ALSA capture: rate=96000` je
na tomto zapojení očekávaný, viz následující část.

## 7. Vzorkovací frekvence a hlášení 96 kHz

Na Pi 5 zůstává vestavěný stub codec `spdif-dir`/`spdif-dit` omezený na
standardní ALSA sazby, proto se při požadavku na 125 kHz může uvést:

```text
SX1255 ALSA capture: rate=96000
SX1255 ALSA playback: rate=96000
```

Samotný SX1255 však dodává BCLK/LRCLK jako I2S master a skutečný tok je
125 kSa/s. Ověřeno na Raspberry Pi 500:

| Nastavení SX1255 | Naměřený tok IQ |
|---|---:|
| 125 kSa/s | přibližně 124,2 kSa/s včetně režie startu |
| 250 kSa/s | přibližně 248,5 kSa/s včetně režie startu |
| 500 kSa/s | přibližně 493,8 kSa/s včetně režie startu |

Naměřené hodnoty jsou mírně nižší kvůli času potřebnému na otevření záznamu.
Pro běžný M17 provoz zůstává 125 kSa/s správná volba. M17 DSP je na této
frekvenci navázaný; vyšší frekvence by vyžadovaly decimaci zpět na 125 kSa/s.

Pro ruční měření zastav službu, aby nebyla ALSA karta obsazená:

```bash
sudo systemctl stop sx1255-multimode
sudo arecord -D hw:SX1255I2S,1 -f S32_LE -c 2 -r 96000 \
  -d 5 /tmp/sx1255-test.wav
stat -c '%s' /tmp/sx1255-test.wav
```

Po testu službu znovu spusť:

```bash
sudo systemctl start sx1255-multimode
```

## 8. Řešení potíží

### Chybí `/dev/spidev0.0`

Ověř, že je v `[all]` sekci `dtparam=spi=on`, a po změně restartuj systém.
Zkontroluj také, že `spidev` není blacklistovaný:

```bash
lsmod | grep -E 'spi|spidev'
dmesg | grep -iE 'spi|spidev'
```

### Karta SX1255-I2S se nevytvořila

```bash
dmesg | grep -iE 'i2s|simple-card|spdif|asoc'
```

Chyba `ASoC error (-22)` u `i2s0` znamená nesprávný overlay. Pro Pi 5 / Pi 500
musí být použit `sx1255-pi5-duplex`, nikoli `genericstereoaudiocodec`.

### Název karty `GenericStereoAu` neexistuje

Na Pi 5 / Pi 500 je název `SX1255I2S`. Oprav obě cesty v konfiguraci a
restartuj službu:

```bash
sudo systemctl restart sx1255-multimode
```

### `M17 LSF CRC fail` v klidovém stavu

Krátké neplatné kandidáty v šumu jsou normální diagnostický výstup. Služba
není poškozená, pokud `/api/health` hlásí `healthy` a v `/api/status` se
objeví `DETECTING`.

### ALSA zařízení je obsazené

Služba drží capture i playback otevřené. Před ručním `arecord`, `aplay` nebo
testovacím nástrojem zastav službu:

```bash
sudo systemctl stop sx1255-multimode
```

## 9. Rollback

Zastav službu:

```bash
sudo systemctl disable --now sx1255-multimode
```

Obnov boot konfiguraci ze zálohy:

```bash
sudo cp -a /boot/firmware/config.txt.bak-sx1255 \
  /boot/firmware/config.txt
```

Volitelně odstraň overlay:

```bash
sudo rm /boot/firmware/overlays/sx1255-pi5-duplex.dtbo
sudo reboot
```

Balíček můžeš odebrat pomocí:

```bash
sudo apt-get remove sx1255-multimode
```
