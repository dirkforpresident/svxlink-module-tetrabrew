/**
@file    ModuleTetraBrew.cpp
@brief   SvxLink-Modul TETRA-Brew Bridge (Multi-Endpunkt) — Implementierung.
@author  Dirk / DO1XX
@date    2026-08-24
*/
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <set>

#include <AsyncConfig.h>
#include <AsyncAudioSplitter.h>
#include <AsyncAudioSelector.h>
#include <AsyncAudioPassthrough.h>
#include <AsyncTimer.h>

#include "version/MODULE_TETRA_BREW.h"
#include "ModuleTetraBrew.h"
#include "TetraBrewConnection.h"

using namespace std;
using namespace Async;


extern "C" {
  Module *module_init(void *dl_handle, Logic *logic, const char *cfg_name)
  {
    return new ModuleTetraBrew(dl_handle, logic, cfg_name);
  }
}


namespace {
  // "1,2,262" -> Menge von GSSIs
  set<uint32_t> parseGssiList(const string& s)
  {
    set<uint32_t> out;
    stringstream ss(s); string tok;
    while (getline(ss, tok, ','))
    {
      // Whitespace trimmen
      size_t a = tok.find_first_not_of(" \t");
      size_t b = tok.find_last_not_of(" \t");
      if (a == string::npos) continue;
      uint32_t v = static_cast<uint32_t>(strtoul(tok.substr(a, b-a+1).c_str(), 0, 10));
      if (v) out.insert(v);
    }
    return out;
  }
}


ModuleTetraBrew::ModuleTetraBrew(void *dl_handle, Logic *logic,
                                 const string& cfg_name)
  : Module(dl_handle, logic, cfg_name)
{
  cout << "\tModule TetraBrew v" MODULE_TETRA_BREW_VERSION " (Multi-Brew) starting...\n";
}


ModuleTetraBrew::~ModuleTetraBrew(void)
{
  AudioSink::clearHandler();
  AudioSource::clearHandler();
  delete m_tx_tmo;     m_tx_tmo = 0;
  delete m_status_tmo; m_status_tmo = 0;
  delete m_autocon_tmo; m_autocon_tmo = 0;
  for (size_t i = 0; i < m_eps.size(); i++) { delete m_eps[i].conn; m_eps[i].conn = 0; }
  delete m_local_mon; m_local_mon = 0;
  m_eps.clear();
  delete m_rx_split; m_rx_split = 0;
  delete m_tx_sel;   m_tx_sel = 0;
}


