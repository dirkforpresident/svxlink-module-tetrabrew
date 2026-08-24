#!/bin/bash
###############################################################################
# ModuleTetraBrew — Installer (baut auf dem Ziel gegen dein installiertes SvxLink)
#
#   sudo ./install.sh
#
# Baut libtetra-codec + das Modul passend zu deiner SvxLink-Version, installiert
# .so / .conf / .tcl / Sounds, richtet den TLS-Proxy (systemd) + den CW-Kennungs-
# Hook ein und sagt dir am Ende, was noch in die svxlink.conf muss.
###############################################################################
set -e
say()  { printf "\033[1;36m==>\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m!!\033[0m %s\n" "$*"; }
die()  { printf "\033[1;31mFEHLER:\033[0m %s\n" "$*" >&2; exit 1; }

[ "$(id -u)" = "0" ] || die "Bitte mit sudo/als root laufen lassen."
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# --- 1. Werkzeuge prüfen ---
say "Prüfe Voraussetzungen..."
for t in g++ gcc pkg-config python3; do command -v "$t" >/dev/null || die "'$t' fehlt (bitte installieren)."; done
[ -d /usr/include/svxlink ] || die "SvxLink-Header /usr/include/svxlink nicht gefunden — läuft hier SvxLink?"
pkg-config --exists sigc++-2.0 || die "sigc++-2.0 fehlt."

# --- 2. SvxLink-Version + Pfade ermitteln ---
CONF="${SVXCONF:-/etc/svxlink/svxlink.conf}"
[ -f "$CONF" ] || die "svxlink.conf nicht gefunden ($CONF). Setze SVXCONF=<pfad> und erneut."
MODULE_PATH="$(grep -E '^MODULE_PATH=' "$CONF" | head -1 | cut -d= -f2)"
CFG_DIR_REL="$(grep -E '^CFG_DIR=' "$CONF" | head -1 | cut -d= -f2)"
: "${MODULE_PATH:=/usr/lib/svxlink}"
CFG_DIR="/etc/svxlink/${CFG_DIR_REL:-svxlink.d}"
SOUND_BASE="/usr/share/svxlink/sounds"
EVENTS_LOCAL="/usr/share/svxlink/events.d/local"

# Version aus laufendem Log oder --version ziehen; sonst fragen.
VER="$(svxlink --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
[ -z "$VER" ] && VER="$(grep -hoE 'SvxLink v[0-9]+\.[0-9]+\.[0-9]+' /var/log/svxlink* 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | tail -1 || true)"
if [ -z "$VER" ]; then
  read -rp "SvxLink-Version konnte nicht erkannt werden. Bitte eingeben (z.B. 1.7.0): " VER
fi
[ -n "$VER" ] || die "Keine SvxLink-Version."
say "SvxLink-Version: $VER · MODULE_PATH: $MODULE_PATH · CFG_DIR: $CFG_DIR"

# --- 3. libtetra-codec bauen ---
# ACELP-Codec = ETSI-TETRA-Referenz (EN 300 395-2). Wird vom Upstream geholt
# (NICHT in diesem Repo mitgeliefert — ETSI-Lizenz). Override: TETRA_CODEC_DIR=<pfad>.
CODEC_DIR="${TETRA_CODEC_DIR:-tetra-codec}"
if [ ! -d "$CODEC_DIR" ]; then
  command -v git >/dev/null || die "git fehlt (für den Codec-Download)."
  say "Hole ACELP-Codec (github.com/outerplane/tetra-codec)..."
  git clone --depth 1 https://github.com/outerplane/tetra-codec.git "$CODEC_DIR" \
    || die "Codec-Clone fehlgeschlagen. Manuell holen + TETRA_CODEC_DIR=<pfad> setzen."
fi
say "Baue libtetra-codec..."
gcc -shared -fPIC -O2 -std=c11 -I"$CODEC_DIR/include" -I"$CODEC_DIR/source" \
    -o /usr/local/lib/libtetra-codec.so \
    "$CODEC_DIR/source/tetra-codec.c" "$CODEC_DIR/source/tetra-codec-impl.c"
install -m644 "$CODEC_DIR/include/tetra-codec.h" /usr/local/include/
ldconfig
say "libtetra-codec installiert."

