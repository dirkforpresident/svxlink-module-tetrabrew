# ModuleTetraBrew

Bindet einen SvxLink-FM-Repeater über das BREW-Protokoll an ein TETRA-Netz an —
zum Beispiel an das offene [FreeTetra](https://freetetra.de)-Netz.

Ein natives SvxLink-Modul (wie ModuleEchoLink), das einen analogen FM-Repeater über
BREW (TETRA, ACELP-Codec) mit einem oder mehreren TETRA-Netzen verbindet: Talkgroup-Wahl
per DTMF, deutsche Sprachansagen, läuft auf alten wie neuen Repeatern.

---

## Am Netz — in einem Schritt

`install.sh` macht **alles**: baut Codec + Modul **gegen dein SvxLink** (alt wie neu), fragt deine
RadioID, trägt das Modul ein, testet den Neustart — und **rollt bei jedem Fehler automatisch
zurück**. Dein laufendes Relais kann dabei nicht kaputtgehen.

```bash
git clone https://github.com/do1xx/svxlink-module-tetrabrew.git
cd svxlink-module-tetrabrew

# Erst nur prüfen (ändert NICHTS am System):
sudo ./install.sh --check

# Installieren — fragt RadioID, zeigt eine Vorschau, du bestätigst:
sudo ./install.sh --radioid <DEINE-RADIOID>
```

**Fertig.** Auftasten → `5#` → du bist im FreeTetra-Netz (Talkgroup 1). Andere TG: `<tg>#`, aus: `#`.

Status prüfen: `sudo ./install.sh --status` · Entfernen: `sudo ./install.sh --uninstall` ·
Details: **[INSTALL.md](INSTALL.md)**

> **Du brauchst nur:** einen laufenden SvxLink-Repeater, deine **RadioID** (kostenlos auf
> radioid.net) und die üblichen Bau-Werkzeuge (`g++`, `python3` — meist schon da). Der Installer
> fasst nur das Modul an (Rx/Tx, Audio, Logic, Kennung bleiben unberührt) und sichert vorher alles.

Alles Weitere unten ist Nachschlagewerk: Optionen, mehrere Netze, Technik.

---

## Die Idee: FreeTetra

[**freetetra.de**](https://freetetra.de) ist ein **freier, offener BREW-Server**, der einzelne
**TETRA-BlueStations** und **FM-Repeater** miteinander vernetzt. Wer eine **RadioID** hat, kann
connecten — kein Account, keine Anmeldung. So entsteht ein gemeinsames Amateurfunk-TETRA-Netz,
in dem sich FM- und TETRA-Leute treffen.

- **FM-Repeater** kommen über dieses Modul rein.
- **TETRA-BlueStations** sprechen BREW von Haus aus.

FM und TETRA in einem Netz: bei wenig Betrieb bringt das die Modi und die Leute zusammen,
statt dass jeder ein eigenes Gerät für jedes Netz braucht.

### TETRA-Kern & FM-Rand — wie beide Seiten zusammenpassen

Wichtig zu verstehen, weil die zwei Seiten **unterschiedlich** angebunden sind:

- **TETRA-Seite: sinnvollerweise dauerhaft verbunden.** Eine BlueStation läuft zwar auch
  lokal für sich — aber es lohnt sich, **alle Zellen einer Region** permanent am BREW-Server
  zu haben, damit die Region zusammenwächst und man sich jederzeit erreicht. Deshalb sind
  BlueStations üblicherweise dauernd online.
- **FM-Seite: freie Wahl.** Ein FM-Repeater funktioniert komplett ohne Netz; die Brücke ist
  Zusatz. Standardmäßig klinkt sich das Modul **nur bei Bedarf per `5#`** ein (wie
  ModuleEchoLink) und gibt danach wieder frei — **oder** du fährst es dauerhaft verbunden.

Bei on-demand gilt: solange die FM-Seite *schläft*, ist sie von TETRA aus nicht erreichbar —
Cross-Mode läuft, sobald ein FM-OM aktiviert (dann in **beide** Richtungen). Wer stattdessen
einen **dauerhaft vernetzten** FM-Einstieg will (wie eine BlueStation), setzt `AUTO_CONNECT=1` +
`TIMEOUT=0` — dann ist die FM-Seite auch immer erreichbar. **Du entscheidest pro Repeater.**

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

- **Pre-Flight-Checks** vor jeder Änderung (SvxLink, Version, Header, Build-Tools, Rechte) —
  fehlt was, gibt's eine klare Meldung + Fix, und **nichts wird angefasst**,
- baut den ACELP-Codec (`libtetra-codec`) und das Modul **gegen dein installiertes SvxLink**
  (versions-gebunden → lokal kompiliert, passt exakt — auch bei alten Versionen),
- zeigt eine **Vorschau + Haftungshinweis** und fragt dich um Bestätigung,
- installiert `.so` / `.conf` / `.tcl` / Sounds, füllt deine **RadioID** ein, richtet den
  TLS-Proxy ein und trägt **`MODULES=…,ModuleTetraBrew`** selbst ein (mit Backup),
- **testet** den Neustart und **rollt bei jedem Fehler automatisch zurück** → dein Relais läuft
  wieder wie vorher. Dazu `--check` (Dry-Run), `--status` (Doctor), `--uninstall`.

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
| `AUTO_CONNECT` | `1` = **Permanent-Knoten**: Modul aktiviert sich beim Start selbst (statt erst per `5#`). Für echten Dauerbetrieb zusätzlich `TIMEOUT=0`. Default `0` = on-demand |

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
(ein anderes TETRA-Netz, ein Gateway o.ä.). Wenn du in ein fremdes/belebtes Netz brückst:
`GSSI_ALLOW` + regionale/Test-TGs nutzen, dessen Regeln beachten — du bist verantwortlich.

---

## Wie's funktioniert

- Das Modul ist gleichzeitig `AudioSink` (FM-RX) und `AudioSource` (FM-TX) in der SvxLink-Pipeline.
- Der BREW-Client läuft auf `Async::TcpClient` (HTTP-Digest + WebSocket + BREW-Binärprotokoll),
  ACELP über `libtetra-codec`, Resampling 16↔8 kHz.
- **TLS:** altes SvxLink/Async kann kein TLS → ein winziger `python3`-TLS-Proxy terminiert TLS
  lokal (`install.sh` richtet ihn ein). Auf neuem SvxLink kommt später natives TLS.

---

## Brauche ich den TLS-Proxy?

Nur, wenn dein BREW-Server über **`wss://` (TLS)** läuft — so wie **FreeTetra** (hinter nginx auf
Port 443). SvxLink selbst kann kein TLS, deshalb übernimmt das der Proxy:

```
Modul --(plain ws://)--> 127.0.0.1:18443 --(TLS)--> freetetra.de:443
```

| Ziel-Server | Proxy nötig? |
|---|---|
| **`wss://` (TLS)** — z.B. FreeTetra | **Ja** (`install.sh` richtet ihn automatisch ein) |
| **`ws://` (plain)** — eigener Server im LAN / über Tailscale / VPN | **Nein** — dann direkt `HOST=<ip>` `PORT=<port>`, kein Proxy |

---

## Läuft das auf uraltem SvxLink?

**Ja — genau dafür ist es gebaut.** Das Modul nutzt nur die schon ewig stabilen Async-Bausteine
(`TcpClient`, Audio-Pipeline, Timer) und wird von `install.sh` **lokal gegen dein installiertes
SvxLink kompiliert** — dadurch passt es exakt zu deiner Version, egal ob alt oder neu. Genau
deshalb gibt es auch den TLS-Proxy (altes Async kann kein TLS). Getestet u.a. auf **SvxLink 1.7.0
(armhf)**. Voraussetzung ist praktisch nur ein `g++` mit C++11 (auf jedem Raspberry-Pi-SvxLink
dabei). Auf ganz neuem SvxLink läuft es identisch — dort wird irgendwann nur der Proxy überflüssig.

---

## Lizenz

GPL v2+ (wie SvxLink). Nutzung auf eigene Verantwortung — du bist als Repeater-Verantwortlicher
für alles zuständig, was über deine Brücke ausgesendet wird.
