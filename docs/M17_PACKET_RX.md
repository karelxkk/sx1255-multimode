# Příjem M17 packetů: nic se neztrácí

M17 packet mode (SMS zprávy, poloha) se v multimode dekóduje vždy, když je
zapnutý M17 plugin – nezávisle na tom, který mód zrovna vlastní kanál. Cílem je,
aby žádný packet nezmizel: ani dlouhá zpráva, ani přenos bez koncové značky, ani
packet, který přijde během relace FM/D-Star.

## Co packet ztrácelo (a je opravené)

1. **Zaseknutý dekodér.** LSF se přijímal jen když dekodér nebyl „uvnitř“ jiného
   přenosu. Když relace neskončila čistě (nosič zmizel bez EOT), dekodér zůstal
   ve stavu stream a **ignoroval všechna další LSF** – tedy i packet.
2. **Více-rámcové packety.** Rámce se přijímaly jen v přesné posloupnosti
   (`číslo rámce == předchozí + 1`) a vnitřní stav po resetu byl `0xff`, takže
   se první rámec nikdy nepřijal. Prakticky fungovaly jen jedno-rámcové SMS
   (do ~22 znaků textu); delší zprávy se vždy zahodily.
3. **Vymazaný synchronizátor.** Při startu relace se synchronizátor resetoval,
   čímž se zahodily vzorky, které už obsahovaly rámec následující hned po LSF.
   Typicky tak zmizel poslední (u krátké SMS jediný) rámec packetu.
   Druhá strana téže mince: když se stav dekodéru ztratil (falešná koncová
   značka nebo minutá hlavička dalšího pokusu), rámce, které následovaly,
   zůstaly bez hlavičky a nedaly se doručit.
4. **EOT ze šumu.** Relaci ukončovala jakákoli detekce koncové značky, i zjevně
   falešná (např. během příjmu packetu).
5. **Poslední rámec bez následujícího syncu.** Detekce packet rámce vyžadovala
   i shodu o 192 symbolů dál (další rámec nebo EOT). Když vysílač skončil hned
   po rámci, rámec se nenašel.
6. **M17 během relace jiného módu.** Když kanál vlastnil FM nebo D-Star plugin
   (včetně hang time), M17 plugin nedostával IQ vzorky a packet neviděl.
7. **Packety ze sítě.** Zpráva z APRS-IS směrem na RF se zahodila, pokud byl
   kanál v tu chvíli obsazený.
8. **Arbitráž podle síťového módu.** M17 packet ze sítě se odmítl, když
   „síťový mód“ (network.mode = auto = naposledy použitý lokální mód) nebyl
   M17 – typicky po restartu nebo po FM relaci. APRS zpráva pro stanici tak
   nedošla, i když byl kanál volný.
9. **Ticho v logu.** RF → síť packet se nelogoval vůbec a CRC chyby či
   nepoužitelné rámce mizely bez stopy.

## Co teď platí

- LSF s platným CRC **vždy** začne nový přenos; předchozí (i nedokončený) se
  korektně uzavře.
- Packet rámce se skládají podle čísla rámce, takže rámec mimo pořadí nebo po
  poškozeném rámci se uloží na správné místo místo zahození.
- Poslední packet LSF se drží ještě chvíli (64 rámců): pokud se stav mezitím
  ztratí, rámce se k němu přidruží a rozhodne až CRC payloadu. Po tichu na
  kanálu se držení uvolní, aby se hlavička nepřiřadila k něčemu vzdálenému.
- **Opakované pokusy se kombinují.** Rádio posílá zprávu víckrát (u OpenRTX
  třikrát). Rámce se ukládají podle čísla rámce a pro každé číslo vyhrává kopie
  s nejmenším počtem chyb, takže chybějící nebo poškozený rámec z prvního
  pokusu doplní druhý či třetí – místo aby celá zpráva propadla.
- **Hlavička i bez preamble.** U opakovaného pokusu rádio neposílá preamble
  znovu, takže se hlavička hledá i podle samotného LSF sync slova
  (`sync_lsf_sync_threshold`). Bez toho byly rámce slyšet výborně, ale zpráva
  neměla hlavičku a nedala se zařadit.
