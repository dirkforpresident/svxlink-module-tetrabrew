#!/usr/bin/env bash
###############################################################################
# ModuleTetraBrew — sicherer Installer
#
#   sudo ./install.sh [BEFEHL] [OPTIONEN]
#
# Befehle:  (ohne)       installieren  (Pre-Flight -> bauen -> testen -> Auto-Rollback)
#           --check      nur pruefen + probeweise bauen; aendert NICHTS am System
#           --status     Zustand anzeigen (installiert? geladen? Version?)
#           --uninstall  sauber entfernen (Modul raus, Original wiederhergestellt)
#
# Optionen: --call RUFZEICHEN  dein Rufzeichen -> feste ISSI + Login (sonst Abfrage)
#           --issi N       optionaler ISSI-Override (eigene DMR-RadioID)
#           --radioid N    Alias fuer --issi (RadioID als ISSI-Override)
#           --ident-hook  optionalen CW-Status-Hook mitinstallieren (Default: aus)
#           --yes|-y      keine Rueckfragen (Automatik-Modus)
#
# Prinzip: kompiliert AUF dem Ziel gegen dessen SvxLink -> version-universal +
# ABI-sicher. Transaktional mit Manifest + Auto-Rollback + Test-Start. Fasst nur
# das Modul an (Rx/Tx/Audio/Logic/Kennung bleiben unberuehrt). Kann das laufende
# Relais nicht dauerhaft stoeren: bei jedem Fehler -> exakt vorheriger Zustand.
###############################################################################
set -euo pipefail

C_G=$'\033[1;36m'; C_Y=$'\033[1;33m'; C_R=$'\033[1;31m'; C_N=$'\033[0m'
say(){  printf "%s==>%s %s\n" "$C_G" "$C_N" "$*"; }
ok(){   printf "   %s[ok]%s %s\n" "$C_G" "$C_N" "$*"; }
warn(){ printf "%s !!%s %s\n" "$C_Y" "$C_N" "$*"; }
err(){  printf "%sFEHLER:%s %s\n" "$C_R" "$C_N" "$*" >&2; }
die(){  err "$*"; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
CONF="${SVXCONF:-/etc/svxlink/svxlink.conf}"
STATE_DIR=/var/lib/tetrabrew
MANIFEST="$STATE_DIR/manifest"
CONFBAK="$STATE_DIR/svxlink.conf.bak"

# ---------- Argumente ----------
MODE=install; CALL=""; ISSI=""; IDENT_HOOK=0; ASSUME_YES=0
while [ $# -gt 0 ]; do case "$1" in
  --check) MODE=check ;;  --status) MODE=status ;;  --uninstall) MODE=uninstall ;;
  --call) CALL="${2:-}"; shift ;;
  --issi) ISSI="${2:-}"; shift ;;
  --radioid) ISSI="${2:-}"; shift ;;   # Alias: RadioID als ISSI-Override
  --ident-hook) IDENT_HOOK=1 ;;
  --yes|-y) ASSUME_YES=1 ;;
  -h|--help) sed -n '4,20p' "$0"; exit 0 ;;
  *) die "Unbekannte Option: $1 (--help fuer Hilfe)";;
esac; shift; done

