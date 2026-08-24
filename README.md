# ModuleTetraBrew

**Häng deinen SvxLink-FM-Repeater über BREW an ein TETRA-Netz — und mach mit bei [FreeTetra](https://freetetra.de).**

Ein natives SvxLink-Modul (wie ModuleEchoLink), das einen analogen **FM-Repeater** über das
**BREW-Protokoll** (TETRA, ACELP-Codec) mit einem oder mehreren TETRA-Netzen verbindet.
Talkgroup-Wahl per DTMF, deutsche Sprachansagen, läuft auf **alten wie neuen** Repeatern.

---

## Die Idee: FreeTetra 🌐

[**freetetra.de**](https://freetetra.de) ist ein **freier, offener BREW-Server**, der einzelne
**TETRA-BlueStations** und **FM-Repeater** miteinander vernetzt. Wer eine **RadioID** hat, kann
connecten — kein Account, keine Anmeldung. So entsteht ein gemeinsames Amateurfunk-TETRA-Netz,
in dem sich FM- und TETRA-Leute treffen.

- 📻 **FM-Repeater** kommen über *dieses Modul* rein.
- 📡 **TETRA-BlueStations** sprechen BREW von Haus aus.

**TETRA + FM ist das Geile.** 😎 Bei wenig Betrieb bringt das die Modi und die Leute
zusammen, statt dass jeder ein eigenes Gerät für jedes Netz braucht.

---

## Was das Modul kann

- **FM ↔ TETRA** in beide Richtungen, echter ACELP-Codec (TETRA-Qualität, kein DMR-Doppel-Transcode)
- **Mehrere Netze gleichzeitig** — per (frei konfigurierbarem) Talkgroup-Bereich legst du fest, welche TG zu welchem Server geht (bei nur einem Server: alle TGs dorthin)
- **DTMF:** `5#` = an (Default-TG) · `5<tg>#` = an + direkt diese TG · `<tg>#` = TG wechseln · `#` = aus
- **Deutsche Sprachansagen** („Verbunden bei FreeTetra, eingebucht in Talkgroup …") + Status an der CW-Kennung
- **Sicherheit:** GSSI-Whitelist (Troll-Bremse), Inaktivitäts-Timeout, FM→TETRA-Durchgangslimit
- **Alles in der `svxlink.conf`** — nichts hartcodiert, jeder Sysop zeigt auf seine eigenen Server
- Läuft auf **altem SvxLink (armhf)** genauso wie auf neuem

---

## Schnellstart: an FreeTetra 🚀

Nach der [Installation](#installation) in die `svxlink.conf` (bzw. `svxlink.d/ModuleTetraBrew.conf`):

```ini
[ModuleTetraBrew]
NAME=TetraBrew
ID=5
TIMEOUT=10800
SRC_ISSI=<deine RadioID, z.B. DEINE_RADIOID>
DEFAULT_TG=1
BREW_SERVERS=FreeTetra

[ModuleTetraBrew_FreeTetra]
HOST=127.0.0.1        ; lokaler TLS-Proxy (install.sh richtet ihn ein)
PORT=18443
HOST_HEADER=freetetra.de
USER=<deine RadioID>
PASSWORD=freetetra    ; öffentliches Community-Passwort
REALM=brew
GSSI_MIN=1
GSSI_MAX=16777215     ; nur ein Server -> ganzer Bereich (Aufteilung nur beim Splitten nötig)
```

Und in deiner Logic-Sektion `MODULES=...,ModuleTetraBrew` ergänzen. Fertig — auftasten, `5#`,
und du bist im FreeTetra-Netz.

> **Du brauchst:** einen laufenden SvxLink-Repeater, deine **RadioID** (radioid.net), `python3`
> (für den TLS-Proxy zu `wss://freetetra.de`) und `g++` (zum Bauen). Mehr nicht.

---

## Installation

```bash
git clone https://github.com/do1xx/svxlink-module-tetrabrew.git
cd svxlink-module-tetrabrew
sudo ./install.sh
```

`install.sh` baut den ACELP-Codec (`libtetra-codec`) und das Modul **gegen dein installiertes
SvxLink** (⚠️ das Modul ist versions-gebunden — es wird lokal kompiliert, damit es exakt passt),
installiert `.so` / `.conf` / `.tcl` / Sounds, richtet den TLS-Proxy als systemd-Service ein und
sagt dir, was du noch in die `svxlink.conf` eintragen musst.

---

## Konfiguration

**Global `[ModuleTetraBrew]`:**

| Key | Zweck |
|---|---|
| `TIMEOUT` | Inaktivitäts-Auto-Release (Sek), `0` = nie |
| `DEFAULT_TG` | Talkgroup beim Aktivieren |
| `SRC_ISSI` | Standard-ISSI/RadioID des Relais (pro Server überschreibbar) |
| `BREW_SERVERS` | Liste der Endpunkte (Namen frei), z.B. `FreeTetra,Weiteres` |
| `MAX_TX_TIME` | max. FM→TETRA-Durchgang (Sek), `0` = aus |
| `STATUS_INTERVAL` | periodische „verbunden mit"-Ansage (Sek), `0` = aus (bzw. an CW-Kennung koppeln) |
| `ANNOUNCE` | Sprachansagen an/aus |

**Pro Endpunkt `[ModuleTetraBrew_<Name>]`:**

| Key | Zweck |
|---|---|
| `HOST` `PORT` `HOST_HEADER` | Verbindung (direkt oder über lokalen TLS-Proxy) |
| `USER` `PASSWORD` `REALM` | Digest-Auth (RadioID + Passwort) |
| `SRC_ISSI` | ISSI-Override für dieses Netz |
| `GSSI_MIN` `GSSI_MAX` | **Routing:** welcher Endpunkt kriegt welche Talkgroup |
| `GSSI_ALLOW` | **Policy:** nur diese TGs wählbar (leer = ganzer Bereich) — Troll-Bremse |
| `RX_GAIN` | Lautstärke TETRA→FM für dieses Netz |

---

## Mehrere Netze gleichzeitig

Ein Relais kann an **mehreren BREW-Servern gleichzeitig** hängen, aufgeteilt nach Talkgroup-Bereich —
genau wie eine BlueStation mit zwei `[[brew]]`-Blöcken, aber nativ im Modul:

```ini
BREW_SERVERS=FreeTetra,Weiteres
# [ModuleTetraBrew_FreeTetra]  GSSI_MIN=1  GSSI_MAX=90         -> FreeTetra
# [ModuleTetraBrew_Weiteres]   GSSI_MIN=91 GSSI_MAX=16777215   -> ein weiterer BREW-Server
#                              GSSI_ALLOW=...                  -> nur erlaubte TGs
```

Was du als zweites Netz dranhängst, bleibt dir überlassen — es geht **jeder BREW-fähige Server**
(ein anderes TETRA-Netz, ein Gateway o.ä.). 😉 ⚠️ Wenn du in ein fremdes/belebtes Netz brückst:
`GSSI_ALLOW` + regionale/Test-TGs nutzen, dessen Regeln beachten — du bist verantwortlich.

---

## Wie's funktioniert

- Das Modul ist gleichzeitig `AudioSink` (FM-RX) und `AudioSource` (FM-TX) in der SvxLink-Pipeline.
- Der BREW-Client läuft auf `Async::TcpClient` (HTTP-Digest + WebSocket + BREW-Binärprotokoll),
  ACELP über `libtetra-codec`, Resampling 16↔8 kHz.
- **TLS:** altes SvxLink/Async kann kein TLS → ein winziger `python3`-TLS-Proxy terminiert TLS
  lokal (`install.sh` richtet ihn ein). Auf neuem SvxLink kommt später natives TLS.

---

## Lizenz

GPL v2+ (wie SvxLink). Nutzung auf eigene Verantwortung — du bist als Repeater-Verantwortlicher
für alles zuständig, was über deine Brücke ausgesendet wird.

*73 & viel Spaß beim Vernetzen. — DO1XX*
