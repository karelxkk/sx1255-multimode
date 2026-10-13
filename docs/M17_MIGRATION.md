# M17: migrace DSP z m17-gateway

Cíl: **ne** psát M17 modem od nuly, ale maximálně znovupoužít funkční
implementaci z `github.com/jancona/m17` (dále jen „m17-gateway“). Pořadí je
dané: nejdřív reuse, potom refactoring, nový kód až když reuse není možný.

> Důležité upozornění: m17-gateway je napsaný v Go, sx1255-multimode v C++20.
> „Převzato“ tedy znamená věrný 1:1 port algoritmu (stejná matematika, stejné
> konstanty, stejné pořadí operací), ne kopírování zdrojového textu. Kód se
> smí refaktorovat kvůli rozhraní, ale nesmí se měnit číselné chování.

## 1. RX řetězec v m17-gateway

Z `modem/sx1255_alsa.go` (`sx1255RXPipelineTuned`) a `modem/sx1255_modem.go`:

```text
ALSA capture (S32_LE stereo, I=left, Q=right)
→ ComplexDCRemoval        (EMA high-pass, alpha = 0.9999)
→ PolyphaseDecimator      (125 kSa/s → 25 kSa/s, channel filter 12.5 kHz, 200 taps)
→ FMDemodAFC              (komplexní IQ → okamžitá frekvence + softwarové AFC)
→ RationalResampler       (25 kSa/s → 24 kSa/s, poměr 24/25)
→ SampleToSymbol          (RRC matched filter, rrcTaps5, 5 vzorků/symbol, scaling 2.48)
→ chan float32            (24 kSa/s, tedy 5 vzorků na symbol)
```

Po vzorkovém filtru následuje symbolová synchronizace a dekódování:

```text
processSymbolStream(sps=5)
→ SyncDistance            (Euklidovská vzdálenost od sync vzorů)
→ extractPayload          (odhad vzorkovací fáze + decimace na symbol rate)
→ CalcSoftbits            (symboly ±3/±1 → soft bity)
→ Decoder.DecodeFrame     (Viterbi, puncturing, Golay, LSF/stream/packet)
```

Klíčové zjištění: **symbol recovery v m17-gateway není adaptivní clock
recovery.** Je to RRC matched filter při 5 vzorcích/symbol a poté hledání
nejlepší vzorkovací fáze pomocí sync slova (`extractPayload` zkouší offset
`±sps/2` a pak bere každý `sps`-tý vzorek). Tento mechanismus je nutné
zachovat beze změny, protože na něm závisí prahy synchronizace.

Poznámka k `sps`: komentáře v `codec.go` a `modem/modem.go` tvrdí „1 pro
SX1255“, ale skutečný kód (`StartDecoding(..., 5)` a RX pipeline bez
`MaxAbsDecimator`) používá **5 vzorků/symbol i pro SX1255**. Při portu se řiď
kódem, ne těmi komentáři.

## 2. TX řetězec v m17-gateway

Z `NewSX1255` (modem/sx1255_modem.go) a `sx1255_dsp.go`:

```text
symboly → TXPulseShaper (rrcTaps5, 5 vzorků/symbol)
        → BatchResampler (24 kSa/s → 125 kSa/s, 125/24)
        → BatchFMModulator (deviace → komplexní IQ)
        → ALSA playback (S32_LE stereo)
```

TX gain (`transmitGain = sqrt(5)`, `modem/transform.go`) a RRC tapy
`rrcTaps5` se přebírají doslova.

## 3. Klasifikace kódu

