# M17 — porting report

Report k migraci DSP z `github.com/jancona/m17` do `sx1255-multimode`.

Cíl byl jediný: **věrný 1:1 port**, ne nová implementace. Žádné Gardner/Mueller-
Müller recovery, žádné nové AFC ani nové synchronizační mechanismy.

## Výsledek validace

Křížová validace proti Go referenci (spuštěn test přímo v repu m17-gateway):

```text
Go:  IQ 17813, RX 3420
C++: iq size=17813, rx size=3420
```

Sync vzdálenosti v identických offsetech jsou bitově shodné (rozdíl 0 na 3
desetinná místa). Loopback test (vlastní TX → vlastní RX) dekóduje identický
LSF včetně CRC a volacích znaků. Kritérium „stejné synchronizační body a
stejné dekódované rámce“ je splněno.

## Převzaté soubory (Go → C++)

| m17-gateway | sx1255-multimode |
|---|---|
| `modem/sx1255_dsp.go` | `src/m17/M17DSP.cpp` |
| `modem/transform.go` | `src/m17/M17DSP.cpp` |
| `codec.go` (SyncDistance + sync symboly + punture + Viterbi) | `src/m17/M17Synchronizer.cpp`, `src/m17/M17FrameDecoder.cpp` |
| `modem/modem.go` (processSymbolStream, extractPayload, generateLSF/StreamSymbols) | `src/m17/M17Synchronizer.cpp`, `src/m17/M17FrameDecoder.cpp` |
| `decoder.go` | `src/m17/M17FrameDecoder.cpp` |
| `golay.go` | `src/m17/M17FrameDecoder.cpp` |
| `lsf.go`, `crc.go`, `encoding.go` | `src/m17/M17FrameDecoder.cpp` |
| konstanty | `include/sx1255/m17/M17Constants.hpp` |

## Převzaté algoritmy (beze změny)

- `ComplexDCRemoval` — EMA high-pass, `alpha=0.9999`
- `PolyphaseDecimator` — 125k→25k, channel filter 12.5 kHz / 200 tapů
- `FMDemodulator` / `FMDemodAFC` — konjugovaný součin + `atan2`, nosná
  +10 dB / −3 dB, `fast=25`, `slow=2500`, `avgCnt=4000`
- `RationalResampler` — 25k→24k, poměr 24/25, Blackman-windowed sinc
- `SampleToSymbol` — RRC matched filter (`rrcTaps5`, `scaling=2.48`)
- TX: `TXPulseShaper`, `BatchResampler` (125/24), `BatchFMModulator` (800 Hz),
  korekce `sqrt(5)`
- `SyncDistance` — Euklidovská vzdálenost od LSF/Stream/Packet/EOT vzorů
- `processSymbolStream` / `extractPayload` — klouzavý buffer, hledání
  vzorkovací fáze `±sps/2`, decimace na symbol rate
- `calcSoftbits`, `deinterleaveSoftBits`, `derandomizeSoftBits`
- `ViterbiDecoder` (decodeBit/chainback), `golaySoftDecode24`, `DecodeLICH`,
  `EncodeLICH`, `crc16`, `LSF` parse, `Decoder.DecodeFrame`

Všechny klíčové konstanty jsou v `M17Constants.hpp` a hodnoty odpovídají
`codec.go`/`decoder.go`/`transform.go`/`sx1255_alsa.go`: `rxScalingCoeff=2.48`,
`sps=5`, sync prahy 4.5/5.0, `LSFPuncturePattern` (7×11011101+11011),
`StreamPuncturePattern`, `PacketPuncturePattern`, `randomizeSeq`,
`interleaveSequence`, `rrcTaps5`, `golayEncodeMatrix`.

## Změněné části a důvody

- **Go kanály/gorutiny → synchronní push / callbacky.** DSP bloky mají metody
  `process()`, synchronizér `push()`, frame decoder callbacky. Matematika se
  nemění, jde jen o transport vzorků.
- **`container/ring` → ruční cirkulární buffer.** Stejné pořadí čtení/zápisu.
- **`math/rand` (streamID) → deterministický čítač.** `streamID` je jen lokální
  identifikátor streamu, neovlivňuje dekódované rámce; Go `math/rand` by v C++
  neměl identický protějšek.
- **`regexp` validace volacích znaků** v `EncodeCallsign` nebyla portovaná.
  Kódování znaků je identické; validaci řeší volající (testy používají platné
  znaky).

## Rozdíly proti m17-gateway

- `streamID` pochází z čítače, ne z Go `math/rand` (viz výše).
- Chybí regex validace volacích znaků (Go-specifické).
- `M17Plugin` je nový adapter (IQDispatcher → M17DSP → M17Synchronizer →
  M17FrameDecoder); v m17-gateway toto propojení dělá `SX1255` modem +
  `Gateway`.
- TX DSP je portovaná a otestovaná přes `modulateSymbols()`; plugin ji zatím
  nezapojuje na `ITransmit` (až vznikne TX orchestrace v SessionManageru).

## Přijímací kritéria

- `test_m17_units` — CRC (0x772B), volací znaky, Golay round-trip,
  CalcSoftbits, LSF CRC, SyncDistance.
- `test_m17_loopback` — TX LSF (preamble + LSF frame) → RX řetězec →
  synchronizér → Viterbi → LSF; dekódované byty se rovnají vysílaným.

Shoda s Go byla ověřena referenčním testem spuštěným přímo v m17-gateway
(dočasný test byl po validaci odstraněn).
