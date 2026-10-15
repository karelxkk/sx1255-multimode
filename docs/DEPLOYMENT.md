# Nasazení na Raspberry Pi Zero 2 W

Cílový systém: Raspberry Pi OS (64-bit, arm64), SX1255 HAT, sound karta pro
125 kSa/s stereo IQ (viz `SX1255_HARDWARE.md`).

> **Doporučený postup:** použij připravený deb balíček — viz
> [DEB_PACKAGE.md](DEB_PACKAGE.md). Níže je ruční postup pro vývojáře.

## 0. Kernel a device-tree overlay

SX1255 potřebuje I2S zvukovku s nestandardním vzorkováním **125 000 Hz** a
samostatné I2S/SPI rozhraní. Stockový Raspberry Pi kernel 125 kHz neumí
(stub kodeky `spdif-dir`/`spdif-dit` mají pevné sazby), takže je potřeba:

1. **Kernel s patchem** `0001-ASoC-allow-125000-Hz-CONTINUOUS-for-sx1255_sound.patch`
   (kořen repozitáře). Připravený balíček pro Pi Zero 2 W:

   ```text
   sudo apt install ./rpi-zero2-kernel-6.18.54-myzero2+.deb
   sudo reboot
   ```

   Při vlastním sestavení kernelu aplikuj patch na
   `sound/soc/codecs/spdif_receiver.c` a `spdif_transmitter.c`
   (`SNDRV_PCM_RATE_CONTINUOUS` + `rate_min`/`rate_max`).

2. **Overlay `genericstereoaudiocodec`** (zdroj `genericstereoaudiocodec.dts`
   v kořeni repozitáře; Pi je I2S slave, kodek je bit/frame master):

   ```text
   dtc -@ -H epapr -O dtb -o genericstereoaudiocodec.dtbo \
     -Wno-unit_address_vs_reg genericstereoaudiocodec.dts
   sudo install -m 0644 genericstereoaudiocodec.dtbo \
     /boot/firmware/overlays/genericstereoaudiocodec.dtbo
   ```

3. **Povol rozhraní v `/boot/firmware/config.txt`** (starší obrazy používají
   `/boot/config.txt`):

   ```ini
   dtparam=spi=on
   dtparam=i2s=on
   dtoverlay=genericstereoaudiocodec
   ```

4. Reboot a ověření:

   ```text
   sudo reboot
   aplay -l          # karta "GenericStereoAudioCodec" / GenericStereoAu
   arecord -l
   ```

   Očekávané ALSA zařízení (viz `SX1255_HARDWARE.md`):

   ```text
   playback: hw:GenericStereoAu,0,0
   capture:  hw:GenericStereoAu,1,0
   ```

Pro backend `fm.backend: svxlink` je navíc potřeba virtuální zvukovka
`snd-aloop` — viz [deploy/svxlink/README.md](../deploy/svxlink/README.md).

## 1. Build

```text
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ libasound2-dev
cd sx1255-multimode
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 2. Instalace

```text
sudo mkdir -p /opt/sx1255-multimode
sudo cp -r build sx1255-multimode.yaml.example /opt/sx1255-multimode/
sudo mkdir -p /etc/sx1255-multimode
sudo cp sx1255-multimode.yaml.example /etc/sx1255-multimode/sx1255-multimode.yaml
```

Uprav `/etc/sx1255-multimode/sx1255-multimode.yaml` (frekvence, GPIO, ALSA,
volací znak pro pluginy).

## 3. systemd služba

```text
sudo cp deploy/systemd/sx1255-multimode.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now sx1255-multimode
```

Ověření:

```text
sudo systemctl status sx1255-multimode
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/api/status
```

Dashboard: `http://<ip-pi>:8080/`.

## 4. Trvalé statistiky

`stats.json` se ukládá do pracovního adresáře (`/opt/sx1255-multimode`) při
každém čistém ukončení a načítá se při startu, takže čítače detekcí, relací a
uptime přežijí restart.

## 5. Datasets a regrese

```text
mkdir -p datasets/{fm,dstar,m17,unknown}
./build/sx1255-multimode --capture datasets/m17/qso.iq   # záznam
./build/sx1255-multimode --replay datasets/m17/qso.iq    # přehrání
./build/sx1255-multimode --dataset datasets              # všechny + report
```

Výsledek: `regression_report.json`.
