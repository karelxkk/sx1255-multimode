# D-Star gateway (DSRP)

D-Star plugin umí kromě lokálního RF převaděče i repeaterovou stranu protokolu
DSRP, kterým mluví `ircDDBGateway` / `DStarGateway`. Díky tomu lze D-Star
používat i bez lokální D-STAR stanice: provoz přichází z brány a je vysílán na
RF, lokální RF provoz je naopak odesílán do brány.

## Konfigurace

```yaml
modes:
  dstar:
    enabled: true
    rx_invert: true           # polarita D-Star RX na SX1255
    callsign: OK1LBC
    tx_preamble_bytes: 210
    tx_tail_ms: 50
    gateway:
      enabled: true
      address: 127.0.0.1      # kam posílat DSRP pakety (gateway)
      port: 20010             # UDP port gatewaye
      local_address: 127.0.0.1
      local_port: 20011       # kde repeater naslouchá odpovědi gatewaye
      name: sx1255-multimode

network:
  mode: dstar                 # auto | fm | dstar | m17
```

Porty musí odpovídat konfiguraci gatewaye. Pro `dstarrepeaterd` to bývá
`GatewayAddress/GatewayPort` a `LocalAddress/LocalPort`; pro samostatný
`ircDDBGateway` si hodnoty ověř v jeho konfiguraci.

`rx_invert: true` odpovídá změřené polaritě SX1255 RX cesty. Detektor používá
kanálový filtr 7,5 kHz, decimaci 125 → 25 kSa/s a lineární převzorkování na
48 kSa/s podle referenčního DStarRepeateru.

TX cesta používá sample-and-hold 48 → 125 kSa/s a kladnou FM deviaci pro
datový bit 1. Tato kombinace byla ověřena příjmem na Icom ID-52E; původní
úzký resampler a opačná polarita produkovaly signál, který stanice nedekódovala.

### Hlavička z radiostanice

Pro hotspot `OK1LBC B` s bránou `OK1LBC G` má lokální D-Star hlavička obsahovat:

```text
RPT1 = OK1LBC B
RPT2 = OK1LBC G
YOUR = CQCQCQ
MY1  = osobní volací znak
```

U Icom ID-52 nastav `MENU > SET > My Station > My Call Sign`, potom v režimu
DR zvol přístupový opakovač `OK1LBC B` (438,575 MHz, shift −7,6 MHz) a cíl
`CQCQCQ`, případně `Use Reflector` → `XRFOK1 D`. Prázdné RPT1/RPT2/MY1 sice
může mít platné CRC, ale gateway nemá kam relaci přiřadit.

### Instalace DStarGateway

Na cílovém RPi se gateway instaluje z balíčku (cesta odpovídá zdrojovému
adresáři, ze kterého byl postaven):

```text
sudo apt install ./dstargateway_0.7-2_arm64.deb
sudo nano /etc/dstargateway/dstargateway.cfg
sudo systemctl restart dstargateway
```

Minimální úprava konfigurace pro homebrew repeater:

```ini
[Gateway]
callsign=OK1LBC
hbAddress=127.0.0.1
hbPort=20010

[Repeater_1]
enabled=true
band=B
callsign=OK1LBC
address=127.0.0.1
port=20011
type=hb

[APRS]
enabled=false
```

`DStarGateway` si z `callsign` a `band` sestaví repeater `OK1LBC B` a
gateway callsign doplní na `OK1LBC G`.

### Hotspot a český DCS reflector

Pro osobní hotspot není potřeba samostatný gateway callsign; použije se vlastní
znak a DStarGateway se přepne do režimu `hotspot`. Pro český DCS019
(`dcs019.xreflector.net`, DCS port 30051) nastav:

```ini
[Gateway]
type=hotspot
callsign=OK1LBC

[Repeater_1]
enabled=true
band=B
callsign=OK1LBC
reflector=DCS019 B
reflectorAtStartup=true
frequency=438.575
offset=-7.6

[DCS]
enabled=true
```

`reflector` se uvádí jako osmiznakový `DCS019 B` (skupina B, česká národní
skupina). Kratší `DCS019` umí DStarGateway 0.7 shodit na `basic_string::at`
při sestavování DCS paketu. Varianta `DCS019 G` se připojí na gateway modul a
dashboard pak u hotspotu zobrazuje `Group Not Defined`; pro běžný provoz je
potřeba zvolit konkrétní skupinu, např. `DCS019 B`.
Po restartu se v logu objeví `DCS link to DCS019 B established`.

### Statická poloha

Pro statickou polohu hotspotu vyplň `latitude`/`longitude` v `[Gateway]` i v
`[Repeater_1]` a v `[APRS]` nastav `positionSource=fixed`:

```ini
[Gateway]
latitude=<LAT>
longitude=<LON>

[Repeater_1]
latitude=<LAT>
longitude=<LON>

[APRS]
enabled=false
positionSource=fixed
```

Hodnoty se použijí pro QTH hlášení do ircDDB/DCS; APRS může zůstat vypnuté,
pokud pro něj nemáš vlastní login.

## Výběr síťového zdroje

Směr síť → RF řídí arbiter. Výchozí `network.mode: auto` povolí síťový provoz
jen toho módu, který byl naposledy použit lokálně. Protože D-Star gateway
často běží bez lokální D-STAR stanice, nastav `network.mode: dstar`, aby brána
mohla klíčovat i bez předchozího lokálního D-Star provozu.

Při `network.mode: auto` je po každém restartu nutné nejprve jednou zaklíčovat
lokálně D-Star, aby se poslední lokální mód nastavil na D-Star. Pro hotspot
používaný výhradně pro D-Star je proto vhodnější `network.mode: dstar`.

## Provoz

- `HEADER` z gatewaye: plugin získá TX arbiter, klíčuje a re-moduluje
  D-Star hlavičku.
- `DATA` z gatewaye: DV rámce jsou re-modulovány se souvislým GMSK stavem.
- konec streamu: vyšle se D-Star end-of-transmission sekvence, `drainTx` a
  odklíčování.
- watchdog odklíčí, pokud gateway přestane posílat data déle než 3 s. Pokud
  nepřišel DSRP END, současně uvolní příchozí stream ID, aby další relace
  nezačala být tiše odmítána.
- lokální RF příjem: hlavička a DV rámce se odesílají do gatewaye, EOT jako
  `NULL AMBE + END_PATTERN`.

Stav a čítače jsou v `/api/plugins` (`gateway_state`, `gateway_rx_frames`,
`gateway_tx_frames`, `gateway_invalid_packets`,
`gateway_input_stream_resets`, `net_tx_ended`, `net_tx_timeouts`).

## Ověření

```text
test_dstar_gateway     # DSRP UDP round-trip proti falešnému gateway procesu
test_dstar_netplugin   # gateway -> plugin -> falešný ITransmit/arbiter
dstar_iq_audit capture.iq 1.2 5
                       # offline kontrola syncu, polarity a obsahu hlavičky
```