# --- 4. Versions-Header erzeugen + Modul bauen ---
say "Baue ModuleTetraBrew gegen SvxLink $VER..."
mkdir -p svxheaders/version
printf '#ifndef SVXLINK_VERSION_INCLUDED\n#define SVXLINK_VERSION_INCLUDED\n#define SVXLINK_APP_VERSION "%s"\n#endif\n' "$VER" > svxheaders/version/SVXLINK.h
[ -f svxheaders/version/MODULE_TETRA_BREW.h ] || \
  printf '#define MODULE_TETRA_BREW_VERSION "0.2.0"\n' > svxheaders/version/MODULE_TETRA_BREW.h
SR="$(grep -hoE 'INTERNAL_SAMPLE_RATE[= ]+[0-9]+' /usr/include/svxlink/*.h 2>/dev/null | grep -oE '[0-9]+' | head -1)"
: "${SR:=16000}"
g++ -shared -fPIC -std=c++11 -O2 -o ModuleTetraBrew.so \
    ModuleTetraBrew.cpp TetraBrewConnection.cpp \
    -I/usr/include/svxlink -Isvxheaders -I/usr/local/include \
    -DINTERNAL_SAMPLE_RATE="${SR}" $(pkg-config --cflags sigc++-2.0) \
    -L/usr/local/lib -ltetra-codec
file ModuleTetraBrew.so | grep -q ELF || die "Build fehlgeschlagen."
install -m644 ModuleTetraBrew.so "$MODULE_PATH/"
say "Modul installiert -> $MODULE_PATH/ModuleTetraBrew.so"

# --- 5. TCL, Sounds, Kennungs-Hook ---
install -m644 TetraBrew.tcl /usr/share/svxlink/events.d/
mkdir -p "$EVENTS_LOCAL" && install -m644 events.d.local/zz_tetrabrew_ident.tcl "$EVENTS_LOCAL/"
for L in en_US de_DE; do
  mkdir -p "$SOUND_BASE/$L/TetraBrew"
  install -m644 sounds/TetraBrew/*.wav "$SOUND_BASE/$L/TetraBrew/" 2>/dev/null || true
done
say "TCL + Sounds + Kennungs-Hook installiert."

# --- 6. TLS-Proxy (systemd) — nur nötig für TLS-Server (z.B. freetetra.de) ---
install -m755 tools/tetrabrew-tls-proxy.py /opt/tetrabrew-tls-proxy.py 2>/dev/null || \
  warn "tools/tetrabrew-tls-proxy.py fehlt — TLS-Proxy nicht installiert (nur für ws:// nötig)."
if [ -f /opt/tetrabrew-tls-proxy.py ]; then
  cat > /etc/systemd/system/tetrabrew-tls-freetetra.service <<UNIT
[Unit]
Description=ModuleTetraBrew TLS-Proxy (FreeTetra)
After=network-online.target
[Service]
Environment=TLSP_LPORT=18443 TLSP_RHOST=freetetra.de TLSP_RPORT=443
ExecStart=/usr/bin/python3 /opt/tetrabrew-tls-proxy.py
Restart=always
[Install]
WantedBy=multi-user.target
UNIT
  systemctl daemon-reload
  systemctl enable --now tetrabrew-tls-freetetra
  say "TLS-Proxy 127.0.0.1:18443 -> freetetra.de:443 läuft."
fi

# --- 7. Config-Vorlage ---
DEST_CONF="$CFG_DIR/ModuleTetraBrew.conf"
if [ ! -f "$DEST_CONF" ]; then
  install -m644 examples/freetetra-only.conf "$DEST_CONF"
  warn "Config-Vorlage kopiert -> $DEST_CONF  (USER=<deine RadioID> eintragen!)"
else
  say "Config existiert schon ($DEST_CONF) — bleibt unangetastet."
fi

cat <<DONE

============================================================
  FERTIG. Noch zwei Handgriffe:

  1) In $DEST_CONF  ->  USER + SRC_ISSI = deine RadioID.
  2) In $CONF, Logic-Sektion:  MODULES=...,ModuleTetraBrew
  3) sudo systemctl restart svxlink

  Dann: auftasten, 5# = an (FreeTetra TG 1). 73!
  Beispiel-Configs siehe examples/.
============================================================
DONE