- Vzorkovací fáze se doladí podle stejné metriky, kterou byl burst rozpoznán
  (dřív se u rámců uvnitř přenosu hledání fáze míjelo účinkem, což zvyšovalo
  chybovost).
- Stejná zpráva doručená z dalšího pokusu se neposílá znovu: počítá se jako
  `packets_duplicate`, takže z APRS-IS není tři kopie jedné SMS.
- Délka zprávy se neurčuje podle čísla posledního rámce (to může být v šumu
  přečtené špatně), ale tak, že se zkusí všechny přípustné délky a rozhodne CRC
  payloadu.
- Synchronizátor se při startu ani konci relace nevymazává.
- Koncovou značku bere dekodér vážně jen ve chvíli, kdy je synchronizovaný; jinak
  ji ignoruje a relaci neukončí.
- Uvnitř packet přenosu stačí na rozpoznání rámce jediný sync (`sync_follow_threshold`).
- Dispečer pouští M17 i během relace jiného módu, takže packet se neztratí
  kvůli hang time FM nebo D-Star.
- Packety ze sítě čekají na volný kanál ve frontě (`network_packet_queue`,
  `network_packet_ttl_ms`); zahodí se jen po vypršení TTL, a to s hláškou.
- Arbitráž vysílače rozlišuje audio a data: packet se rezervuje přes
  `acquireTxPacket()`, takže ho nezablokuje jiný síťový mód. Hlasové audio
  reflectoru se dál řídí `network.mode`.
- Nic není tiché: každý packet, neplatné CRC i nepoužitelný rámec je v logu a
  v telemetrii.

## Log (stderr / journal)

```text
M17: LSF decoded
M17 RF->NET packet: dst=OK1ABC src=OK1XYZ payload=32 bytes ber=0.6% forwarded
M17 RF->NET packet CRC fail: dst=OK1ABC src=OK1XYZ len=12 ber=3.2% data=0561...
M17: LSF CRC fail (dst=N1ADJ src=OK1XYZ ber=1.1%)
M17 frame not usable: sync=0x75ff packet frame without a valid LSF
M17 NET TX packet: dst=OK1ABC src=OK1LBC payload=21 bytes
M17 NET RX: 1 packet(s) expired after 2000 ms without a free channel
```

## Telemetrie (`/api/plugins`, plugin M17)

| klíč | význam |
| --- | --- |
| `sync_count` | počet detekovaných sync burstů |
| `lsf_count`, `lsf_bad_crc` | přijaté / neplatné LSF |
| `resync_count` | kolikrát nový LSF ukončil předchozí (nedokončený) přenos |
| `packet_frames` | packet rámce zařazené do skládání |
| `packets_rx` | doručené packety (RF → síť) |
| `packets_bad_crc` | packet s chybným CRC (nedoručen, zalogován) |
| `frames_dropped` | rámce, které nešlo použít (bez LSF apod.) |
| `frames_reattached` | rámce přidružené k nedávné hlavičce po ztrátě stavu |
| `frames_improved` | rámce nahrazené lepší kopií z dalšího pokusu |
| `packets_duplicate` | opakované pokusy o již doručenou zprávu |
| `rf_packets`, `rf_packets_forwarded`, `rf_packets_not_forwarded` | kolik packetů z RF přišlo a kolik se jich poslalo do sítě |
| `net_packets_queued`, `net_packets_sent`, `net_packets_dropped`, `net_packets_pending` | fronta směru síť → RF |

Zdravý provoz při příjmu SMS: `packets_rx` roste a `packets_bad_crc` zůstává 0.
Když roste `packets_bad_crc` nebo `frames_dropped`, signál je slabý na to, aby
rámce prošly FEC/CRC – pak má smysl povolit prahy (`sync_*_threshold`) nebo
zkontrolovat úroveň/vf cestu.

## Ladění v konfiguraci

