# Analýza skoků BER během M17 streamu

Datum měření: 2026-10-21

Zdroj: Baofeng DM-1701, OpenRTX 0.4.4 s preamble 160 ms

Příjem: SX1255, LNA 36 dB, RX 430,975 MHz

IQ záznam: `datasets/m17/ber_jump_20261021/m17_fullstream_20261021.iq`

## Závěr

V zachyceném provozu nebyl nalezen důkaz, že by OpenRTX během relace
přerušoval nebo poškozoval M17 hlasové rámce. Offline audit našel
**2326 platných stream rámců, žádný rámec s chybou Golay a žádný rámec,
který by neprošel dekódováním**. Kmitočet rámců zůstal shodný s nominální
periodou 40 ms.

Skok hlavního ukazatele BER je způsoben příjmem falešných LSF kandidátů
uprostřed již otevřeného streamu. Tyto kandidáty nejsou platné hlavičky,
ale synchronizátor je dočasně klasifikuje jako `LSF`. Jejich CRC selže
a jejich před-FEC BER je přibližně 8-14 %. Protože je dekodér už ve stavu
streamu, aplikace je označí jako `Result=M17`, a tím je započítá do
hlavního BER. Po jejich skončení ale nemají platné hlasové rámce
dostupnou hodnotu BER, takže poslední vysoká hodnota může zůstat zobrazená
až do další platné hlavičky nebo další relace.

Toto není porucha modulátoru OpenRTX. Je to kombinace slepé detekce
synchromích slov v náhodném streamovém obsahu a nesprávné agregace BER
v sx1255-multimode.

Dashboard proto používá jako hlavní živý ukazatel symbol MER, který lze
měřit u každého 4FSK rámce bez čekání na CRC. Ověřená BER zůstává dostupná
jako doplňkový údaj pro LSF a packety.

## Zachycená relace

| Parametr | Hodnota |
|---|---:|
| Délka IQ záznamu | 141,0 s |
| Vzorkovací kmitočet | 125 kSa/s |
| Platné LSF | 3 |
| Platné stream rámce | 2326 |
| Stream rámce s Golay OK | 2326 |
| Stream rámce s Golay FAIL | 0 |
| Medián Viterbi metric streamu | 10 |
| P95 Viterbi metric streamu | 15 |
| Maximum Viterbi metric streamu | 26 |
| Medián SyncDistance streamu | 3,53 |
| P95 SyncDistance streamu | 4,85 |
| Medián Softbit RMS streamu | 0,938 |
| Medián symbolové úrovně | 2,264 |
| Medián intervalu stream rámců | 40,000 ms |
| P95 intervalu stream rámců | 40,039 ms |

První relace začala platným LSF v `18:37:12.656` a skončila EOT
v `18:37:17.219`. Druhá relace začala v `18:37:19.268` a skončila
v `18:38:50.670`. Během druhé, přibližně 91sekundové relace zůstal
stream souvislý.

## Falešné LSF kandidáty

Při RSSI nad -55 dBFS bylo nalezeno:

- 121 falešných LSF s neplatným CRC,
- 15 falešných packet kandidátů,
- medián SyncDistance falešných kandidátů 4,18,
- medián Viterbi metric 49,
- medián před-FEC BER 10,46 %.

Pro srovnání, platné stream rámce měly medián Viterbi metric 10
a medián SyncDistance 3,53. Falešné LSF jsou tedy odlišitelné nejen
podle CRC: na stejné úrovni signálu mají výrazně horší Viterbi metric
a obvykle horší SyncDistance.

První falešný LSF uvnitř stabilního druhého streamu byl zachycen
v `18:37:38.226`. Jeho BER byl 10,60-11,69 %. Od této doby se hlavní
ukazatel už nemusí vrátit na nulu, protože následující platné stream
rámce v `FrameAudit` nemají vyplněnou hodnotu BER.

### Důsledek pro audio

Bylo nalezeno 10 mezer v rámci streamu delších než 80 ms a kratších
než 200 ms. Celkem odpovídají 17 chybějícím 40ms rámcům, tedy přibližně
1,08 s z 95 s aktivního provozu:

- 11 rámců bylo pohlceno falešnými LSF,
- 6 rámců bylo pohlceno falešnými packet kandidáty.

Tento malý skutečný zásah do streamu vzniká v přijímači při falešné
synchronizaci. Není doprovázen ztrátou nebo zhoršením kvality okolních
stream rámců.

## Proč ukazatel není skutečné BER streamu

M17 stream nemá na úrovni každého hlasového rámce CRC. Hodnota BER,
kterou lze spolehlivě ověřit re-encodováním, je dostupná pro LSF
a packet, jejichž CRC prošlo. U streamu lze měřit kumulativní
re-encoding distance a kvalitativní ukazatele, ale nelze ji vydávat
za nezávislé potvrzení správnosti každého rámce.

