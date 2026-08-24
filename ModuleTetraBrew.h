/**
@file    ModuleTetraBrew.h
@brief   SvxLink-Modul: verbindet den FM-Repeater über BREW (TETRA) mit einem
         ODER MEHREREN TETRA-Netzen — Auswahl per Talkgroup.
@author  Dirk / DO1XX
@date    2026-08-24

Multi-Brew: das Modul hält beliebig viele BREW-Endpunkte (jeder mit eigener
GSSI-Range + Zugangsdaten, alles aus der svxlink.conf). Beim TG-Wechsel wählt es
automatisch den Endpunkt, in dessen Bereich die Talkgroup fällt — z.B.
TG 1–90 -> eigenes FreeTetra, TG 91+ -> ein weiteres Netz. Immer nur EINE TG aktiv.
Nichts hartcodiert: BREW_SERVERS + je eine Sektion [<Modul>_<Name>].
*/
#ifndef MODULE_TETRA_BREW_INCLUDED
#define MODULE_TETRA_BREW_INCLUDED

#include <string>
#include <vector>
#include <set>
#include <cstdint>

#include <Module.h>
#include <version/SVXLINK.h>

namespace Async
{
  class AudioSplitter;
  class AudioSelector;
  class AudioPassthrough;
  class Timer;
};

class TetraBrewConnection;


class ModuleTetraBrew : public Module
{
  public:
    ModuleTetraBrew(void *dl_handle, Logic *logic, const std::string& cfg_name);
    ~ModuleTetraBrew(void);
    const char *compiledForVersion(void) const { return SVXLINK_APP_VERSION; }

  private:
    struct Endpoint
    {
      std::string          name;
      TetraBrewConnection *conn = 0;
      uint32_t             gssi_min = 0;
      uint32_t             gssi_max = 0;
      std::set<uint32_t>   allow;      // leer = ganzer Bereich erlaubt
    };

    // --- Config (global) ---
    uint32_t m_default_tg  = 0;
    uint32_t m_src_issi    = 0;        // Standard-ISSI (pro Endpunkt überschreibbar)
    int      m_max_tx_time = 0;        // FM->TETRA-Durchgangslimit (Sek), 0 = aus
    int      m_status_interval = 0;    // periodische „verbunden mit"-Ansage (Sek), 0 = aus
    bool     m_announce    = true;
    bool     m_auto_connect = false;   // beim Start automatisch aktivieren (Permanent-Knoten)
    bool     m_local_repeat = true;    // lokalen FM-Repeat (RX->TX) parallel weiterlaufen lassen
    bool     m_want_connected = false; // Soll-Zustand: verbunden bleiben (Permanent-Reconnect)
    uint32_t m_reconnect_tg = 0;       // TG, auf die nach Abriss wieder verbunden wird
    int      m_backoff_s   = 0;        // aktueller Reconnect-Backoff (Sek), 0 = frisch

    // --- Endpunkte + Laufzeitzustand ---
    std::vector<Endpoint> m_eps;
    int      m_active = -1;            // Index des aktiven Endpunkts (-1 = keiner)
    uint32_t m_cur_tg = 0;
    uint32_t m_pending_tg = 0;         // TG aus "5<tg>#"-Ein-Schritt-Aktivierung

    // --- Audio ---
    Async::AudioSplitter  *m_rx_split = 0;   // FM-RX -> alle Endpunkte (nur aktiver sendet)
    Async::AudioSelector  *m_tx_sel   = 0;   // Endpunkte + Lokal-Monitor -> FM-TX (Auto-Select)
    Async::AudioPassthrough *m_local_mon = 0; // FM-RX -> FM-TX (lokaler Repeat, wenn aktiv)
    Async::Timer         *m_tx_tmo   = 0;   // MAX_TX_TIME-Wächter
    Async::Timer         *m_status_tmo = 0; // periodische Status-Ansage
    Async::Timer         *m_autocon_tmo = 0; // verzögerter Auto-Connect beim Start

    // --- Module-Overrides ---
    bool initialize(void);
    void activateInit(void);
    void deactivateCleanup(void);
    bool dtmfDigitReceived(char digit, int duration);
    void dtmfCmdReceived(const std::string& cmd);
    void dtmfCmdReceivedWhenIdle(const std::string& cmd);   // "5<tg>#" Ein-Schritt-Start
    void squelchOpen(bool is_open);
    void allMsgsWritten(void);

    // --- Helfer ---
    bool loadEndpoint(const std::string& name);
    int  findEndpoint(uint32_t gssi) const;             // Index oder -1
    bool tgAllowed(const Endpoint& ep, uint32_t gssi) const;
    void selectTg(uint32_t gssi);
    void announceLinked(void);
    void disconnectAll(void);
    void scheduleReconnect(void);   // Permanent-Knoten: Wiederverbindung mit Backoff planen

    // --- Connection-Callbacks (mit Endpunkt-Index gebunden) ---
    void onConnected(int ep);
    void onDisconnected(int ep);
    void onTetraTalkStart(uint32_t issi, int ep);   // gebundener ep hinten (sigc::bind)
    void onTetraTalkStop(int ep);
    void onTxTimeout(Async::Timer *t);
    void onStatusTimer(Async::Timer *t);    // periodische „verbunden mit"-Ansage
    void onAutoConnect(Async::Timer *t);    // AUTO_CONNECT: Selbst-Aktivierung beim Start

};  /* class ModuleTetraBrew */

#endif /* MODULE_TETRA_BREW_INCLUDED */
