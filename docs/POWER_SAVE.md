# Úspora CPU v klidovém režimu

SX1255 multimode zůstává v režimu `DETECTING` i tehdy, když je kanál
klidný. Trvalé spouštění všech plugin detektorů v tomto stavu zbytečně
zatěžuje CPU Raspberry Pi.

## Chování

Konfigurace:

```yaml
power_save:
  enabled: true
  sleep_below_dbfs: -57.0
  wake_above_dbfs: -55.0
  prebuffer_ms: 250.0
```

Pokud není aktivní žádná relace a IQ výkon klesne pod
`sleep_below_dbfs`, pozastaví se volání plugin detektorů FM, D-Star a M17.
Mezi oběma prahy existuje hystereze, takže režim nekmitá na šumu.

Jakmile některý blok IQ překročí `wake_above_dbfs`, detekce se obnoví
ještě pro tento blok. M17 má 160ms preamble, D-Star dlouhou preamble
a FM používá 700ms potvrzení nosné, takže probuzení má dostatečnou
rezervu.

FFT, waterfall, webový dashboard a IQ capture zůstávají v klidovém režimu
aktivní. Nevypíná se napájení SX1255 ani LNA; úspora se týká především
CPU a tím i spotřeby Raspberry Pi. Přínos na samotném RF čipu by vyžadoval
ještě obvodové měření a samostatný návrh duty-cycle příjmu.

## Diagnostika

Přechod se zapíše do journalu:

```text
POWER: detection paused rssi=-59.5 dBFS
POWER: detection resumed rssi=-52.0 dBFS
```

Stav je také v `GET /api/status` jako `power_save` a na dashboardu
v hlavičce jako `úspora aktivní/vypnuto`.

## Omezení

- Signál, který trvale zůstává pod `wake_above_dbfs`, nemusí být
  detekován. Při provozu se slabšími stanicemi je proto nutné práh snížit.
- Režim se aktivuje pouze ve stavu `DETECTING`; aktivní relace, síťový
  přenos ani doběh FM se nepozastavují.
- M17 a D-Star startují v klidovém režimu studené: první LSF/preamble může
  přijít dřív, než se detektory stihnou synchronizovat. Proto se posledních
  `prebuffer_ms` vzorků při probuzení přehraje detektorům ještě před aktuálním
  blokem; reálná data pak navazují. Prebuffer neovlivní FFT, waterfall ani
  IQ capture, protože ty stále vidí původní živý stream.
