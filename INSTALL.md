# Installation

`install.sh` baut das Modul **auf deinem Gerät gegen dein installiertes SvxLink**
(funktioniert mit alten wie neuen Versionen) und ist so gebaut, dass es dein
**laufendes Relais nicht kaputt machen kann**: bei jedem Fehler stellt es den
vorherigen Zustand automatisch wieder her.

## Zuerst nur prüfen (ändert nichts)
```
sudo ./install.sh --check
```
Läuft alle Voraussetzungs-Checks durch und baut das Modul probeweise. Zeigt, ob
eine saubere Installation möglich wäre — **ohne** etwas am System zu ändern.

## Installieren
```
sudo ./install.sh --radioid <DEINE-RADIOID>
```
Ablauf:
1. **Pre-Flight-Checks** — SvxLink, Version, Header, Build-Tools, Schreibrechte.
   Fehlt etwas → klare Meldung + Fix-Befehl, **nichts wird geändert.**
2. **Bauen** (Codec + Modul) — noch nichts am System.
3. **Vorschau + Bestätigung** — es zeigt genau, was installiert/geändert wird,
   plus Haftungshinweis. Du bestätigst mit `j`.
4. **Installieren** (transaktional, mit Backup), **MODULES** ergänzen.
5. **Test-Neustart + Verifikation** — kommt svxlink nicht sauber hoch →
   **automatischer Rollback**, Relais läuft wieder wie vorher.

Nur die eine Config-Zeile (`MODULES=…`) + ein eigener `.conf` in `svxlink.d/`
werden angefasst — Rx/Tx, Audio, Logic und Kennung bleiben unberührt.

## Status prüfen
```
sudo ./install.sh --status
```
Zeigt: installiert? in MODULES? svxlink aktiv? Modul geladen? Version.

## Deinstallieren
```
sudo ./install.sh --uninstall
```
Entfernt nur, was der Installer angelegt hat (laut Manifest), löst
ModuleTetraBrew aus `MODULES` und startet svxlink neu.

## Optionen
| Option | Wirkung |
|---|---|
| `--check` | nur prüfen + probeweise bauen (ändert nichts) |
| `--status` | Zustand anzeigen |
| `--uninstall` | sauber entfernen |
| `--radioid N` | RadioID/ISSI (sonst Abfrage) |
| `--ident-hook` | optionalen CW-Status-Hook mitinstallieren (Default: aus) |
| `--yes` / `-y` | keine Rückfragen (Automatik) |

## Haftung
Der Einbau erfolgt **auf eigene Verantwortung**. Der Installer sichert alles und
rollt bei Fehlern automatisch zurück — eine 100%-Garantie gibt es aber nie. Wer
lieber von Hand einbaut: Modul bauen (`build.sh`) und `MODULES=…,ModuleTetraBrew`
selbst setzen (siehe `examples/` und `BEDIENUNG.md`).
