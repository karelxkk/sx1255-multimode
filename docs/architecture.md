# Architektura

## Vrstvy

```
+----------------------+
| SX1255 Frontend      |   ISX1255 + RadioFrontend
+----------+-----------+
           | IQBlock
           v
+----------------------+
| IQ Dispatcher        |   fan-out / exclusive routing
+----------+-----------+
           |
           v
+----------------------+
| Session Manager      |   vlastnictví, priority, stavy
+----------+-----------+
           |
           v
  +--------+--------+--------+
  | FM     | D-Star | M17    |   IModePlugin
  +--------+--------+--------+
```

## Komponenty

### RadioFrontend

Jediný RF frontend. Vlastní `ISX1255` zařízení, běží capture smyčku, razítkuje
IQ bloky sekvenčním číslem a předává je dispatcheru. Implementuje `ITransmit`,
aby pluginy mohly posílat svůj vysílací řetězec (frekvence, PTT, IQ) bez
znalosti hardwaru.

Frontend **neobsahuje žádnou mode logiku**. Konkrétní implementace `ISX1255`
řeší SPI/ALSA/GPIO; `NullSX1255` poskytuje tichý zdroj pro testy.

### IQ Dispatcher

Jeden bod, kterým prochází veškerý IQ. Pro každý blok:

- aktivní relace → blok jde exkluzivně vlastníkovi (`process`),
- monitoring → blok se fan-outuje všem pluginům (`detect`) a SessionManager
  rozhodne o vlastnictví.

Dispatcher nezná žádný mód ani DSP.

### Session Manager

Vlastní stavový automat a pravidlo jediného vlastníka. Arbitráž je
deterministická: vyhrává plugin s nejvyšší prioritou, který hlásí detekci
(M17 > D-Star > FM).

### Pluginy

Implementují `IModePlugin`. Každý plugin má vlastní detekci i vysílací řetězec.
Viz [plugin-api.md](plugin-api.md).

## Datový tok

IQ vzorky jsou `ComplexFloat` (I, Q) v nemodifikujícím bloku `IQBlock`
(sekvence, ukazatel, počet, vzorkovací frekvence). Frontend je jediný
producent; pluginy vzorky nikdy nevlastní, jen čtou.

## Rozšiřitelnost

Přidání režimu = nová třída `IModePlugin` + registrace v `PluginRegistry`.
Nedotýká se frontendu ani dispatcheru. Registr je připravený i pro budoucí
dynamické načítání `.so` pluginů (dlopen loader se registruje stejným
`registerPlugin()`).