bool ModuleTetraBrew::initialize(void)
{
  if (!Module::initialize())
  {
    return false;
  }

  // --- globale Config ---
  cfg().getValue(cfgName(), "DEFAULT_TG", m_default_tg);
  cfg().getValue(cfgName(), "SRC_ISSI", m_src_issi);
  cfg().getValue(cfgName(), "MAX_TX_TIME", m_max_tx_time);
  cfg().getValue(cfgName(), "STATUS_INTERVAL", m_status_interval);
  cfg().getValue(cfgName(), "ANNOUNCE", m_announce);
  cfg().getValue(cfgName(), "AUTO_CONNECT", m_auto_connect);
  cfg().getValue(cfgName(), "LOCAL_REPEAT", m_local_repeat);

  string servers;
  cfg().getValue(cfgName(), "BREW_SERVERS", servers);
  if (servers.empty())
  {
    cerr << "*** ERROR: ModuleTetraBrew: BREW_SERVERS ist leer.\n";
    return false;
  }

  // --- Endpunkte laden ---
  {
    stringstream ss(servers); string name;
    while (getline(ss, name, ','))
    {
      size_t a = name.find_first_not_of(" \t");
      size_t b = name.find_last_not_of(" \t");
      if (a == string::npos) continue;
      if (!loadEndpoint(name.substr(a, b-a+1)))
      {
        cerr << "*** ERROR: ModuleTetraBrew: Endpunkt '" << name << "' fehlerhaft.\n";
        return false;
      }
    }
  }
  if (m_eps.empty())
  {
    cerr << "*** ERROR: ModuleTetraBrew: keine gültigen Endpunkte.\n";
    return false;
  }

  // --- Audio verdrahten ---
  // FM-RX -> Splitter -> alle Endpunkte (nur der aktive mit local_tx sendet wirklich)
  m_rx_split = new AudioSplitter;
  AudioSink::setHandler(m_rx_split);
  // Endpunkte -> Selector -> FM-TX (nur der aktive liefert Audio)
  m_tx_sel = new AudioSelector;
  AudioSource::setHandler(m_tx_sel);

  for (size_t i = 0; i < m_eps.size(); i++)
  {
    TetraBrewConnection *c = m_eps[i].conn;
    m_rx_split->addSink(c->txSink());
    m_tx_sel->addSource(c->rxSource());
    m_tx_sel->enableAutoSelect(c->rxSource(), 0);
    int idx = static_cast<int>(i);
    c->sigConnected.connect(sigc::bind(sigc::mem_fun(*this, &ModuleTetraBrew::onConnected), idx));
    c->sigDisconnected.connect(sigc::bind(sigc::mem_fun(*this, &ModuleTetraBrew::onDisconnected), idx));
    c->sigTalkStart.connect(sigc::bind(sigc::mem_fun(*this, &ModuleTetraBrew::onTetraTalkStart), idx));
    c->sigTalkStop.connect(sigc::bind(sigc::mem_fun(*this, &ModuleTetraBrew::onTetraTalkStop), idx));
  }

  // Lokaler FM-Repeat: FM-RX zusätzlich direkt auf FM-TX legen (Vorrang vor TETRA),
  // damit FM->FM im Relais sauber durchläuft, während das Modul aktiv ist. Nur so
  // bleibt ein normaler Repeater-Betrieb erhalten (nicht nur Bridge-Audio).
  if (m_local_repeat)
  {
    m_local_mon = new AudioPassthrough;
    m_rx_split->addSink(m_local_mon);
    m_tx_sel->addSource(m_local_mon);
    m_tx_sel->enableAutoSelect(m_local_mon, 10);   // Ortsuser hat Vorrang vor TETRA-Audio
  }

  // MAX_TX_TIME-Wächter (optional)
  if (m_max_tx_time > 0)
  {
    m_tx_tmo = new Timer(m_max_tx_time * 1000, Timer::TYPE_ONESHOT);
    m_tx_tmo->setEnable(false);
    m_tx_tmo->expired.connect(mem_fun(*this, &ModuleTetraBrew::onTxTimeout));
  }

  // Periodische „verbunden mit"-Status-Ansage (läuft nur solange verbunden)
  if (m_status_interval > 0)
  {
    m_status_tmo = new Timer(m_status_interval * 1000, Timer::TYPE_PERIODIC);
    m_status_tmo->setEnable(false);
    m_status_tmo->expired.connect(mem_fun(*this, &ModuleTetraBrew::onStatusTimer));
  }

  // AUTO_CONNECT: Permanent-Knoten — kurz nach dem Start selbst aktivieren (nicht
  // sofort in initialize(), damit die Logic-Core fertig hochgefahren ist). Für einen
  // echten Dauer-Knoten zusätzlich TIMEOUT=0 setzen (kein Inaktivitäts-Release).
  if (m_auto_connect)
  {
    if (m_default_tg == 0)
      cerr << "*** WARNING: ModuleTetraBrew: AUTO_CONNECT gesetzt, aber DEFAULT_TG=0 "
              "-> es gibt keine TG zum Verbinden. Bitte DEFAULT_TG setzen.\n";
    m_autocon_tmo = new Timer(4000, Timer::TYPE_ONESHOT);
    m_autocon_tmo->expired.connect(mem_fun(*this, &ModuleTetraBrew::onAutoConnect));
    cout << "\t  AUTO_CONNECT: Modul aktiviert sich beim Start selbst.\n";
  }

  return true;
}


bool ModuleTetraBrew::loadEndpoint(const string& name)
{
  const string sec = string(cfgName()) + "_" + name;
  Endpoint ep;
  ep.name = name;

  string host, host_header, user, pass, realm = "brew", allow_str;
  int port = 8443;
  uint32_t src_issi = m_src_issi;   // globaler Default, überschreibbar
  double rx_gain = 4.0;

  cfg().getValue(sec, "HOST", host);
  cfg().getValue(sec, "PORT", port);
  cfg().getValue(sec, "HOST_HEADER", host_header);
  cfg().getValue(sec, "USER", user);
  cfg().getValue(sec, "PASSWORD", pass);
  cfg().getValue(sec, "REALM", realm);
  cfg().getValue(sec, "SRC_ISSI", src_issi);
  cfg().getValue(sec, "RX_GAIN", rx_gain);
  cfg().getValue(sec, "GSSI_MIN", ep.gssi_min);
  cfg().getValue(sec, "GSSI_MAX", ep.gssi_max);
  cfg().getValue(sec, "GSSI_ALLOW", allow_str);
  ep.allow = parseGssiList(allow_str);

  if (host.empty() || user.empty())
  {
    cerr << "*** ERROR: [" << sec << "] HOST und USER müssen gesetzt sein.\n";
    return false;
  }
  if (ep.gssi_max == 0) ep.gssi_max = 16777215;   // Default: ganzer 24-Bit-Bereich

  ep.conn = new TetraBrewConnection(host, port, host_header, user, pass, realm,
                                    src_issi, static_cast<float>(rx_gain));
  m_eps.push_back(ep);
  cout << "\t  Endpunkt '" << name << "': " << host << ":" << port
       << " GSSI " << ep.gssi_min << "-" << ep.gssi_max
       << (ep.allow.empty() ? "" : " (Allow-Liste)") << "\n";
  return true;
}


