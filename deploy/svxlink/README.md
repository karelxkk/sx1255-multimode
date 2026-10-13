# SVXLink FM převaděč (síť SvxLink)

`sx1255-multimode` v režimu `fm.backend: svxlink` funguje jako RF↔audio bridge:

```text
RF RX IQ -> diskriminátor -> de-emfáze -> 16 kHz -> snd-aloop -> SVXLink Rx1
SVXLink Tx1 -> snd-aloop 16 kHz -> pre-emfáze -> FM modulátor -> RF TX IQ
SVXLink PTT (PTY 'T'/'R') -> sx1255-multimode -> SX1255 TX/PTT
```

SVXLink dělá veškerou logiku: squelch, CTCSS, hang time, identifikaci a
ReflectorLogic (připojení do sítě SvxLink). Žádný `svx_ptt_bridge` proces není
potřeba — PTT jde přímo přes PTY.

## Jak se otevírá SQL (squelch)

Otevírá ho **sám SVXLink** ze sub-tónu CTCSS v bridgovaném audiu:

```text
FM + CTCSS 88,5 Hz
  -> multimode FM plugin detekuje CTCSS -> ACTIVE_FM -> spustí audio bridge
  -> snd-aloop: RX audio (i s 88,5 Hz) teče do SVXLink Rx1
  -> SVXLink [Rx1] SQL_DET=CTCSS otevře SQL
  -> RepeaterLogic klíčuje Tx1 -> PTT 'T' na PTY -> multimode klíčuje SX1255
```

CTCSS se tedy vyhodnocuje dvakrát (multimode pro módovou arbitráž + SVXLink
pro squelch). Pokus o jednorázové vyhodnocení s externím squelchem přes PTY
(`SQL_DET=SERIAL`, čtení CTS) **nefunguje** — Linux PTY neimplementuje
modem-control linky (`TIOCMGET` → `ENOTTY`). Pro „jedno CTCSS" je nutný volný
GPIO pin a `SQL_DET=GPIOD` v SVXLinku.

## Instalace

1. Nainstaluj SVXLink a modul `snd-aloop`:

   ```text
   sudo apt-get install -y svxlink
   echo snd-aloop | sudo tee /etc/modules-load.d/snd-aloop.conf
   sudo modprobe snd-aloop
   ```

2. Zkopíruj config a dosaď volací znak/reflector:

   ```text
   sudo mkdir -p /etc/svxlink
   sed -e 's/@REPEATER_CALLSIGN@/OK1LBC/' \
       -e 's/@REFLECTOR_HOSTS@/reflector.example.org:5300/' \
       -e 's/@REFLECTOR_CALLSIGN@/OK1LBC/' \
       -e 's/@SVXLINK_AUTH_KEY@/heslo/' \
       deploy/svxlink/svxlink.conf | sudo tee /etc/svxlink/svxlink.conf
   ```

3. V `sx1255-multimode.yaml` přepni FM backend:

   ```yaml
   fm:
     enabled: true
     backend: svxlink
     svxlink:
       pty: /run/svxlink/ptt
   ```

4. Spusť SVXLink i multimode:

   ```text
   sudo systemctl restart svxlink
   sudo systemctl restart sx1255-multimode
   ```

## Ověření

```text
sudo journalctl -u svxlink -f
sudo journalctl -u sx1255-multimode -f
```

PTT se testuje bez rádia:

```text
printf T | sudo tee /run/svxlink/ptt    # TX on
printf R | sudo tee /run/svxlink/ptt    # TX off
```
