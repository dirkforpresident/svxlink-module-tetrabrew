###############################################################################
# TetraBrew — Status-Ansage an die CW-Kennung koppeln.
# Nach /usr/share/svxlink/events.d/local/  kopieren (lädt zuletzt).
# Sagt bei jeder send_short_ident/send_long_ident zusätzlich an, womit das
# Relais gerade verbunden ist. Link-Status kommt aus globalen Variablen, die
# das Modul-TCL (TetraBrew.tcl) bei Connect/TG-Wechsel/Disconnect setzt.
###############################################################################

if {![info exists ::TB_active]} { set ::TB_active 0 }
if {![info exists ::TB_net]}    { set ::TB_net "" }
if {![info exists ::TB_tg]}     { set ::TB_tg 0 }

# "Verbunden … bei <Netz>, eingebucht in Talkgroup <Ziffern>" — nur wenn verbunden.
proc tb_announce_status {} {
  if {!$::TB_active} { return }
  playSilence 300
  playMsg TetraBrew connected
  catch { playMsg TetraBrew net_$::TB_net }
  playSilence 150
  playMsg TetraBrew linked
  playSilence 100
  foreach d [split $::TB_tg ""] {
    if {[string is digit -strict $d]} { playMsg TetraBrew $d; playSilence 60 }
  }
}

# send_short_ident / send_long_ident umschließen: erst normale Kennung, dann Status.
# Die Kennungs-Procs liegen im Namespace (Logic:: bzw. der Logik-Namespace).
proc tb_install_ident_hook {} {
  foreach p {Logic::send_short_ident Logic::send_long_ident \
             ORP_RepeaterLogic_Port1::send_short_ident \
             ORP_RepeaterLogic_Port1::send_long_ident} {
    set orig ${p}_tb_orig
    if {[llength [info procs $p]] && ![llength [info procs $orig]]} {
      rename $p $orig
      proc $p {args} "eval $orig \$args; ::tb_announce_status"
      puts "TetraBrew-Ident: Hook fuer $p installiert"
    }
  }
}

# Sofort (diese Datei lädt zuletzt in local/, send_short_ident ist dann final)
# UND verzögert als Backup (falls doch was später überschreibt / after gepumpt wird).
tb_install_ident_hook
catch { after 3000 tb_install_ident_hook }