| Soubor / symbol | Kategorie | Kam v sx1255-multimode |
|---|---|---|
| `modem/sx1255_spi.go`, `sx1255_alsa.go` (ALSA), `modem_gpio_linux.go` | závislé na SX1255/hardwaru | **zůstává ve frontendu** (`ISX1255`), nemigruje se |
| `modem/sx1255_dsp.go`: ComplexDCRemoval, PolyphaseDecimator, FMDemodAFC, RationalResampler, TXPulseShaper, BatchResampler, BatchFMModulator, designChannelFilter, designLowpassFIR | čisté DSP | `M17DSP` |
| `modem/transform.go`: SampleToSymbol, SymbolToSample, rrcTaps5, transmitGain | čisté DSP | `M17DSP` |
| `codec.go`: SyncDistance, sync symboly, puncture patterny, cost tabulky | synchronizace | `M17Synchronizer` |
| `modem/modem.go`: processSymbolStream, extractPayload | synchronizace + framing | `M17Synchronizer` |
| `decoder.go`: Decoder.DecodeFrame, CalcSoftbits, decodeStreamFrame, decodePacketFrame | protokol / FEC | `M17FrameDecoder` |
| `codec.go`: ViterbiDecoder | FEC | `M17FrameDecoder` |
| `golay.go`: SoftDecode24, HardDecode24, DecodeLICH | FEC | `M17FrameDecoder` |
| `lsf.go`, `packet.go`, `stream.go`: LSF/Packet/Stream datové struktury | protokol | `M17FrameDecoder` |
| `crc.go`, `encoding.go` | kódování | `M17FrameDecoder` |
| `cmd/m17-gateway/gateway.go`, `audio.go`, `packets.go` | aplikační logika (reflektor, Codec2 audio) | **nemigruje se do pluginu**; adapter dělá jen rozhraní |

## 4. Navržené třídy

### M17DSP — čistý DSP, žádná synchronizace

Zodpovídá jen za transformaci vzorků. Žádný stav protokolu.

- `ComplexDCRemoval` (alpha 0.9999)
- `PolyphaseDecimator` (125k→25k, 200-tap channel filter 12.5 kHz)
- `FMDemodAFC` (carrier rise +10 dB / fall +3 dB, fast=25, slow=2500,
  offset averaging `avgCnt=4000`)
- `RationalResampler` (25k→24k, 24/25)
- `SampleToSymbol` (RRC `rrcTaps5`, 5 sps, scaling 2.48)
- TX: `TXPulseShaper`, `BatchResampler` (125/24), `BatchFMModulator`,
  `transmitGain = sqrt(5)`

Rozhraní: `pushIQ(const IQBlock&)` → callback se vzorky 24 kSa/s (float);
`pushSymbols(...)` → IQ pro `ITransmit`.

### M17Synchronizer — sync a symbol recovery

Port `SyncDistance` + `processSymbolStream` + `extractPayload`:

- klouzavý buffer vzorků (velikost 8·sps + 2·(8·sps + 192·sps) + sps/2 + 256),
- výpočet Euklidovské vzdálenosti od LSF/Stream/Packet/EOT sync vzorů,
- prahy **4.5** pro LSF/EOT a **5.0** pro Stream/Packet,
- hledání vzorkovací fáze (`extractPayload`, offset ±sps/2) a decimace,
- výstup: `(syncType, softBits)` událost do `M17FrameDecoder`.

### M17FrameDecoder — protokol a FEC

Port `CalcSoftbits`, `Decoder.DecodeFrame`, `ViterbiDecoder`, `Golay`,
`LSF`, `Packet`, `Stream`, `crc`, `encoding`.

- soft bity → deinterleave → depuncture → Viterbi → Golay → LSF / frame,
- výstup: hotové LSF / stream framy / packety jako callbacky do M17Plugin.

## 5. M17Plugin — jen adapter

M17Plugin **neobsahuje DSP**. Jen propojuje:

- `detect(block)`: předá IQ do `M17DSP` → `M17Synchronizer`; vrátí
  `Detection{detected}` když synchronizér zahlásí LSF/Stream/Packet sync.
- `process(block)`: stejná cesta, ale výsledky jdou do `M17FrameDecoder`;
  události (LSF, voice frame, packet, EOT) předává gateway vrstvě.
- `startSession/stopSession`: drží stav relace, TX DSP reset.
- TX: přes `SessionContext::tx` (ITransmit) — `M17DSP` TX řetězec.

## 6. Převzato vs. refaktorováno vs. nově vytvořeno

