#!/bin/bash
# Out-of-tree Build von ModuleTetraBrew gegen ein installiertes SvxLink (armhf/aarch64).
# Läuft AUF dem Ziel-Pi. Async/Config-Header aus /usr/include/svxlink (= installierte SvxLink-Version),
# Module.h + Versions-Header aus svxheaders/ (passend zur Ziel-Version halten!).
# libtetra-codec.so + tetra-codec.h aus /usr/local (per build_codec / gcc gebaut).
set -e
: "${SR:=16000}"   # INTERNAL_SAMPLE_RATE der Ziel-SvxLink (Default 16000)
g++ -shared -fPIC -std=c++11 -O2 -o ModuleTetraBrew.so \
  ModuleTetraBrew.cpp TetraBrewConnection.cpp \
  -I/usr/include/svxlink -Isvxheaders -I/usr/local/include \
  -DINTERNAL_SAMPLE_RATE=${SR} \
  $(pkg-config --cflags sigc++-2.0) \
  -L/usr/local/lib -ltetra-codec
echo "Gebaut: $(file ModuleTetraBrew.so | cut -d, -f1-3)"
echo "Installieren: cp ModuleTetraBrew.so \$(grep -E '^MODULE_PATH' /etc/svxlink/svxlink.conf | cut -d= -f2)/"