void ModuleTetraBrew::activateInit(void)
{
  // Ab jetzt „soll verbunden bleiben" (nur relevant im Permanent-Modus für Reconnect).
  m_want_connected = m_auto_connect;
  // "5<tg>#" liefert eine Wunsch-TG (m_pending_tg), sonst DEFAULT_TG.
  uint32_t tg = m_pending_tg ? m_pending_tg : m_default_tg;
  m_pending_tg = 0;
  if (tg != 0)
  {
    selectTg(tg);
  }
  else if (m_announce)
  {
    processEvent("tetra_activated");   // "Brücke aktiviert" — TG-Wahl abwarten
  }
  setIdle(false);
}


void ModuleTetraBrew::deactivateCleanup(void)
{
  // Bewusste Deaktivierung (# oder Inaktivitäts-TIMEOUT) -> keinen Reconnect mehr.
  m_want_connected = false;
  m_reconnect_tg = 0;
  m_backoff_s = 0;
  if (m_autocon_tmo) m_autocon_tmo->setEnable(false);
  disconnectAll();
  m_active = -1;
  m_cur_tg = 0;
  if (m_tx_tmo) m_tx_tmo->setEnable(false);
  if (m_status_tmo) m_status_tmo->setEnable(false);
  setIdle(true);
}


bool ModuleTetraBrew::dtmfDigitReceived(char, int)
{
  return false;   // Ziffern nicht konsumieren -> als Kommando sammeln
}


void ModuleTetraBrew::dtmfCmdReceived(const string& cmd)
{
  if (cmd.empty())                 // leer (nur #) = deaktivieren
  {
    deactivateMe();
    return;
  }
  uint32_t gssi = static_cast<uint32_t>(strtoul(cmd.c_str(), 0, 10));
  selectTg(gssi);
}


void ModuleTetraBrew::dtmfCmdReceivedWhenIdle(const string& cmd)
{
  // Ein-Schritt-Start: "5<tg>#" -> Modul aktivieren UND direkt diese TG wählen.
  if (!cmd.empty())
  {
    m_pending_tg = static_cast<uint32_t>(strtoul(cmd.c_str(), 0, 10));
  }
  activateMe();
}


void ModuleTetraBrew::squelchOpen(bool is_open)
{
  if (m_active >= 0)
  {
    m_eps[m_active].conn->setLocalTx(is_open);
  }
  if (m_tx_tmo)                    // FM->TETRA-Durchgang zeitlich begrenzen
  {
    if (is_open) { m_tx_tmo->reset(); m_tx_tmo->setEnable(true); }
    else         { m_tx_tmo->setEnable(false); }
  }
}


void ModuleTetraBrew::allMsgsWritten(void)
{
}


// --------------------------------------------------------------------------

