# DM-1701 / OpenRTX M17 dataset

Cíl je 20 až 50 samostatných reálných vysílání z Baofeng DM-1701 s OpenRTX.
Každé `.iq` má vedle sebe `metadata.json` s RSSI, AFC korekcí, výsledkem
detekce a SyncDistance.

Postup na cílovém RPi:

```bash
sudo systemctl stop sx1255-multimode
/home/xkk/sx1255-diagnostics/scripts/m17_dm1701_record.sh voice_01 12
sudo systemctl start sx1255-multimode
```

Výstup se ukládá do
`/home/xkk/sx1255-diagnostics/datasets/m17/dm1701/`. Ve zdrojovém stromu lze
použít stejný příkaz jako `scripts/m17_dm1701_record.sh`.

Vysílej po vypsání instrukce po celou dobu záznamu. Pro dataset je vhodné
střídat krátké voice overs, SMS/packet a různé vzdálenosti/úrovně signálu.
Soubor `.iq` zůstává lokálně; do gitu patří metadata a analýzy.
