# Sedmidenní test reálného provozu

Operational soak monitoruje běžící službu `sx1255-multimode` bez zásahu do
jejího RX/TX provozu. Nezaklíčuje PTT, nemění konfiguraci a nečte IQ.

## Co se měří

Každých 30 sekund se ukládá:

- stav služby, PID, systemd restarty a boot ID,
- stav `DETECTING` / aktivní režim a síťový režim,
- RSSI, `power_save`, FFT frame a noise floor,
- health stav všech komponent,
- CPU sekundy, RSS paměť, vlákna a kontextové přepínače,
- volná RAM, swap, teplota a místo na disku,
- detekce a relace FM / D-Star / M17,
- M17 telemetrie včetně BER/MER, CRC, Golay, Viterbi a reflektoru.

Data se zapisují do `/var/lib/sx1255-operational-soak/`:

```text
samples.jsonl       jeden JSON objekt na vzorek
session.json        persistentní stav přes restart/reboot
last_sample.json    poslední vzorek
summary.json        průběžný souhrn
report.md           čitelný průběžný a závěrečný report
```

## Instalace a start

```bash
sudo install -m 0755 scripts/operational_soak_monitor.py \
  /usr/local/sbin/sx1255-operational-soak
sudo install -m 0644 deploy/systemd/sx1255-operational-soak.service \
  /etc/systemd/system/sx1255-operational-soak.service
sudo systemctl daemon-reload
sudo systemctl enable --now sx1255-operational-soak.service
```

Test začne okamžitě a trvá 604800 sekund. Stav je uložený, takže po restartu
služby nebo celého Raspberry Pi pokračuje ve stejném sedmidenním okně.

## Kontrola

```bash
systemctl status sx1255-operational-soak --no-pager
cat /var/lib/sx1255-operational-soak/report.md
cat /var/lib/sx1255-operational-soak/summary.json
tail -n 5 /var/lib/sx1255-operational-soak/samples.jsonl
```

## Vyhodnocení

Po skončení se v `summary.json` doplní počty chyb z journalu a `completed=true`.
Za úspěšný test se považuje běh po celou dobu bez:

- neočekávaného restartu aplikace,
- trvale nezdravé komponenty,
- rostoucího trendu RSS nebo počtu vláken,
- opakovaných HTTP/API selhání.

Krátké ojedinělé chyby se nesmí skrývat; zůstávají v `samples.jsonl` a
v souhrnných počtech.