# ---------- Helfer ----------
detect_paths(){
  MODULE_PATH="$(grep -E '^MODULE_PATH=' "$CONF" 2>/dev/null | head -1 | cut -d= -f2 || true)"
  local rel;  rel="$(grep -E '^CFG_DIR=' "$CONF" 2>/dev/null | head -1 | cut -d= -f2 || true)"
  : "${MODULE_PATH:=/usr/lib/svxlink}"
  CFG_DIR="/etc/svxlink/${rel:-svxlink.d}"
  SO_DEST="$MODULE_PATH/ModuleTetraBrew.so"
  CONF_DEST="$CFG_DIR/ModuleTetraBrew.conf"
  TCL_DEST="/usr/share/svxlink/events.d/TetraBrew.tcl"
}
detect_version(){
  local v; v="$(svxlink --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
  [ -z "$v" ] && v="$(grep -rhoE 'SvxLink v?[0-9]+\.[0-9]+\.[0-9]+' /var/log/svxlink* 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | tail -1 || true)"
  printf '%s' "$v"
}
svx_active(){ systemctl is-active --quiet svxlink 2>/dev/null; }
svx_log(){ { journalctl -u svxlink --no-pager -n 80 2>/dev/null || true; tail -80 /var/log/svxlink 2>/dev/null || true; } | tail -160 || true; }
pkg_hint(){ if command -v apt-get >/dev/null; then echo "sudo apt-get install $*"
  elif command -v dnf >/dev/null; then echo "sudo dnf install $*"
  elif command -v pacman >/dev/null; then echo "sudo pacman -S $*"; else echo "installiere: $*"; fi; }

