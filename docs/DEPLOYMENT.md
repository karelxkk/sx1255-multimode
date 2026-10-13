# Nasazení na Raspberry Pi Zero 2 W

Cílový systém: Raspberry Pi OS (64-bit, arm64), SX1255 HAT, sound karta pro
125 kSa/s stereo IQ (viz `SX1255_HARDWARE.md`).

> **Doporučený postup:** použij připravený deb balíček — viz
> [DEB_PACKAGE.md](DEB_PACKAGE.md). Níže je ruční postup pro vývojáře.

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
