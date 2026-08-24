/**
@file  test_brew.cpp
@brief Standalone-Smoke-Test der TetraBrewConnection (BREW-Handshake) — ohne
       SvxLink/Repeater. Verbindet gegen den echten BREW-Server und loggt den
       kompletten Handshake. Aufruf: ./test_brew <host> <port> <user> <pass> <issi> <tg>
*/
#include <iostream>
#include <cstdlib>
#include <AsyncCppApplication.h>
#include <AsyncTimer.h>
#include "TetraBrewConnection.h"

using namespace std;
using namespace Async;

struct Watcher : public sigc::trackable
{
  CppApplication *app;
  void onConn(void)  { cout << "TEST: >>> sigConnected — BREW-Handshake OK!" << endl; }
  void onDisc(void)  { cout << "TEST: <<< sigDisconnected" << endl; }
  void onTalk(uint32_t issi) { cout << "TEST: TETRA-Sprecher ISSI " << issi << endl; }
  void onQuit(Timer*) { cout << "TEST: Zeit abgelaufen — Ende." << endl; app->quit(); }
};

int main(int argc, char **argv)
{
  string host = argc > 1 ? argv[1] : "10.60.100.189";
  int    port = argc > 2 ? atoi(argv[2]) : 8443;
  string user = argc > 3 ? argv[3] : "2635718";
  string pass = argc > 4 ? argv[4] : "changeme";
  uint32_t issi = argc > 5 ? strtoul(argv[5], 0, 10) : 2620001;
  uint32_t tg   = argc > 6 ? strtoul(argv[6], 0, 10) : 1;
  string host_header = argc > 7 ? argv[7] : "";

  CppApplication app;
  Watcher w; w.app = &app;

  TetraBrewConnection conn(host, port, host_header, user, pass, "brew", issi, 4.0f);
  conn.sigConnected.connect(sigc::mem_fun(w, &Watcher::onConn));
  conn.sigDisconnected.connect(sigc::mem_fun(w, &Watcher::onDisc));
  conn.sigTalkStart.connect(sigc::mem_fun(w, &Watcher::onTalk));

  conn.selectTg(tg);
  cout << "TEST: verbinde " << host << ":" << port << " user=" << user
       << " issi=" << issi << " tg=" << tg << endl;
  conn.connect();

  Timer t(10000);
  t.expired.connect(sigc::mem_fun(w, &Watcher::onQuit));
  app.exec();
  return 0;
}