**Převzato 1:1 (port algoritmu beze změny chování):**

- všechny DSP bloky z `sx1255_dsp.go` a `transform.go` včetně tapů a gainů,
- `SyncDistance`, `processSymbolStream`, `extractPayload` včetně prahů,
- `CalcSoftbits`, `ViterbiDecoder`, `Golay`, `LSF/Packet/Stream` parse.

**Refaktorováno (stejný algoritmus, jiné rozhraní):**

- Go `chan`/gorutiny → synchronní push (IQBlock) nebo callbacky; jinak beze
  změny matematiky,
- `Transform` pipeline (generika + kanály) → jednoduché C++ bloky s
  `process()` metodami,
- INI konfigurace → YAML v sx1255-multimode.

**Nově vytvořeno:**

- `M17Plugin` adapter k `IModePlugin` / `IQDispatcher` / `SessionManager`,
- propojení TX do `ITransmit`,
- C++ unit testy (s referenčními vektory z Go testů, viz níže).

**Proč nebylo možné použít zbytek kódu:**

- `modem/sx1255_*.go` — ALSA (`github.com/yobert/alsa`), SPI a GPIO jsou
  hardwarové a v sx1255-multimode je vlastní `ISX1255` vrstva; duplikovat
  ovladač by porušilo návrh „jeden frontend“.
- `cmd/m17-gateway/*` — reflektor/gateway/Codec2 audio logika patří do
  aplikační vrstvy, ne do pluginu; plugin vydává dekódované rámce a gateway
  vrstva (budoucí) je zpracuje.
- Go-specifické konstrukce (kanály, gorutiny, `time` řízení) nelze přenést
  mechanicky, jen ekvivalentně nahradit.

## 7. Zachování citlivosti a dekódovacích vlastností

Tyto hodnoty jsou **empiricky naladěné** (komentáře přímo v kódu popisují
BER dopad). Při portu se musí přenést přesně:

| Konstanta | Hodnota | Důsledek při změně |
|---|---|---|
| `rxScalingCoeffSX1255` | 2.48 | posune konstelaci vůči prahům sync vzdálenosti |
| channel filter | 12.5 kHz, 200 tapů, decim 5 | antialiasing, okolní kanály |
| `FMDemodAFC` avgCnt | 4000 (160 ms @25k) | konvergence AFC na začátku přenosu |
| `FMDemodAFC` fast/slow | 25 / 2500 | detekce nástupu/poklesu nosné |
| carrier rise/fall | 10x / 2x | restart AFC odhadu na nosné |
| RationalResampler | 24/25 | 25k → 24k přesně pro 4800 baud |
| `rrcTaps5` + scaling | 2.48 | tvar symbolu |
| sync prahy | 4.5 (LSF/EOT), 5.0 (Stream/Packet) | falešné/zmeškané syncy |
| `sps` | 5 | vzorkovací fáze a decimace |
| `transmitGain` | sqrt(5) | TX úroveň |

Testy citlivosti: převzít referenční signály z `sx1255_capture_test.go`,
`sx1255_loopback_test.go` a `sx1255_afc_test.go` a ověřit identický počet
přijatých synců / BER. To je přijímací kritérium „stejná citlivost“.

## 8. Oprava sync slov (dřívější chyba ve stubu)

Předchozí komentář v `M17Plugin.cpp` uváděl chybný LSF sync
`0x3243F6A8885A308D`. Skutečné sync hodnoty z `codec.go` / `decoder.go`:

| Typ | Sync word | Symboly |
|---|---|---|
| LSF | `0x55F7` | `+3 +3 +3 +3 −3 −3 +3 −3` |
| Stream | `0xFF5D` | `−3 −3 −3 −3 +3 +3 −3 +3` |
| Packet | `0x75FF` | `+3 −3 +3 +3 −3 −3 −3 −3` |
| BERT | `0xDF55` | `−3 +3 −3 −3 +3 +3 +3 +3` |
| EOT marker | `0x555D` | `+3 +3 +3 +3 +3 +3 −3 +3` |
