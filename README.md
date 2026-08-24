# ModuleTetraBrew

**Häng deinen SvxLink-FM-Repeater über BREW an ein TETRA-Netz — und mach mit bei [FreeTetra](https://freetetra.de).**

Ein natives SvxLink-Modul (wie ModuleEchoLink), das einen analogen **FM-Repeater** über das
**BREW-Protokoll** (TETRA, ACELP-Codec) mit einem oder mehreren TETRA-Netzen verbindet.
Talkgroup-Wahl per DTMF, deutsche Sprachansagen, läuft auf **alten wie neuen** Repeatern.

---

## In 3 Schritten am Netz ⏱️

Noch nie ein SvxLink-Modul von Hand installiert? Kein Problem — `install.sh` macht die ganze
Arbeit (Codec + Modul bauen, alles kopieren, TLS-Proxy einrichten). Du machst nur **drei Dinge**:

```bash
# 1) Holen + installieren (baut & installiert alles automatisch)
git clone https://github.com/do1xx/svxlink-module-tetrabrew.git
cd svxlink-module-tetrabrew
sudo ./install.sh

# 2) Deine RadioID eintragen (2 Zeilen) in der frisch kopierten Vorlage:
sudo nano /etc/svxlink/svxlink.d/ModuleTetraBrew.conf
#    -> USER=<deine RadioID>   und   SRC_ISSI=<deine RadioID>

# 3) Modul in der Logic-Sektion aktivieren + neu starten:
sudo nano /etc/svxlink/svxlink.conf      # in der Zeile: MODULES=...,ModuleTetraBrew
sudo systemctl restart svxlink
```

**Fertig.** Auftasten → `5#` → du bist im FreeTetra-Netz (Talkgroup 1). Andere TG: `<tg>#`, aus: `#`.

> **Du brauchst nur:** einen laufenden SvxLink-Repeater, deine **RadioID** (kostenlos auf
> radioid.net) und die üblichen Bau-Werkzeuge (`g++`, `python3` — meist schon da). Mehr nicht.
> Die ~35 Dateien im Repo sind fast alle Sprachansagen (`sounds/*.wav`) und Header — **die fasst
> du nie an**, das erledigt der Installer.

Mehr willst du fürs Mitmachen nicht wissen. Alles darunter ist Nachschlagewerk (Optionen,
mehrere Netze, Technik).

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

## Die FreeTetra-Config im Detail

`install.sh` kopiert dir diese Vorlage schon nach `svxlink.d/ModuleTetraBrew.conf` — du trägst nur
deine RadioID ein. So sieht sie aus, mit Erklärungen:

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

## Was `install.sh` automatisch macht

Damit du weißt, was da passiert (musst du nicht selbst tun):

- baut den ACELP-Codec (`libtetra-codec`) und das Modul **gegen dein installiertes SvxLink**
  (⚠️ das Modul ist versions-gebunden — es wird lokal kompiliert, damit es exakt passt),
- installiert `.so` / `.conf` / `.tcl` / Sounds an die richtigen Stellen,
- richtet den TLS-Proxy als systemd-Service ein (für `wss://freetetra.de`),
- kopiert die Config-Vorlage und sagt dir am Ende die zwei/drei Handgriffe, die noch fehlen.

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
