# FM RX during SVXLink TX

## Výsledek auditu

Chování bylo **nechtěný vedlejší efekt** opravy proti pronikání digitálních
módů do SVXLinku. Nebyl to požadavek původního návrhu.

Příčina byla v `IQDispatcher` ve stavu `ACTIVE_SVXLINK`, který nemá
vlastnící plugin:

```cpp
plugin->setRxBlocked(plugin->mode() == Mode::FM);
```

Tím se FM detektor stále spouštěl, ale `FmPlugin` už neposílal RX IQ do
`FmSvxlinkBridge`.

## Kdo co rozhodoval

| Vrstva | Chování |
|---|---|
| `SessionManager` | během síťového TX nepřiděluje novou lokální relaci |
| `IQDispatcher` | rozhodoval o zablokování FM RX feedu |
| `FmPlugin` | při `m_rxBlocked` vynechal `pushRxIq()` |
| `FmSvxlinkBridge` | TX část fungovala dál; RX část jen nedostávala vzorky |

FM detektor nebyl vypnutý. Počítal RSSI a vracel `Detection`, ale audio
nebylo předáno do SVXLinku.

## Nové rozhodování

Ve stavu `ACTIVE_SVXLINK`:

- lokální FM RX se normálně předává do SVXLinku,
- FM detekce běží,
- nová lokální FM relace se během síťového TX stále nepřiděluje, protože
  vysílač vlastní SVXLink,
- pokud je na stejném bloku detekován M17 nebo D-Star, FM feed se potlačí
  důvodem `session_busy`, aby digitální signál nepronikl jako FM.

Ve stavu `ACTIVE_M17_NETWORK` je FM RX blokován důvodem `tx_locked`.

## Logy

```text
FM DETECT reason=... rssi=... detected=0|1
FM ACCEPT reason=svxlink_tx_active rssi=...
FM REJECT reason=session_busy rssi=...
FM REJECT reason=tx_locked rssi=...
```

`FM DETECT` znamená běžící FM detektor. `FM ACCEPT` znamená předání RX
audio do SVXLink bridge, nikoli přidělení nové RF relace.

## Ověření

`test_dispatcher` ověřuje:

- `ACTIVE_SVXLINK` bez digitálního signálu: FM není blokován,
- stejný stav s M17: FM je blokován důvodem `session_busy`,
- po uvolnění síťového TX lze M17 relaci normálně přidělit,
- `ACTIVE_M17_NETWORK`: FM je blokován důvodem `tx_locked`.
