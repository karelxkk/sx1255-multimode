# Připojení FlexNetu

Tento dokument je závazný návrh architektury pro budoucí „připojení FlexNetu“.
Cílem je, aby multimode fungoval jako FlexNet node s uplinkem přes AXIP/AXUDP
a uživatelé se připojovali přes M17 na RF.

## Zvolená architektura

```text
M17 uživatelé (RF)
        │  M17 packet mode (PacketTypeAX25)
        ▼
Pi Zero + SX1255 = M17 packet modem / TNC
        │  KISS nebo raw AX.25 rámce po UDP/TCP
        ▼
Pi 5 = FlexNet gateway + APRS + user access
        │  AXUDP/AXIP
        ▼
vj.ham-radio.cz (FlexNet síť)
```

Rozdělení práce:

| kde | co běží |
|---|---|
| Pi Zero | multimode, SX1255, M17 packet TX/RX, bridge AX.25 rámců na KISS/UDP |
| Pi 5 | Linux AX.25, `flexnetd`/`linbpq-flexnet`, AXUDP peer, URONode, APRS (Direwolf/LinBPQ) |
| FlexNet | AX.25 connected-mode, PID `0xCE`/`0xCF`, routing a digipeater chain |

## Proč ne FlexNet node na Pi Zero

- Pi Zero už teď zvládá multimode RF/DSP přibližně na 50 % jednoho jádra.
- AX.25 LAPB + FlexNet routing + APRS by přidaly latenci a mohly ohrozit
  spolehlivost RF části.
- Pi 5 má dost výkonu a může používat odzkoušené nástroje.

## Fáze implementace

1. M17 packet TX/RX a bridge AX.25 rámců (M17 packet mode) – základ je hotový
   v multimode.
2. Na Pi 5 rozjet Linux AX.25 + `flexnetd`/`linbpq-flexnet` a AXUDP peering
   s `vj.ham-radio.cz`.
3. Propojit Pi Zero ↔ Pi 5 přes KISS/UDP a ověřit AX.25 link.
4. URONode pro uživatelský přístup z M17.
5. Volitelně APRS/IGate.

## Vybavení

- Pi Zero + SX1255 HAT + anténa (existující).
- Pi 5 + internet.
- Síťové propojení Pi Zero ↔ Pi 5.
- Údaje k FlexNet serveru: AXIP/AXUDP port, peer callsign, node callsign/SSID.
- M17 rádio/software na straně uživatele pro test.
- APRS-IS passcode, pokud se zapojí i APRS.

## Poznámka

Toto je plán, ne hotová implementace. Aktuální práce se soustředí na M17
packet bridge pro zprávy a polohu (`docs/M17_BRIDGE.md`).
