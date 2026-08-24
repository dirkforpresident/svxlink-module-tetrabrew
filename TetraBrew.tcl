###############################################################################
# TetraBrew module event handlers — deutsche Sprachansagen fürs Multi-Brew-Modul.
# (SvxLink v19.09 / 1.7.0 Idiom — Namespace MUSS zu NAME in [ModuleTetraBrew] passen.)
# Sounds liegen in sounds/<lang>/TetraBrew/ (dt. WAVs, in beide Sprachordner kopiert).
###############################################################################

namespace eval TetraBrew {

if {![info exists CFG_ID]} {
  return;
}

set module_name [namespace tail [namespace current]];

proc playMsg {msg} {
  variable module_name;
  ::playMsg $module_name $msg;
}

proc printInfo {msg} {
  variable module_name;
  puts "$module_name: $msg";
}

# Talkgroup Ziffer für Ziffer ansagen (funktioniert für jede TG)
proc spellGssi {gssi} {
  foreach d [split $gssi ""] {
    if {[string is digit -strict $d]} {
      playMsg $d;
      playSilence 60;
    }
  }
}

# Netz-Name ansagen (net_<Name>.wav). Fehlt der Clip -> nur Log-Warnung, Ansage läuft weiter.
proc playNet {net} {
  playSilence 100;
  playMsg "net_$net";
}

# --- Standard-Modul-Hooks (still halten, keine modul-eigenen name-Sounds) ---
proc activating_module {} {
  printInfo "aktiviert";
}
proc deactivating_module {} {
  printInfo "deaktiviert";
}
proc timeout {} {
  printInfo "Timeout — Brücke wird getrennt";
  playMsg "disconnected";
}
proc play_help {} {
  printInfo "Hilfe: DTMF <gssi># waehlt Talkgroup, # beendet";
}

# --- Eigene Events (aus dem C++-Modul via processEvent) ---

# Modul aktiviert, aber (noch) keine Default-TG -> nur Aktiv-Ansage
proc tetra_activated {} {
  printInfo "aktiviert, warte auf TG-Wahl";
  playMsg "activated";
}

# Link-Status global merken -> der Ident-Wrapper (events.d/local/) sagt ihn
# bei jeder CW-Kennung mit an.
proc setLinkStatus {net gssi} {
  set ::TB_active 1; set ::TB_net $net; set ::TB_tg $gssi;
}

# BREW verbunden: "brew_connected <netz> <gssi>"
proc brew_connected {net gssi} {
  printInfo "verbunden mit $net, TG $gssi";
  setLinkStatus $net $gssi;
  playMsg "connected";
  playNet $net;
  playSilence 150;
  playMsg "linked";
  playSilence 100;
  spellGssi $gssi;
}

# TG innerhalb desselben Netzes gewechselt: "linked_to_tg <netz> <gssi>"
proc linked_to_tg {net gssi} {
  printInfo "eingebucht $net TG $gssi";
  setLinkStatus $net $gssi;
  playMsg "linked";
  playNet $net;
  playSilence 100;
  spellGssi $gssi;
}

# BREW getrennt
proc brew_disconnected {} {
  printInfo "getrennt";
  set ::TB_active 0;
  playMsg "disconnected";
}

# TG nicht erlaubt / kein Endpunkt
proc tg_not_allowed {} {
  printInfo "TG nicht verfuegbar";
  playMsg "not_allowed";
}

# TETRA-Sprecher aktiv (nur Log, keine Ansage)
proc tetra_talker {issi} {
  printInfo "TETRA-Sprecher ISSI $issi";
}

# end of namespace
}

#
# This file has not been truncated
#
