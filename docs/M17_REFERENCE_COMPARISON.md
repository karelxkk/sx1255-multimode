# M17 reference comparison: sx1255-multimode vs m17-gateway

Dataset: `datasets/m17/afc_audit/capture_20261014_005543.iq`

Srovnání používá stejné payload symboly z jednoho IQ záznamu. Go
implementace znovu spočítá softbity, deinterleave, Viterbi, Golay
a CRC; C++ strana poskytuje stejné mezivýsledky z běžného dekodéru.

## Souhrn

- rámců: **55**
- LSF / Packet / Stream / EOT: **55 / 0 / 0 / 0**
- softbity shodné: **21/55**
- deinterleaved shodné: **21/55**
- Viterbi výstup shodný: **55/55**
- Viterbi metric shodný: **55/55**
- softbit neshod celkem: **51**, max |rozdíl|: **1**
- softbit neshody bez změny Viterbi výstupu: **34**
- Golay shodný: **55/55**
- CRC shodný: **55/55**
- LSF výsledek shodný: **55/55**
- plně shodných rámců: **21/55**

## První rozdílná vrstva

**CRC (shodně v obou implementacích)**

Funkční pořadí zkoumaných vrstev: softbity a Viterbi výstup jsou
shodné s Go referencí, Golay se u LSF neuplatní a první skutečně
selhávající kontrolou je CRC. Numerické softbit rozdíly maximálně
**1** LSB nemění Viterbi výstup ani metric.

Rozhodující je vztah mezi vrstvami: pokud se softbity liší jen
numericky a Viterbi výstup i metric jsou shodné, nejde o funkční
rozdíl dekodéru. U zkoumaného capture je Viterbi výstup shodný ve
**55/55** rámců.

## První neshody

| soubor | sample | typ | softbit | deinter | Viterbi | metric | Golay | CRC C++/Go |
|---|---:|---|---|---|---|---|---|---|
| datasets/m17/afc_audit/capture_20261014_005543.iq | 699670 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 715135 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 778680 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 844605 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 873400 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 881950 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 896955 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1049280 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1060810 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1097405 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1331630 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1355095 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1367155 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1422780 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1429170 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1483300 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1506555 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1546350 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1575620 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1603960 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1612735 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1680975 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1726615 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 1984440 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2008910 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2030115 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2149065 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2230310 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2304825 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005543.iq | 2338530 | LSF | False | False | True | True | True | FAIL/FAIL |

## Poznámka k vrstvám

- LSF cesta je Viterbi → CRC; Golay se jí netýká.
- Golay se používá pro LICH ve stream rámcích.
- Packet CRC vzniká až po složení všech packet frame do zprávy.

## Kontrolní čistý dataset

Stejné srovnání na referenčním M17 TX IQ (`m17_tx_20261013.iq`):

- rámců: **145**, z toho **134 M17** a **11 EOT**,
- softbity: **72/145** přesně shodných; ostatní se liší nejvýše o několik
  LSB,
- Viterbi výstup: **145/145 shodný**,
- Viterbi metric: **145/145 shodný**,
- Golay: **145/145 shodný**,
- LSF/CRC: **2/2 shodné a OK**.

Tím je ověřeno, že referenční srovnání umí potvrdit i úspěšnou cestu,
nejen shodné selhání.
