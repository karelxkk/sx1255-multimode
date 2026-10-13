# Spectrum Monitor a web dashboard

Diagnostický subsystém pro sledování okamžitého spektra, waterfallu, RSSI,
aktivního pluginu a detekčních metrik v reálném čase. Slouží k ladění DSP a
pluginů — nejde o koncový SDR přijímač.

## Architektura

```text
RadioFrontend
     |  IQBlock
     v
IQDispatcher --------------------------------> pluginy (FM / D-Star / M17)
     |
     `-- IQ sink (diagnostický tap)
            |                         +------------------+
            +--> SpectrumAnalyzer --> | WaterfallGenerator|
            |        (vlastní vlákno)|                  |
            +--> AutomaticCapture    |                  |
                                      +------------------+
                                               |
                                               v
                                        HttpStatusServer
                                        /api/spectrum
                                        /api/waterfall
                                        /api/plugins
                                        /api/status
                                        /api/iq
                                        /  (web dashboard)
```

Surové IQ vzorky se odebírají na jediném místě (`IQDispatcher`), takže
spektrum i záznam vidí přesně stejná data jako pluginy, aniž by se měnila
frontend vrstva.

## Spectrum Analyzer

- FFT 4096 bodů (min. 1024), Hann okno, one-sided PSD.
- Publikace ~5× za sekundu, výpočty v samostatném vlákně.
- Peak-hold a odhad noise floor (medián nejtiššího kvartilu binů).
- Producent jen krátce zamyká pre-alokovaný ring buffer; RX cesta se nikdy
  neblokuje na FFT práci.

Výstup `GET /api/spectrum`:

```json
{
  "fft_size": 4096,
  "sample_rate": 125000,
  "frame": 123,
  "dropped_samples": 0,
  "noise_floor_db": -91.3,
  "spectrum": [ -120.0, ... ],
  "peak_hold": [ -120.0, ... ]
}
```

## Waterfall

`WaterfallGenerator` drží kruhovou historii spektrálních řádků (výchozí 500,
nejstarší první). Každý publikovaný frame se přidá jako jeden řádek.

Řádky se **decimují** na `waterfall_bins` (výchozí 256) a ukládají
kvantizované jako `int16` dB. Serializace celého 500řádkového bufferu tím
klesla z ~87 ms / 3,9 MB na ~2 ms / 0,5 MB, takže HTTP server při výpisu
waterfallu neblokuje `/api/spectrum` ani `/api/status`.

Výstup `GET /api/waterfall`:

```json
{
  "bins": 2049,
  "max_rows": 500,
  "rows": 123,
  "data": [ [ -120, -118, ... ], ... ]
}
```

Endpoint podporuje inkrementální přenos `GET /api/waterfall?since=<seq>`:
vrátí jen řádky novější než `seq` a v poli `seq` poslední pořadové číslo.
Dashboard tak po úvodním načtení přenáší jen pár nových řádků (~5 KB), ne
celou 500řádkovou historii (~0,5 MB).

## Plugin telemetrie

Každý plugin vrací vlastní metriky v `GET /api/plugins`:

| Plugin | Metriky |
|---|---|
| FM | `ctcss_hz`, `confidence` |
| D-Star | `sync_confidence`, `frame_count` |
| M17 | `sync_confidence`, `lsf_count`, `frame_count` |

Položka `active` říká, který plugin právě vlastní kanál.

## Web dashboard

`GET /` vrací samostatnou HTML/JS stránku bez frameworků. Zobrazuje spektrum,
waterfall, RSSI, aktuální režim, aktivní plugin, čítače detekcí a uptime.

## Konfigurace

```yaml
spectrum:
  enabled: true
  fft_size: 4096
  update_hz: 5
  waterfall_rows: 500
  waterfall_bins: 256
```

## Výkon na Raspberry Pi Zero 2 W

FFT 4096 bodů ~5×/s je jednotky procent jednoho jádra; diagnostika běží ve
vlastním vlákně a vzorky do ní tečou přes pre-alokovaný ring, takže RX cesta
není blokována. Při velkém zatížení se nejstarší nepoužité vzorky zahazují a
počet zahození je vidět v `dropped_samples`.
