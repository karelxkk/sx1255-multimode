# SVXLink FM převaděč (síť SvxLink)

`sx1255-multimode` v režimu `fm.backend: svxlink` funguje jako RF↔audio bridge:

```text
RF RX IQ -> diskriminátor -> de-emfáze -> 16 kHz -> snd-aloop -> SVXLink Rx1
SVXLink Tx1 -> snd-aloop 16 kHz -> pre-emfáze -> FM modulátor -> RF TX IQ
SVXLink PTT (PTY 'T'/'R') -> sx1255-multimode -> SX1255 TX/PTT
```

SVXLink dělá logiku převaděče (hang time, identifikaci, ReflectorLogic a
připojení do sítě SvxLink); nosnou detekuje multimode šumově. Žádný
`svx_ptt_bridge` proces není potřeba — PTT i squelch jdou přímo přes PTY.

## Jak se otevírá SQL (squelch)

Squelch otevírá **multimode** šumovou detekcí nosné:

```text
FM nosná (klidně i bez subtónu)
  -> multimode FM plugin změří poměr šumu z diskriminátoru (squelch_noise_ratio)
  -> nosná utiší diskriminátor -> SQL PTY 'O'
  -> snd-aloop: RX audio teče do SVXLink Rx1
  -> SVXLink [Rx1] SQL_DET=PTY otevře SQL
  -> RepeaterLogic klíčuje Tx1 -> PTT 'T' na PTY -> multimode klíčuje SX1255
```

CTCSS se na RX už nevyhodnocuje: multimode detekuje pouze nosnou a SVXLink
čte stav squelche z PTY (`SQL_DET=PTY`, `PTY_PATH=/run/svxlink/sql`). Proto
na RX funguje i stanice bez subtónu. CTCSS na TX zůstává volitelné, viz níže.

## CTCSS na TX

V backendu `svxlink` se `ctcss_tx` z multimode konfigurace nepoužívá. Tón na
vysílání generuje SVXLink:

```ini
[RepeaterLogic]
TX_CTCSS=ALWAYS

[ReflectorLogic]
TX_CTCSS=ALWAYS

[Tx1]
CTCSS_FQ=88.5
CTCSS_LEVEL=-24
```

`TX_CTCSS` se nastavuje v logice, `CTCSS_FQ`/`CTCSS_LEVEL` v sekci vysílače.

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
