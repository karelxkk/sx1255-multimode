# Session Manager

## Stavy

```
IDLE -> DETECTING -> ACTIVE_FM | ACTIVE_DSTAR | ACTIVE_M17
                          |
                          +-> TIMEOUT -> RETURN_TO_IDLE -> IDLE
                          |
ACTIVE_* (normální konec) -> RETURN_TO_IDLE -> IDLE

DETECTING/IDLE <-> ACTIVE_SVXLINK | ACTIVE_M17_NETWORK   (arbiter-only TX)
```

| Stav | Význam |
|---|---|
| `Idle` | klid, čekání na start |
| `Detecting` | monitoring, detekce u všech pluginů |
| `ActiveFm` / `ActiveDStar` / `ActiveM17` | exkluzivní vlastnictví |
| `ActiveSvxlink` | TX drží SVXLink bridge (dojezd, ID, reflector) |
| `ActiveM17Network` | TX drží M17 síťový bridge (reflector -> RF) |
| `Timeout` | relace překročila limit |
| `ReturnToIdle` | odklad před návratem do Idle (grace period) |

## Arbitráž

Pouze jeden plugin smí být aktivní. Při monitoringu se seberou detekce
všech pluginů, seřadí se podle priority a první s `detected=true` vyhrává.
Pokud je relace aktivní, další detekce se ignorují.

## Arbiter TX (ITxArbiter)

`SessionManager` je jediná autorita pro přidělení vysílače. Vedle plugin
session (FM/D-Star/M17) existují dva „arbiter-only" stavy, kdy TX drží
entita, která není plugin:

- `ActiveSvxlink` — žádost z FM bridge (`acquireTx(Mode::FM)`), např. dojezd
  nebo vlastní zvuk SVXLinku,
- `ActiveM17Network` — žádost M17 síťového bridge (`acquireTx(Mode::M17)`),
  reflector -> RF.

`acquireTx()` povolí TX jen jedné entitě; ostatní dostanou `false` (bridge
si žádost zopakuje později). `releaseTx()` stav uvolní.

### Volba síťového zdroje podle posledního lokálního módu

Co se vrací ze sítě na RF, se řídí naposledy použitým lokálním (RX) módem:

- výchozí stav je FM/SVXLink — M17 a D-Star ze sítě jsou blokované,
- po lokální relaci se zdroj přepne na odpovídající mód:
  M17 -> M17, FM -> SVXLink, D-Star -> D-Star.

`acquireTx(mode)` tedy propustí jen ten síťový zdroj, který odpovídá
poslednímu lokálnímu módu. Aktuální volba je vidět v `/api/status`
(`network_mode`) a na dashboardu; zablokování se loguje jako
`ARB: network TX ... blocked (last local mode is ...)`.

### RX cesta během arbiter-only stavů

RX cesta musí být živá i tehdy, když TX drží arbiter bez plugin session.
Dispatcher proto ve stavech `ActiveSvxlink` / `ActiveM17Network` pustí IQ
blok do monitorovacího fan-outu (FM plugin krmí SVXLink bridge), ale
nezakládá novou session. Squelch do SVXLinku se v těchto stavech řídí
místní nosnou (`FmPlugin::detect()`), takže místní stanice může klíčovat
i během dojezdu.

## Časovače

- `sessionTimeout` (výchozí 120 s) — maximální délka relace; po překročení
  přechod do `Timeout` a `stopSession()`.
- `returnGrace` (výchozí 20 ms) — odklad mezi `ReturnToIdle` a `Idle`, aby se
  hardwarový tail dokončil.

Časové přechody řídí `timeoutCheck()`, který dispatcher volá s každým IQ
blokem, takže celý systém běží v jediném vlákně.

## Threading

Veškerá orchestrace běží na frontend/dispatch vlákně. Stav je přesto chráněn
mutexem, aby ho mohl bezpečně číst/watchdog přidávat z jiného vlákna.