# ---------- Pre-Flight (aendert NICHTS) ----------
preflight(){
  local fail=0
  say "Pre-Flight-Checks (es wird noch NICHTS geaendert)"
  command -v svxlink >/dev/null && ok "svxlink gefunden" || { err "svxlink nicht gefunden — laeuft hier ein SvxLink-Relais?"; fail=1; }
  VER="$(detect_version)"
  [ -n "$VER" ] && ok "SvxLink-Version: $VER" || warn "Version nicht auto-erkennbar (wird beim Bauen abgefragt)"
  [ -f "$CONF" ] && ok "Config: $CONF" || { err "svxlink.conf fehlt ($CONF) — SVXCONF=<pfad> setzen"; fail=1; }
  grep -qE '^MODULES=' "$CONF" 2>/dev/null && ok "MODULES-Zeile vorhanden" || { err "Keine MODULES= Zeile in $CONF"; fail=1; }
  [ -d /usr/include/svxlink ] && ok "SvxLink-Header /usr/include/svxlink" || { err "SvxLink-Header fehlen  ->  $(pkg_hint svxlink-dev)"; fail=1; }
  local miss=()
  for t in g++ gcc pkg-config git; do command -v "$t" >/dev/null || miss+=("$t"); done
  pkg-config --exists sigc++-2.0 2>/dev/null || miss+=("libsigc++-2.0-dev")
  [ ${#miss[@]} -eq 0 ] && ok "Build-Werkzeuge komplett" || { err "Fehlt: ${miss[*]}  ->  $(pkg_hint build-essential ${miss[*]})"; fail=1; }
  [ -d "$MODULE_PATH" ] && [ -w "$MODULE_PATH" ] && ok "MODULE_PATH schreibbar: $MODULE_PATH" || { err "MODULE_PATH nicht schreibbar: $MODULE_PATH"; fail=1; }
  systemctl cat svxlink >/dev/null 2>&1 && ok "systemd-Unit svxlink" || warn "Kein systemd-svxlink — Auto-Test/Rollback eingeschraenkt"
  svx_active && ok "svxlink laeuft (gesunder Ausgangspunkt)" || warn "svxlink laeuft gerade NICHT — bring erst dein Relais zum Laufen (sonst kann der Test-Start nicht sauber pruefen)"
  return $fail
}

# ---------- Bauen (in HERE; installiert noch nicht) ----------
build(){
  [ -n "${VER:-}" ] || VER="$(detect_version)"
  if [ -z "$VER" ]; then [ "$ASSUME_YES" = 1 ] && die "SvxLink-Version unbekannt (bei --yes bitte Version vorab sicherstellen)"; read -rp "SvxLink-Version (z.B. 1.10.1): " VER; fi
  [ -n "$VER" ] || die "Keine SvxLink-Version."
  say "Baue ACELP-Codec + Modul gegen SvxLink $VER"
  local CD="${TETRA_CODEC_DIR:-$HERE/tetra-codec}"
  [ -d "$CD" ] || git clone --depth 1 https://github.com/outerplane/tetra-codec.git "$CD" >/dev/null 2>&1 || die "ACELP-Codec-Download fehlgeschlagen (manuell holen + TETRA_CODEC_DIR=<pfad>)"
  gcc -shared -fPIC -O2 -std=c11 -I"$CD/include" -I"$CD/source" -o "$HERE/libtetra-codec.so" \
      "$CD/source/tetra-codec.c" "$CD/source/tetra-codec-impl.c" || die "Codec-Build fehlgeschlagen"
  ok "libtetra-codec gebaut"
  mkdir -p "$HERE/svxheaders/version"
  printf '#ifndef SVXLINK_VERSION_INCLUDED\n#define SVXLINK_VERSION_INCLUDED\n#define SVXLINK_APP_VERSION "%s"\n#endif\n' "$VER" > "$HERE/svxheaders/version/SVXLINK.h"
  [ -f "$HERE/svxheaders/version/MODULE_TETRA_BREW.h" ] || printf '#define MODULE_TETRA_BREW_VERSION "0.2.0"\n' > "$HERE/svxheaders/version/MODULE_TETRA_BREW.h"
  local SR; SR="$(grep -rhoE 'INTERNAL_SAMPLE_RATE[= ]+[0-9]+' /usr/include/svxlink/*.h 2>/dev/null | grep -oE '[0-9]+' | head -1 || true)"; : "${SR:=16000}"
  g++ -shared -fPIC -std=c++11 -O2 -o "$HERE/ModuleTetraBrew.so" \
      "$HERE/ModuleTetraBrew.cpp" "$HERE/TetraBrewConnection.cpp" \
      -I/usr/include/svxlink -I"$HERE/svxheaders" -I/usr/local/include \
      -DINTERNAL_SAMPLE_RATE="$SR" $(pkg-config --cflags sigc++-2.0) \
      -L"$HERE" -L/usr/local/lib -ltetra-codec || die "Modul-Build fehlgeschlagen"
  file "$HERE/ModuleTetraBrew.so" | grep -q ELF || die "Modul-Build ergab kein ELF-Objekt"
  ok "ModuleTetraBrew.so gebaut (INTERNAL_SAMPLE_RATE=$SR)"
}

# ---------- Rollback (via trap waehrend der Transaktion) ----------
COMMITTED=0
BACKUP_DIR="$STATE_DIR/backup"
record(){ printf '%s\n' "$1" >> "$MANIFEST"; }
# Datei platzieren: existierendes Ziel VORHER sichern (fuer sauberen Rollback), dann installieren.
place(){ local src="$1" dest="$2" mode="${3:-644}"
  if [ -e "$dest" ]; then install -d "$BACKUP_DIR$(dirname "$dest")"; cp -a "$dest" "$BACKUP_DIR$dest"; fi
  install -Dm"$mode" "$src" "$dest"; record "$dest"; }
rollback(){
  [ "$COMMITTED" = 1 ] && return 0
  warn "Rollback — stelle Originalzustand wieder her ..."
  if [ -f "$MANIFEST" ]; then while IFS= read -r f; do [ -z "$f" ] && continue
      if [ -e "$BACKUP_DIR$f" ]; then cp -a "$BACKUP_DIR$f" "$f"; else rm -f "$f"; fi
    done < "$MANIFEST"; fi
  [ -f "$CONFBAK" ] && cp -f "$CONFBAK" "$CONF"
  rm -rf "$MANIFEST" "$BACKUP_DIR"
  systemctl restart svxlink 2>/dev/null || true; sleep 3
  svx_active && warn "Rollback fertig — Relais laeuft wieder wie vorher." \
             || err "Rollback: svxlink kam nicht hoch! Backup liegt in $CONFBAK — bitte pruefen."
}

# ---------- Installieren ----------
do_install(){
  detect_paths
  preflight || die "Pre-Flight fehlgeschlagen — NICHTS geaendert. Fix die Punkte oben, dann erneut."
  build
  if [ -z "$CALL" ]; then [ "$ASSUME_YES" = 1 ] && die "Rufzeichen fehlt (--call RUFZEICHEN)"; read -rp "Dein Rufzeichen (z.B. DO0RAM): " CALL; fi
  CALL=$(printf '%s' "$CALL" | tr 'a-z' 'A-Z')
  [[ "$CALL" =~ ^[A-Z0-9]{3,10}$ ]] || die "Rufzeichen ungueltig (Buchstaben/Ziffern, 3-10 Zeichen): $CALL"
  if [ -n "$ISSI" ]; then [[ "$ISSI" =~ ^[0-9]+$ ]] || die "ISSI/RadioID muss numerisch sein: $ISSI"; fi
  # ISSI zur Info aus dem Call ableiten (falls kein Override und Helfer vorhanden)
  DERIVED=""
  if [ -z "$ISSI" ] && command -v python3 >/dev/null 2>&1 && [ -f "$HERE/tools/call2issi.py" ]; then
    DERIVED=$(python3 "$HERE/tools/call2issi.py" "$CALL" 2>/dev/null | awk '{print $3}')
  fi

  # ---- Vorschau + Bestaetigung (VOR jeder System-Aenderung) ----
  echo
  say "Vorschau — das wird gemacht (bis hierher NICHTS am System geaendert):"
  echo "   SvxLink $VER   MODULE_PATH $MODULE_PATH   CFG_DIR $CFG_DIR"
  echo "   + Modul   -> $SO_DEST"
  echo "   + Codec   -> /usr/local/lib/libtetra-codec.so"
  echo "   + TCL/Sounds -> /usr/share/svxlink/events.d/ + sounds/"
  if [ -n "$ISSI" ]; then CFGINFO="CALL $CALL, ISSI-Override $ISSI"
  elif [ -n "$DERIVED" ]; then CFGINFO="CALL $CALL -> ISSI $DERIVED"
  else CFGINFO="CALL $CALL"; fi
  [ -f "$CONF_DEST" ] && echo "   . Config  -> $CONF_DEST (existiert -> bleibt unangetastet)" \
                      || echo "   + Config  -> $CONF_DEST ($CFGINFO)"
  grep -qE '^MODULES=.*\bModuleTetraBrew\b' "$CONF" && echo "   . MODULES -> ModuleTetraBrew schon eingetragen" \
                      || echo "   ~ MODULES -> ModuleTetraBrew wird angehaengt (Backup: $CONFBAK)"
  [ "$IDENT_HOOK" = 1 ] && echo "   + optionaler CW-Kennungs-Hook"
  echo "   danach: svxlink Test-Neustart + Verifikation. Bei JEDEM Fehler -> automatischer Rollback."
  echo
  warn "Einbau erfolgt auf EIGENE VERANTWORTUNG. Der Installer sichert alles (Backup:"
  warn "$CONFBAK) und rollt bei Fehlern automatisch zurueck — eine 100%-Garantie gibt es"
  warn "aber nie. Lieber selbst? -> Modul bauen + MODULES=...,ModuleTetraBrew von Hand"
  warn "(siehe BEDIENUNG.md und examples/). Rueckgaengig jederzeit: --uninstall."
  echo
  if [ "$ASSUME_YES" != 1 ]; then
    read -rp "Verstanden, auf eigene Verantwortung fortfahren? [j/N] " a
    case "$a" in j|J|y|Y) ;; *) die "Abgebrochen — es wurde NICHTS geaendert.";; esac
  fi

  mkdir -p "$STATE_DIR"; : > "$MANIFEST"; cp -f "$CONF" "$CONFBAK"
  trap rollback ERR INT TERM     # ab hier: jeder Fehler -> alles zurueck

  say "Installiere Dateien ..."
  place "$HERE/libtetra-codec.so" /usr/local/lib/libtetra-codec.so
  [ -f "$HERE/tetra-codec/include/tetra-codec.h" ] && place "$HERE/tetra-codec/include/tetra-codec.h" /usr/local/include/tetra-codec.h
  ldconfig || true
  place "$HERE/ModuleTetraBrew.so" "$SO_DEST"
  place "$HERE/TetraBrew.tcl" "$TCL_DEST"
  for L in en_US de_DE; do for w in "$HERE"/sounds/TetraBrew/*.wav; do [ -e "$w" ] && place "$w" "/usr/share/svxlink/sounds/$L/TetraBrew/$(basename "$w")"; done; done
  if [ "$IDENT_HOOK" = 1 ]; then place "$HERE/events.d.local/zz_tetrabrew_ident.tcl" /usr/share/svxlink/events.d/local/zz_tetrabrew_ident.tcl; fi

  # TLS-Proxy: die Default-Config zeigt auf 127.0.0.1:18443 -> freetetra.de:443 (wss).
  # Nur einrichten, wenn nicht schon vorhanden und das Tool im Repo liegt.
  if [ -f "$HERE/tools/tetrabrew-tls-proxy.py" ] && [ ! -f /etc/systemd/system/tetrabrew-tls-freetetra.service ]; then
    place "$HERE/tools/tetrabrew-tls-proxy.py" /opt/tetrabrew-tls-proxy.py 755
    cat > /etc/systemd/system/tetrabrew-tls-freetetra.service <<'UNIT'
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
    record /etc/systemd/system/tetrabrew-tls-freetetra.service
    systemctl daemon-reload; systemctl enable --now tetrabrew-tls-freetetra 2>/dev/null || true
    ok "TLS-Proxy 127.0.0.1:18443 -> freetetra.de:443 eingerichtet"
  fi

  if [ ! -f "$CONF_DEST" ]; then
    install -d "$CFG_DIR"
    sed_args=(-e "s/^CALL=.*/CALL=$CALL/")
    [ -n "$ISSI" ] && sed_args+=(-e "s/^;*SRC_ISSI=.*/SRC_ISSI=$ISSI/")
    sed "${sed_args[@]}" "$HERE/examples/freetetra-only.conf" > "$CONF_DEST"
    chmod 640 "$CONF_DEST"; record "$CONF_DEST"; ok "Config angelegt: $CONF_DEST ($CFGINFO)"
  else warn "Config existiert schon -> unangetastet ($CONF_DEST)"; fi

  if grep -qE '^MODULES=.*\bModuleTetraBrew\b' "$CONF"; then ok "MODULES enthaelt ModuleTetraBrew bereits";
  else sed -i -E 's/^(MODULES=[^#[:space:]]*)/\1,ModuleTetraBrew/' "$CONF"; ok "ModuleTetraBrew zu MODULES hinzugefuegt"; fi

  say "Test: svxlink neu starten und pruefen ..."
  systemctl restart svxlink 2>/dev/null || { rollback; die "svxlink-Neustart ging nicht — zurueckgerollt."; }
  sleep 5
  svx_active || { err "svxlink kam mit dem Modul NICHT hoch."; rollback; die "Abgebrochen — Relais laeuft wieder wie vorher."; }
  if svx_log | grep -qiE 'Bailing out|Initialization failed for module ModuleTetraBrew'; then
    err "Modul lud nicht sauber (siehe Log)."; rollback; die "Abgebrochen — Relais wiederhergestellt."; fi
  svx_log | grep -qi 'Module TetraBrew' && ok "Modul geladen" || warn "Modul-Start nicht im Log gefunden (evtl. anderes Logziel) — svxlink laeuft aber."

  COMMITTED=1; trap - ERR INT TERM
  echo; say "FERTIG. ModuleTetraBrew installiert und getestet."
  echo "   Aktivieren:  auftasten + DTMF 5#     Status:  sudo $0 --status     Entfernen:  sudo $0 --uninstall"
}