```yaml
m17:
  # GATE - rozhoduje, zda je na kanálu skutečné M17
  sync_lsf_threshold: 6.0      # preamble + LSF sync (nižší = přísnější)
  sync_packet_threshold: 4.5   # packet sync + následující rámec
  sync_stream_threshold: 4.5   # stream sync + následující rámec
  sync_eot_threshold: 4.5      # značka konce přenosu
  sync_lsf_sync_threshold: 3.5 # sync-only LSF, jen pro TRACKING
  sync_follow_threshold: 4.0   # samotný packet sync uvnitř packet přenosu
  max_lsf_ber_percent: 9.0
  rssi_gate_dbfs: -55.0

  # TRACKING - měkčí příjem po validním LSF
  tracking_sync_lsf_threshold: 7.5
  tracking_sync_packet_threshold: 6.0
  tracking_sync_stream_threshold: 6.0
  tracking_sync_eot_threshold: 5.0
  tracking_sync_lsf_sync_threshold: 4.5
  tracking_sync_follow_threshold: 5.5
  max_tracking_ber_percent: 25.0

  min_sync_burst: 5            # kolik synců v 500 ms vyhlásí M17 jako mód
  idle_timeout_ms: 1500        # konec relace po tichu
  network_packet_queue: 8
  network_packet_ttl_ms: 2000
```

Prahy vycházejí z měření reálné stanice (OpenRTX): skutečné LSF mají vzdálenost
1,4–4,4, rámce 2,7–3,8 a cokoliv, co sync slovo není, zůstává nad ~11,5. Výchozí
hodnoty proto leží v této mezeře — dřívější práh LSF 4,5 byl uvnitř rozptylu
skutečných přenosů, takže se ztrácely celé zprávy, když byla slabá jen hlavička
(rámce byly slyšet dobře, ale bez hlavičky se nedaly doručit).

- `min_sync_burst` ovlivňuje jen to, kdo dostane kanál. Dekódování packetů na něm
  nezávisí, takže krátká SMS se neztratí ani při vysoké hodnotě.
- GATE používá přísné prahy, RSSI gate a BER limit LSF. Sync-only LSF cesta je
  v GATE vypnutá, protože v šumu vytvářela falešné kandidáty.
- Po validním LSF se přepne TRACKING s měkčími prahy a vyšším BER. Tím se
  neztratí slabý stream nebo packet frame, ale falešná hlavička sama mód
  neotevře.
- Prahy zvyšuj po malých krocích (např. o 0,5) – čím vyšší, tím víc falešných
  synců v šumu.

## Testy

```text
cd build && ninja && ctest -R m17 --output-on-failure
```

- `m17_packet_frames` – jedno- i dvou-rámcová SMS, přenos bez EOT, dva packety za
  sebou, packet po nedokončeném streamu.
- `m17_packet_pipeline` – celý řetězec IQ → plugin → síť po malých blocích
  (přesně situace, kdy se dřív ztrácel rámec po LSF); přes UDP se ověří, že do
  bridge dorazí packet se správným textem.
- `dispatcher` – ověřuje, že plugin, který nevlastní relaci, dál dostává IQ
  přes `detect()` (M17 packet tak nepřijde o rámec, když kanál drží jiný mód).
- `session_manager` – ověřuje, že packet projde i když je síťový mód jiný
  (`acquireTxPacket` vs. `acquireTx`).

## Diagnostika reálného přenosu

Když je potřeba zjistit, co rádio skutečně poslalo (kolik kopií, jak daleko byl
sync od ideálu, v jakém stavu byl dekodér), zapni v konfiguraci `log_sync`:

```yaml
m17:
  log_sync: true
```

Pak se do journalu loguje každý burst:

```text
M17 sync: LSF dist=1.42 decoder=idle
M17 sync: packet dist=0.91 decoder=packet
M17 sync: packet dist=1.05 decoder=packet
M17 sync: EOT dist=2.10 decoder=idle
```

LSF a packet rámce se logují vždy, stream bursty nejvýš jednou za sekundu
(u hlasu jich je 25/s). Po testu `log_sync` zase vypni.