Současná implementace vybírá pro hlavní BER posledních 100 položek
s `ber_percent >= 0` a `result == "M17"`. Falešný LSF uvnitř streamu
tyto podmínky splní. Stream rámce mají `ber_percent == -1`, takže
nemohou zobrazenou chybu nahradit platnou nízkou hodnotou.

## Implementovaná oprava sx1255-multimode

Oprava byla ověřena na stejném 141sekundovém IQ záznamu bez změny AFC,
sync prahů, RRC, Viterbiho nebo Golay algoritmu:

| Ukazatel | Před opravou | Po opravě |
|---|---:|---:|
| Dekódované stream rámce | 2326 | 2398 |
| Aktivní falešné LSF | 121 | 60 |
| Mezery ve streamu | 10 | 0 |
| Chybějící stream rámce | 17 | 0 |
| Falešný LSF v hlavním BER | ano | ne |

Při odmítnutí falešného kandidáta se nyní obnoví celé původně spotřebované
okno. Stejný typ kandidáta je potlačen pouze po dobu průchodu tímto oknem,
takže se okamžitě nepřijme znovu, ale skutečný `StreamSync` zůstane dostupný.

## Doporučené úpravy sx1255-multimode

1. **Nejprve opravit význam zobrazené BER.** Hlavní BER označit jako
   `BER LSF/paketů` a počítat pouze rámce s `crcStatus == 1`
   a dostupnou hodnotou `channelErrors`. Neúspěšný LSF nikdy
   nepoužívat pro `last_frame_ber_percent` ani `average_ber_percent`.

2. **Přidat samostatný ukazatel kvality streamu.** Zobrazovat například
   `stream frame loss`, `Golay OK/FAIL`, `Viterbi metric` a `SyncDistance`.
   Volitelně zobrazit kumulativní re-encoding distance streamu, ale
   výslovně s označením `stream estimate`, nikoli jako potvrzenou BER.

3. **Falešný LSF uvnitř streamu klasifikovat jako `Candidate`, ne `M17`.**
   Stav `syncedBefore != 0` sám nestačí k tomu, aby neplatná hlavička
   byla považována za úspěšný M17 rámec.

4. **Rozšířit follow režim synchronizátoru i na stream.** Dokud je
   dekodér synchronizovaný na stream, má se přednostně hledat známé
   `StreamSync`. LSF má přepsat tento stav jen při výrazně lepším
   kandidátu a po potvrzení CRC.

5. **Při odmítnutém LSF obnovit celé spotřebované okno.** Současný
   `retract()` vrací payload, ale ne všech 16 symbolů falešného sync
   okna. Pokud je skutečný `StreamSync` uvnitř tohoto okna, daný rámec
   se ztratí. Obnova musí zachovat skutečný stream sync a pouze potlačit
   stejnou falešnou pozici, aby se ihned znovu nepřijala.

6. **Neměnit globální prahy AFC, RRC, Viterbi, Golay ani LSF nyní.**
   Problém je kontextová klasifikace a agregace, nikoli selhání
   demodulace nebo FEC.

## Doporučení pro OpenRTX

Na základě tohoto záznamu **není vhodné měnit M17 modulátor, RRC filtr,
PA, odchylku ani preamble**. Zachovat současné nastavení:

- preamble 160 ms,
- `C6000.setInputGain(-6)`,
- deviation coefficient `0x35 = 0x12`.

Pro budoucí diagnostiku je užitečné doplnit pouze neinvazivní měření
přerušení TX bufferu. `Modulator::sendBaseband()` ignoruje návratovou
hodnotu `outputStream_sync()`, přestože `Cx000dac_sync()` umí ohlásit
přetečení/underrun návratovou hodnotou `-1`. Doporučená úprava je:

- zaznamenat počet selhání `outputStream_sync()`,
- rozlišit nedostatek dat a přetečení DAC FIFO,
- zobrazit čítač v diagnostice OpenRTX,
- při detekovaném underrunu označit záznam za neplatný pro srovnání BER.

Tato instrumentalizace nemění generovaný M17 signál. V tomto capture
se žádné přerušení TX bufferu neprojevilo; všech 2326 stream rámců bylo
dekódovatelných a jejich časování bylo pravidelné.

## Reprodukce

```bash
./build/m17-frame-audit \
  datasets/m17/ber_jump_20261021/m17_fullstream_20261021.iq \
  --output datasets/m17/ber_jump_20261021/m17_fullstream_20261021.audit.json
```

SHA-256 IQ záznamu:

```text
3e22002133f271e4e31bbedd9613415f75424e558eb6277bd54656a4cd9c2c74
```