# ---------- Deinstallieren ----------
do_uninstall(){
  detect_paths
  say "Entferne ModuleTetraBrew ..."
  # ModuleTetraBrew aus MODULES loesen (in beliebiger Position)
  if grep -qE '^MODULES=.*ModuleTetraBrew' "$CONF" 2>/dev/null; then
    cp -f "$CONF" "$STATE_DIR/svxlink.conf.preuninstall.$(date +%s 2>/dev/null || echo bak)" 2>/dev/null || true
    sed -i -E 's/,?ModuleTetraBrew//g' "$CONF"; ok "aus MODULES entfernt"
  fi
  # nur Dienste stoppen, die WIR laut Manifest angelegt haben
  if [ -f "$MANIFEST" ] && grep -q 'tetrabrew-tls-freetetra.service' "$MANIFEST"; then
    systemctl disable --now tetrabrew-tls-freetetra 2>/dev/null || true; fi
  # installierte Dateien laut Manifest entfernen
  if [ -f "$MANIFEST" ]; then while IFS= read -r f; do [ -n "$f" ] && rm -f "$f" && echo "   entfernt: $f"; done < "$MANIFEST"; rm -rf "$MANIFEST" "$BACKUP_DIR";
  else
    warn "kein Manifest — entferne Standardpfade"; rm -f "$SO_DEST" "$TCL_DEST"; fi
  systemctl daemon-reload 2>/dev/null || true
  say "Neustart + Pruefung ..."
  systemctl restart svxlink 2>/dev/null || true; sleep 4
  svx_active && ok "svxlink laeuft (ohne das Modul)." || err "svxlink kam nicht hoch — pruefe $CONF."
  say "Deinstallation fertig."
}