int ModuleTetraBrew::findEndpoint(uint32_t gssi) const
{
  for (size_t i = 0; i < m_eps.size(); i++)
  {
    if (gssi >= m_eps[i].gssi_min && gssi <= m_eps[i].gssi_max)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}


bool ModuleTetraBrew::tgAllowed(const Endpoint& ep, uint32_t gssi) const
{
  if (ep.allow.empty()) return true;             // leer = ganzer Bereich frei
  return ep.allow.find(gssi) != ep.allow.end();
}


void ModuleTetraBrew::selectTg(uint32_t gssi)
{
  int idx = findEndpoint(gssi);
  bool ok = (idx >= 0 && gssi != 0 && tgAllowed(m_eps[idx], gssi));
  cout << "TetraBrew: selectTg(" << gssi << ") -> endpoint=" << idx
       << (ok ? " OK" : " ABGELEHNT") << "\n";
  if (!ok)
  {
    if (m_announce) processEvent("tg_not_allowed");
    return;
  }

  if (idx != m_active)
  {
    // Endpunkt wechseln: alten trennen, neuen verbinden (Ansage kommt bei Connect)
    if (m_active >= 0) m_eps[m_active].conn->disconnect();
    m_active = idx;
    m_cur_tg = gssi;
    m_eps[idx].conn->selectTg(gssi);   // TG vormerken -> wird beim WS-Connect affiliiert
    m_eps[idx].conn->connect();
  }
  else
  {
    // gleicher Endpunkt, nur TG umschalten
    m_cur_tg = gssi;
    m_eps[idx].conn->selectTg(gssi);
    announceLinked();
  }
  setIdle(false);
}


void ModuleTetraBrew::announceLinked(void)
{
  if (!m_announce || m_active < 0) return;
  stringstream ss;
  ss << "linked_to_tg " << m_eps[m_active].name << " " << m_cur_tg;
  processEvent(ss.str());
}


void ModuleTetraBrew::disconnectAll(void)
{
  for (size_t i = 0; i < m_eps.size(); i++)
  {
    if (m_eps[i].conn) m_eps[i].conn->disconnect();
  }
}


void ModuleTetraBrew::onConnected(int ep)
{
  if (ep != m_active) return;
  if (m_announce)
  {
    stringstream ss;
    ss << "brew_connected " << m_eps[ep].name << " " << m_cur_tg;
    processEvent(ss.str());
  }
  if (m_status_tmo) { m_status_tmo->reset(); m_status_tmo->setEnable(true); }
  m_backoff_s = 0;               // erfolgreiche Verbindung -> Backoff zurücksetzen
  setIdle(false);
}


void ModuleTetraBrew::onDisconnected(int ep)
{
  if (ep != m_active) return;
  if (m_announce) processEvent("brew_disconnected");
  if (m_status_tmo) m_status_tmo->setEnable(false);

  // Permanent-Knoten: Verbindung nicht aufgeben, sondern mit Backoff neu aufbauen.
  // m_active bleibt erhalten, damit der Audio-Pfad + die Sprecher-Zuordnung stimmen.
  if (m_want_connected)
  {
    m_reconnect_tg = m_cur_tg ? m_cur_tg : m_default_tg;
    setIdle(false);
    scheduleReconnect();
    return;
  }

  m_active = -1;
  m_cur_tg = 0;
  setIdle(true);
}


void ModuleTetraBrew::scheduleReconnect(void)
{
  // Exponentieller Backoff (5,10,20,40,60,60… s), damit ein dauerhaft schließender
  // Server nicht gehämmert wird. m_autocon_tmo neu aufsetzen (One-Shot).
  m_backoff_s = (m_backoff_s <= 0) ? 5 : (m_backoff_s * 2 > 60 ? 60 : m_backoff_s * 2);
  delete m_autocon_tmo;
  m_autocon_tmo = new Async::Timer(m_backoff_s * 1000, Async::Timer::TYPE_ONESHOT);
  m_autocon_tmo->expired.connect(mem_fun(*this, &ModuleTetraBrew::onAutoConnect));
  cout << "TetraBrew: Reconnect in " << m_backoff_s << " s\n";
}


void ModuleTetraBrew::onTetraTalkStart(uint32_t issi, int ep)
{
  if (ep != m_active) return;
  setIdle(false);
  stringstream ss; ss << "tetra_talker " << issi;
  processEvent(ss.str());
}


void ModuleTetraBrew::onTetraTalkStop(int ep)
{
  if (ep != m_active) return;
  setIdle(!squelchIsOpen());
}


void ModuleTetraBrew::onTxTimeout(Async::Timer*)
{
  // FM-Träger zu lang offen -> FM->TETRA-Ruf zwangsweise beenden (Schutz).
  if (m_active >= 0) m_eps[m_active].conn->setLocalTx(false);
  if (m_tx_tmo) m_tx_tmo->setEnable(false);
}


void ModuleTetraBrew::onStatusTimer(Async::Timer*)
{
  // Periodisch ansagen, womit das Relais verbunden ist (zählt NICHT als Aktivität
  // -> setzt den Inaktivitäts-TIMEOUT nicht zurück).
  if (m_active >= 0 && m_eps[m_active].conn->isConnected())
  {
    announceLinked();
  }
}


void ModuleTetraBrew::onAutoConnect(Async::Timer*)
{
  if (m_active < 0)
  {
    // Erststart (oder Modul war nicht aktiv): Modul aktivieren -> activateInit()
    // verbindet mit DEFAULT_TG (bzw. m_pending_tg).
    cout << "ModuleTetraBrew: AUTO_CONNECT -> aktiviere Brücke.\n";
    if (m_reconnect_tg == 0 && m_default_tg != 0) m_pending_tg = m_default_tg;
    activateMe();
  }
  else if (m_want_connected && !m_eps[m_active].conn->isConnected())
  {
    // Permanent-Knoten nach Abriss: aktiven Endpunkt neu verbinden.
    uint32_t tg = m_reconnect_tg ? m_reconnect_tg : m_default_tg;
    cout << "ModuleTetraBrew: Reconnect zu '" << m_eps[m_active].name
         << "' TG " << tg << ".\n";
    if (tg) m_eps[m_active].conn->selectTg(tg);
    m_eps[m_active].conn->connect();
  }
}


/* This file has not been truncated */
