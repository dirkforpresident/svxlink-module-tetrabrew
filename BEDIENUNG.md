# Bedienung — FreeTetra-Brücke (FM ↔ TETRA)

Die Brücke verbindet einen analogen **FM-Repeater** mit dem **TETRA-Netz** über
den FreeTetra-BREW-Server. Sie lässt sich **von beiden Seiten** steuern: per DTMF
vom FM-Repeater und per SDS vom TETRA-Funkgerät.

Der Repeater ist als eigener TETRA-Teilnehmer (ISSI) dauerhaft eingebucht und
steht im Verzeichnis unter **https://freetetra.de/nodes.html** (online / Standby /
aktiv + Talkgroup).

---

## Von der FM-Seite (DTMF)

Der Repeater kennt das Modul unter der DTMF-Kennung **`5`**.

| Aktion | DTMF | Wirkung |
|---|---|---|
| Brücke **aktivieren** (Heimat-TG) | `5#` | Bucht auf die eingestellte Default-TG ein |
| Brücke aktivieren **auf TG X** | `5<X>#` z.B. `51#` | Aktiviert und bucht direkt auf TG X |
| **Talkgroup wechseln** (wenn aktiv) | `<X>#` z.B. `8#` | Wechselt auf TG X |
| Brücke **trennen** | `#` | Zurück in Standby |

> ⚠️ **Achtung Zahlenfalle:** Die Modul-ID ist `5` — und `TG 5` ist auch gültig.
> Ist die Brücke **schon an** und du tippst nochmal `5#`, heißt das „geh auf TG 5".
> Also: einmal `5#` zum Einschalten, danach die TG **ohne** die 5 wählen (`8#`, nicht `58#`).

Beim Einbuchen sagt der Repeater die TG an; wenn TETRA gesprochen wird, tastet der
FM-Sender auf. Ein laufendes FM-QSO hat Vorrang — TETRA-Audio füllt nur die Pausen.

---

## Von der TETRA-Seite (SDS)

Text-SDS an die **Repeater-ISSI** (z.B. `262102` für DO0RAM) schicken:

| Inhalt der SDS | Wirkung |
|---|---|
| `<tg>` z.B. `1` oder `262` | Repeater bucht sich auf **TG X** ein |
| `0` oder `off` | Repeater **trennt** → Standby |

Der Repeater antwortet per SDS: **„DO0RAM: TG 1 aktiv"** bzw. **„getrennt"**.
Deine SDS wird quittiert (kein „message failed").

> **Wichtig (Model 1):** Du musst selbst auf der TG sein, die du dem Repeater nennst.
> Bist du auf TG 1 und schickst `1`, treffen sich beide auf TG 1 → ihr redet.

---

## Was passiert im Hintergrund

- **Standby:** Der Repeater bleibt eingebucht, ohne auf einer TG zu brücken — er
  ist immer im Verzeichnis sichtbar und per SDS erreichbar, der Repeater bleibt
  normal für FM nutzbar.
- **Auto-Rückfall:** Steht die Brücke lange still auf einer fremden TG, fällt sie
  automatisch auf die Default-TG zurück (`TG_IDLE_RESET`).
- **Durchgangs-Schutz:** FM→TETRA und TETRA→FM sind je auf 3 Minuten pro Durchgang
  begrenzt (`MAX_TX_TIME` / `RX_MAX_TIME`) — kein hängender Sender blockiert die Brücke.

## Wichtigste Config-Schlüssel (`ModuleTetraBrew.conf`)

| Schlüssel | Bedeutung |
|---|---|
| `CALL` | Rufzeichen des Repeaters → daraus feste, diallbare ISSI + Login-Name |
| `DEFAULT_TG` | Heimat-TG beim Aktivieren |
| `STANDBY=1` | Dauerhaft eingebucht ohne TG (Verzeichnis + SDS) |
| `TG_IDLE_RESET` | Sekunden Ruhe auf Fremd-TG → zurück auf Default-TG (0 = aus) |
| `MAX_TX_TIME` / `RX_MAX_TIME` | Max. Durchgang FM→TETRA / TETRA→FM (Sek) |
| `SRC_ISSI` | Optionaler ISSI-Override (eigene DMR-RadioID; pro Server setzbar). Das ist auch die Ziel-ISSI für SDS an den Repeater. |
