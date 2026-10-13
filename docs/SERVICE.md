# Služba a běh

## systemd

`deploy/systemd/sx1255-multimode.service`:

- spouští se automaticky po bootu (`WantedBy=multi-user.target`),
- po pádu se automaticky restartuje (`Restart=always`, `RestartSec=3`),
- čisté ukončení přes SIGTERM,
- hardening: `ProtectSystem=full`, `NoNewPrivileges`, `PrivateTmp`.

Užitečné příkazy:

```text
sudo systemctl restart sx1255-multimode
sudo journalctl -u sx1255-multimode -f
sudo journalctl -u sx1255-multimode --since today
```

## Logování

Logger zapisuje na konzoli (journal) i do rotujících souborů
`sx1255-multimode.log`, `.log.1`, `.log.2`, `.log.3` (max 2 MB každý).

Úroveň se volí přes `--log-level debug|info|warn|error`.

## Watchdog / HealthMonitor

HealthMonitor periodicky kontroluje frontend, dispatcher, session manager,
HTTP server a spectrum monitor. Při déletrvající poruše komponenty:

1. zaloguje ERROR,
2. spustí IQ dump (pokud je zapnutý `automatic_capture`),
3. restartuje daný subsystém (ne celý proces).

Stav je vidět na `GET /api/health`.

## Restart jednotlivých subsystémů

- HTTP server a spectrum monitor se restartují přímo.
- Frontend se zastaví a znovu otevře (SPI/GPIO/ALSA).
- Dispatcher a session manager jsou bezstavové smyčky; jejich porucha se
  diagnostikuje přes absenci pokroku a řeší restartem frontendu.

## Vypnutí

```text
sudo systemctl stop sx1255-multimode
```

Při vypnutí se uloží `stats.json` a zavřou se SPI/GPIO/ALSA zařízení.
