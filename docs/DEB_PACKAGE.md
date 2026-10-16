# Debian balíček a nasazení

Připravený balíček: `sx1255-multimode_1.0.0-rc4_arm64.deb`

Cílová architektura je **arm64** — Raspberry Pi Zero 2 W s 64bitovým
Raspberry Pi OS (Bookworm/Trixie). Pro 32bitový OS by se balíček stavěl jako
`armhf`.

## Obsah balíčku

```text
/usr/bin/sx1255-multimode        démon
/usr/bin/sx1255-soak             soak test
/usr/bin/sx1255-bench            výkonnostní profil
/usr/bin/sx1255-fp-audit         false positive audit
/etc/sx1255-multimode/sx1255-multimode.yaml   konfigurace (conffile)
/lib/systemd/system/sx1255-multimode.service  systemd služba
/usr/share/sx1255-multimode/scripts/          záznam datasetů, RC1 validace, deploy test
/usr/share/doc/sx1255-multimode/              README, CHANGELOG, RELEASE_NOTES, copyright
```

Po instalaci se služba automaticky **enable** (start po bootu) a spustí se až
po úpravě konfigurace.

## 1. Sestavení balíčku

Na stroji s `dpkg-deb`, `cmake`, `ninja` a `g++`:

```text
scripts/build_deb.sh
```

Výsledek: `sx1255-multimode_1.0.0-rc4_arm64.deb`.

## 2. Přenos na Raspberry Pi Zero 2 W

```text
scp sx1255-multimode_1.0.0-rc4_arm64.deb user@<ip-pi>:~/
```

## 3. Instalace

```text
sudo apt-get install -y ./sx1255-multimode_1.0.0-rc4_arm64.deb
```

Případné chybějící závislosti:

```text
sudo apt-get install -f -y
```

## 4. Konfigurace

```text
sudo nano /etc/sx1255-multimode/sx1255-multimode.yaml
```

Nastav zejména:

- `rx_freq` / `tx_freq`,
- `gpio` piny podle konkrétního HAT (reset/tx_enable/rx_enable/ptt),
- `alsa.capture` / `alsa.playback` (ověř `aplay -l`),
- `automatic_capture.directory` (výchozí `/var/lib/sx1255-multimode`).

## 5. Start služby

```text
sudo systemctl daemon-reload
sudo systemctl restart sx1255-multimode
sudo systemctl status sx1255-multimode
```

Log:

```text
sudo journalctl -u sx1255-multimode -f
```

Dashboard: `http://<ip-pi>:8080/`

## 6. Spuštění testu

Kompletní post-install smoke test (binárky, config, služba, HTTP endpointy,
bench, krátký soak):

```text
sudo /usr/share/sx1255-multimode/scripts/deploy_test.sh
```

Rychlá verze bez soak:

```text
sudo /usr/share/sx1255-multimode/scripts/deploy_test.sh --quick
```

Dlouhý soak (7 dní):

```text
sudo /usr/share/sx1255-multimode/scripts/deploy_test.sh --soak-seconds 604800
```

Individuální testy po instalaci:

```text
sudo systemctl restart sx1255-multimode
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/api/status
sx1255-bench
sx1255-soak -config /etc/sx1255-multimode/sx1255-multimode.yaml --duration 60
```

Regresní replay datasetů:

```text
sudo /usr/share/sx1255-multimode/scripts/record_dataset.sh m17 /tmp/m17.iq 30
/usr/bin/sx1255-multimode --replay /tmp/m17.iq
/usr/bin/sx1255-fp-audit --dataset datasets --out false_positive_report.md
```

## 7. Odinstalace

```text
sudo systemctl disable --now sx1255-multimode
sudo apt-get remove sx1255-multimode
```

Konfigurace `/etc/sx1255-multimode/sx1255-multimode.yaml` se při `remove`
zachová (conffile); smaže se až `apt-get purge`.
