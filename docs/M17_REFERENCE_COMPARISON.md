# M17 reference comparison: sx1255-multimode vs m17-gateway

Dataset: `datasets/m17/afc_audit/capture_20261014_005444.iq`

Srovnání používá stejné payload symboly z jednoho IQ záznamu. Go
implementace znovu spočítá softbity, deinterleave, Viterbi, Golay
a CRC; C++ strana poskytuje stejné mezivýsledky z běžného dekodéru.

## Souhrn

- rámců: **144**
- LSF / Packet / Stream / EOT: **144 / 0 / 0 / 0**
- softbity shodné: **55/144**
- deinterleaved shodné: **55/144**
- Viterbi výstup shodný: **144/144**
- Viterbi metric shodný: **144/144**
- softbit neshod celkem: **146**, max |rozdíl|: **1**
- softbit neshody bez změny Viterbi výstupu: **89**
- Golay shodný: **144/144**
- CRC shodný: **144/144**
- LSF výsledek shodný: **144/144**
- plně shodných rámců: **55/144**

## První rozdílná vrstva

**CRC (shodně v obou implementacích)**

Funkční pořadí zkoumaných vrstev: softbity a Viterbi výstup jsou
shodné s Go referencí, Golay se u LSF neuplatní a první skutečně
selhávající kontrolou je CRC. Numerické softbit rozdíly maximálně
**1** LSB nemění Viterbi výstup ani metric.

Rozhodující je vztah mezi vrstvami: pokud se softbity liší jen
numericky a Viterbi výstup i metric jsou shodné, nejde o funkční
rozdíl dekodéru. U zkoumaného capture je Viterbi výstup shodný ve
**144/144** rámců.

## První neshody

| soubor | sample | typ | softbit | deinter | Viterbi | metric | Golay | CRC C++/Go |
|---|---:|---|---|---|---|---|---|---|
| datasets/m17/afc_audit/capture_20261014_005444.iq | 17130 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 50055 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 63565 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 76075 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 90255 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 103330 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 109055 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 160510 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 191665 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 199920 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 217805 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 221370 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 223475 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 227935 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 237330 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 253965 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 256925 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 264220 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 285840 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 297540 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 324510 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 327245 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 338315 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 339350 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 350600 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 367160 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 375940 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 407825 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 409410 | LSF | False | False | True | True | True | FAIL/FAIL |
| datasets/m17/afc_audit/capture_20261014_005444.iq | 419115 | LSF | False | False | True | True | True | FAIL/FAIL |

## Poznámka k vrstvám

- LSF cesta je Viterbi → CRC; Golay se jí netýká.
- Golay se používá pro LICH ve stream rámcích.
- Packet CRC vzniká až po složení všech packet frame do zprávy.
