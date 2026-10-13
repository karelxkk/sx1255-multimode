# Failure Injection

Oveřuje, že watchdog detekuje poruchu, restartuje subsystém a provede IQ dump.

## Automatizované (bez hardwaru, PASS)

`test_failure_injection` ověřuje:

- **zaplnění capture bufferu** — ring přeteče bez pádu a trigger stále vyprodukuje
  validní pre+post dump,
- **neplatný IQ stream** — prázdný blok (0 vzorků) dispatcher zpracuje bez pádu,
- **watchdog restart** — `test_health_monitor` ověřuje restart komponenty po
  překročení prahu a volání problem handleru (IQ dump).

```text
ctest --test-dir build -R 'failure_injection|health_monitor|capture_trigger'
```

## Hardwarové poruchy (PENDING, postup na cíli)

| porucha | jak simulovat | očekávané chování |
|---|---|---|
| ztráta ALSA | zavři zvukové zařízení / odpoj sound kartu | frontend restart, `/api/health` frontend=false, IQ dump |
| ztráta SPI | odpoj HAT / `raspi-config` vypni SPI | frontend restart, log ERROR |
| chyba GPIO | vyjmi gpiochip / špatný line v configu | `open()` selže, proces se ukončí, systemd restart |
| neplatný IQ stream | přehraj poškozený `.iq` přes `--replay` | detekce nehlásí, `samples_processed` roste, bez pádu |
| zaplnění capture bufferu | `test_failure_injection` | PASS (ověřeno) |

## Ověření watchdog / IQ dumpu

```text
curl http://127.0.0.1:8080/api/health
sudo journalctl -u sx1255-multimode -f | grep HealthMonitor
ls automatic_capture.directory/capture_*.iq
```

## Stav

- [x] neplatný IQ stream (unit test) PASS
- [x] zaplnění capture bufferu (unit test) PASS
- [x] watchdog restart + IQ dump (unit test) PASS
- [ ] ztráta ALSA / SPI / GPIO na reálném HAT (PENDING)
