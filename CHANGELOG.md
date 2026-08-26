# Changelog — FreeTetra-Brücke

Änderungen an der Brücke (ModuleTetraBrew) und dem FreeTetra-Umfeld
(Server, DO0RAM-Dashboard). Neue Funktionen und Verbesserungen ausführlich,
Fehlerbehebungen nur kurz.

## 2026-08-26

### Neue Funktionen
- **SDS-Steuerung von der TETRA-Seite.** Text-SDS mit TG-Nummer an die Repeater-ISSI
  bucht die Brücke auf diese TG ein (`0`/`off` = trennen). Der Repeater antwortet per
  SDS („TG 1 aktiv" / „getrennt") und quittiert eingehende SDS (kein „message failed").
  Bedienbar mit jedem TETRA-Funkgerät.
- **Persistent-Standby.** Der Repeater bleibt dauerhaft im TETRA-Netz eingebucht (ohne
  TG zu brücken) — immer im Verzeichnis sichtbar und per SDS erreichbar, während er
  normal für FM nutzbar bleibt. Brücke wird on-demand geschaltet.
- **Live-Knotenverzeichnis** unter `freetetra.de/nodes.html`: zeigt alle Knoten mit
  Status **offline / Standby / aktiv + Talkgroup**. Rufzeichen werden automatisch aus
  der RadioID-Datenbank (radioid.net) aufgelöst.
- **QSO-Audio-Archiv** (DO0RAM): echte Sprach-QSOs werden aufgehoben (rotierend), zum
  Anhören/Zeigen der Audioqualität.
- **Default-TG im Dashboard** einstellbar; zeigt beim Laden den aktuellen Wert.

### Verbesserungen
- **Durchgangs-Zeitsperre in beide Richtungen** (`MAX_TX_TIME` / `RX_MAX_TIME`, je 3 Min):
  ein hängender Sender (FM oder TETRA) kann die Brücke bzw. das Relais nicht mehr
  dauerhaft blockieren.
- **Auto-Rückfall auf die Heimat-TG** (`TG_IDLE_RESET`): steht die Brücke lange still auf
  einer fremden TG, schaltet sie von allein zurück auf die Default-TG.
- **Default-TG** auf eine sinnvolle Heimat-TG gesetzt (statt Brandmeister-Welt).
- **Kürzeres Squelch-Nachlaufen** am Repeater (weniger Rauschen nach dem Loslassen).

### Fehlerbehebungen
- FM→TETRA ging nicht (nur TETRA→FM) — behoben.
- „message failed" beim SDS-Senden — behoben (Empfangsbestätigung).
- Doppelte Einbuch-Ansage auf FM — behoben.
