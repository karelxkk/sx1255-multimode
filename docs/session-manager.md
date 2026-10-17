# Session Manager

## Stavy

```
IDLE -> DETECTING -> ACTIVE_FM | ACTIVE_DSTAR | ACTIVE_M17
                          |
                          +-> TIMEOUT -> RETURN_TO_IDLE -> IDLE
                          |
ACTIVE_* (normální konec) -> RETURN_TO_IDLE -> IDLE
```

| Stav | Význam |
|---|---|
| `Idle` | klid, čekání na start |
| `Detecting` | monitoring, detekce u všech pluginů |
| `ActiveFm` / `ActiveDStar` / `ActiveM17` | exkluzivní vlastnictví |
| `Timeout` | relace překročila limit |
| `ReturnToIdle` | odklad před návratem do Idle (grace period) |

## Arbitráž

Pouze jeden plugin smí být aktivní. Při monitoringu se seberou detekce
všech pluginů, seřadí se podle priority a první s `detected=true` vyhrává.
Pokud je relace aktivní, další detekce se ignorují.

## Časovače

- `sessionTimeout` (výchozí 300 s) — maximální délka lokální relace; po překročení
  přechod do `Timeout` a `stopSession()`.
- `returnGrace` (výchozí 20 ms) — odklad mezi `ReturnToIdle` a `Idle`, aby se
  hardwarový tail dokončil.

Časové přechody řídí `timeoutCheck()`, který dispatcher volá s každým IQ
blokem, takže celý systém běží v jediném vlákně.

## Threading

Veškerá orchestrace běží na frontend/dispatch vlákně. Stav je přesto chráněn
mutexem, aby ho mohl bezpečně číst/watchdog přidávat z jiného vlákna.