# ---------- Status / Doctor ----------
do_status(){
  detect_paths
  say "ModuleTetraBrew — Status"
  echo "   SvxLink-Version : $(detect_version || echo '?')"
  echo "   .so installiert : $([ -f "$SO_DEST" ] && echo "ja  ($SO_DEST)" || echo nein)"
  echo "   Config          : $([ -f "$CONF_DEST" ] && echo "ja  ($CONF_DEST)" || echo nein)"
  echo "   in MODULES       : $(grep -qE '^MODULES=.*ModuleTetraBrew' "$CONF" 2>/dev/null && echo ja || echo nein)"
  echo "   svxlink aktiv    : $(svx_active && echo ja || echo NEIN)"
  local loaded="nein" initfail=0
  if svx_log | grep -qiE 'Bailing out|Initialization failed for module ModuleTetraBrew'; then initfail=1; fi
  if [ "$initfail" = 0 ] && svx_active && grep -qE '^MODULES=.*ModuleTetraBrew' "$CONF" 2>/dev/null; then loaded="ja (aktiv)"; fi
  if svx_log | grep -qi 'Module TetraBrew'; then loaded="ja (im Log)"; fi
  echo "   Modul geladen    : $loaded"
  [ "$initfail" = 1 ] && warn "Log zeigt Modul-Init-Fehler!" || true
  [ -f "$MANIFEST" ] && echo "   Manifest         : $MANIFEST ($(wc -l < "$MANIFEST") Datei(en))"
}

# ---------- Dispatch ----------
[ "$(id -u)" = 0 ] || { [ "$MODE" = status ] || die "Bitte mit sudo/als root ausfuehren."; }
case "$MODE" in
  install)   do_install ;;
  check)     detect_paths; if preflight; then build; echo; say "CHECK OK — wuerde sauber installieren (nichts geaendert)."; else die "CHECK: Voraussetzungen fehlen (siehe oben)."; fi ;;
  status)    do_status ;;
  uninstall) do_uninstall ;;
esac
