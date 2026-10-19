# M17 reference comparison: sx1255-multimode vs m17-gateway

Dataset: `datasets/m17/afc_audit/capture_20261014_005444.iq`

Srovnání používá stejné payload symboly z jednoho IQ záznamu. Go
implementace znovu spočítá softbity, deinterleave, Viterbi, Golay
a CRC; C++ strana poskytuje stejné mezivýsledky z běžného dekodéru.

## Souhrn

- rámců: **15**
- LSF / Packet / Stream / EOT: **15 / 0 / 0 / 0**
- softbity shodné: **5/15**
- deinterleaved shodné: **5/15**
- Viterbi výstup shodný: **15/15**
- Viterbi metric shodný: **15/15**
- softbit neshod celkem: **16**, max |rozdíl|: **1**
- softbit neshody bez změny Viterbi výstupu: **10**
- Golay shodný: **15/15**
- CRC shodný: **15/15**
- LSF výsledek shodný: **15/15**
- plně shodných rámců: **5/15**

## První rozdílná vrstva

**CRC (shodně v obou implementacích)**

Funkční pořadí zkoumaných vrstev: softbity a Viterbi výstup jsou
shodné s Go referencí, Golay se u LSF neuplatní a první skutečně
selhávající kontrolou je CRC. Numerické softbit rozdíly maximálně
**1** LSB nemění Viterbi výstup ani metric.

Rozhodující je vztah mezi vrstvami: pokud se softbity liší jen
numericky a Viterbi výstup i metric jsou shodné, nejde o funkční
rozdíl dekodéru. U zkoumaného capture je Viterbi výstup shodný ve
**15/15** rámců.

## První neshody

| soubor | sample | typ | softbit | deinter | Viterbi | metric | Golay | CRC C++/Go |
|---|---:|---|---|---|---|---|---|---|
| datasets/m17/afc_audit/capture_20261014_005444.iq | 121360 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 246365 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 957200 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 987425 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1095345 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1114225 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1119610 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1167010 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1235435 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 1348470 | LSF | False | False | True | True | True | FAIL/FAIL |

## Nejsilnější reálný burst

Nejsilnější kandidát v historických capture:

- RSSI: **−23.87 dBFS**,
- AFC: **−297.35 Hz**,
- SyncDistance: **4.19**,
- Softbit RMS: **0.859**,
- Viterbi metric: **52**,
- softbit neshoda proti Go: **1 hodnota, rozdíl 1 LSB**,
- Viterbi výstup: **zcela shodný**,
- CRC/LSF: **FAIL v obou implementacích**.

Tedy ani při silném signálu nevzniká rozdíl mezi C++ a původním
dekodérem. CRC selhává shodně a problém je již v obsahu softbitů před
Viterbiho dekódováním, případně v samotném vysílaném M17 signálu.

## Kontrolní čistý dataset

Stejné srovnání na referenčním M17 TX IQ (`m17_tx_20261013.iq`):

- rámců: **145**, z toho **134 M17** a **11 EOT**,
- Viterbi výstup: **145/145 shodný**,
- Viterbi metric: **145/145 shodný**,
- Golay: **145/145 shodný**,
- LSF/CRC: **2/2 shodné a OK**.

Tím je ověřeno, že referenční srovnání umí potvrdit i úspěšnou cestu,
nejen shodné selhání.

## Poznámka k vrstvám

- LSF cesta je Viterbi → CRC; Golay se jí netýká.
- Golay se používá pro LICH ve stream rámcích.
- Packet CRC vzniká až po složení všech packet frame do zprávy.
