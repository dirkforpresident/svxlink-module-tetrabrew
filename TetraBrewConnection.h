/**
@file    TetraBrewConnection.h
@brief   Eine BREW-Session: WebSocket (auf Async::TcpClient) + HTTP-Digest-Auth
         + ACELP-Codec (libtetra-codec) + Resampling 16<->8 kHz.
         Analog zu ModuleEchoLink/QsoImpl, aber fürs BREW/TETRA-Protokoll.
@author  Dirk / DO1XX
@date    2026-08-24

Kapselt die komplette Netz-/Codec-/Audio-Kette einer Verbindung:
  FM->TETRA:  txSink() (16k float) -> Decimator 16->8 -> ACELP-encode -> WS-FRAME
  TETRA->FM:  WS-FRAME -> ACELP-decode -> Fifo -> Interpolator 8->16 -> rxSource() (16k)
Die Protokoll-Logik (Register/Affiliate/GROUP_TX/FRAME/GROUP_IDLE, Digest-Handshake)
ist aus dem bewährten Python-Injektor portiert (relaisd/engine/tetra-bridge).
*/
#ifndef TETRA_BREW_CONNECTION_INCLUDED
#define TETRA_BREW_CONNECTION_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <sigc++/sigc++.h>

#include <AsyncTcpClient.h>

namespace Async
{
  class AudioSink;
  class AudioSource;
  class AudioDecimator;
  class AudioInterpolator;
  class AudioFifo;
};

struct tetra_codec;        // libtetra-codec (opaker Codec-Zustand)
class AcelpEncoderSink;    // AudioSink: 8k -> ACELP-Frame -> WS  (libtetra-codec)
class AcelpDecoderSource;  // AudioSource: WS-Frame -> ACELP -> 8k

// TcpClient mit oeffentlichem FD-Zugriff (socket() ist in TcpConnection protected)
// — noetig, um TCP-Keepalive auf der Session zu setzen.
class KeepaliveTcpClient : public Async::TcpClient<>
{
  public:
    using Async::TcpClient<>::TcpClient;
    int fd(void) const { return socket(); }
};


class TetraBrewConnection : public sigc::trackable
{
  public:
    TetraBrewConnection(const std::string& host, int port,
                        const std::string& host_header,
                        const std::string& user, const std::string& pass,
                        const std::string& realm, uint32_t src_issi,
                        float rx_gain, int prebuf_ms = 120);
    ~TetraBrewConnection(void);

    // --- Audio-Anschlusspunkte fürs Modul ---
    Async::AudioSink   *txSink(void);     // Repeater-RX rein (16k) -> TETRA
    Async::AudioSource *rxSource(void);   // TETRA -> Repeater-TX (16k)

    // --- Steuerung ---
    void connect(void);                   // BREW-Handshake + Register
    void disconnect(void);
    void selectTg(uint32_t gssi);         // Talkgroup wählen (0 = trennen)
    void setLocalTx(bool on);             // FM-Squelch -> GROUP_TX/GROUP_IDLE
    void muteRx(bool on) { m_rx_muted = on; }  // TETRA->FM-Audio unterdrücken (Zeitsperre)
    bool tetraActive(void) const { return m_tetra_active; }
    bool isConnected(void) const { return m_state == ST_WS_UP; }
    uint32_t currentTg(void) const { return m_cur_tg; }
    // Sendet dieser Endpunkt gerade wirklich FM->TETRA? Nur dann lohnt Encoden.
    bool txActive(void) const { return m_state == ST_WS_UP && m_local_tx && m_cur_tg != 0; }

    // --- Signale an das Modul ---
    sigc::signal<void>            sigConnected;
    sigc::signal<void>            sigDisconnected;
    sigc::signal<void, uint32_t>  sigTalkStart;   // ISSI des TETRA-Sprechers
    sigc::signal<void>            sigTalkStop;
    sigc::signal<void, uint32_t, std::string> sigSds;   // (Absender-ISSI, Text) einer SDS an uns

    // --- vom AcelpEncoderSink aufgerufen (ein fertiger 35-Byte-ACELP-Frame) ---
    void handleEncodedFrame(const uint8_t acelp35[35]);

  private:
    enum State { ST_IDLE, ST_HTTP_CHALLENGE, ST_HTTP_AUTH,
                 ST_WS_CONNECTING, ST_WS_UPGRADE, ST_WS_UP, ST_CLOSED };

    std::string m_host, m_host_header, m_user, m_pass, m_realm;
    int         m_port;
    float       m_rx_gain;
    int         m_prebuf_ms = 120;   // Jitter-Vorpuffer (ms) vor Ausgabe TETRA->FM
    uint32_t    m_src_issi;
    uint32_t    m_cur_tg = 0;
    bool        m_tetra_active = false;   // TETRA->FM Ruf läuft
    bool        m_rx_muted = false;       // TETRA->FM-Audio unterdrückt (Zeitsperre gerissen)
    bool        m_local_tx = false;       // FM->TETRA Ruf läuft

    // Netz / Handshake-Zustand
    // Der BREW-Server (websockets-Lib, process_request) schließt die Verbindung
    // nach JEDER HTTP-Antwort (401/200). Darum: FRISCHE Verbindung pro Schritt
    // (Challenge -> Auth -> WS-Upgrade), gesteuert über m_reconnect_as.
    KeepaliveTcpClient m_con;
    State       m_state = ST_IDLE;
    State       m_reconnect_as = ST_IDLE;  // nächster Schritt nach dem Reconnect
    std::string m_www_auth;               // WWW-Authenticate-Header aus dem 401
    std::string m_rxbuf;                  // roher TCP-Empfangspuffer
    std::string m_session;                // WS-Pfad aus dem 200er-Body
    uint8_t     m_tx_uuid[16];            // UUID des laufenden FM->TETRA-Rufs

    // TETRA->FM: call_uuid -> GSSI (nur gebrückte Gruppe auf FM lassen)
    std::map<std::string, uint32_t> m_call_gssi;
    std::map<std::string, uint32_t> m_sds_src;   // uuid -> Absender-ISSI (SHORT_TRANSFER->FRAME)
    uint8_t     m_sds_ref = 0;                    // Message-Reference-Zähler für gesendete SDS

    // Codec + Audio-Ketten
    tetra_codec *m_encoder = 0;
    tetra_codec *m_decoder = 0;
    Async::AudioDecimator   *m_down = 0;   // 16->8 (TX)
    AcelpEncoderSink        *m_enc  = 0;
    AcelpDecoderSource      *m_dec  = 0;
    Async::AudioFifo        *m_fifo = 0;   // Jitter-Puffer (RX)
    Async::AudioInterpolator*m_up   = 0;   // 8->16 (RX)

    // --- TcpClient-Callbacks ---
    void onConnected(void);
    void onDisconnected(Async::TcpConnection *con,
                        Async::TcpConnection::DisconnectReason reason);
    int  onDataReceived(Async::TcpConnection *con, void *buf, int count);

    // --- Handshake / HTTP ---
    void abortHandshake(void);            // Handshake abbrechen + sigDisconnected
    void sendHttpChallenge(void);         // GET /brew/  (ohne Auth)
    void sendHttpAuth(const std::string& www_authenticate);
    void sendWsUpgrade(void);             // GET {session} + Upgrade
    bool parseHttpResponse(int &status, std::map<std::string,std::string>& hdr,
                           std::string& body);   // aus m_rxbuf, konsumiert

    // --- WebSocket ---
    void wsSend(const uint8_t *data, size_t len);   // maskiertes Binär-Frame
    void processWsBuffer(void);                       // Frames aus m_rxbuf ziehen
    void onWsMessage(const uint8_t *data, size_t len);

    // --- BREW-Protokoll (Port aus Python-Injektor) ---
    void sendRegister(void);
    void sendAffiliate(uint32_t gssi, bool on);
    void sendGroupTx(void);
    void sendGroupIdle(void);
    void sendVoiceFrame(const uint8_t acelp35[35]);
  public:
    void sendSds(uint32_t dest_issi, const std::string& text);   // Text-SDS an eine ISSI
  private:
    void sendSdsReport(const uint8_t *uuid, uint32_t dest_issi); // Empfangsbestätigung (ACK)

    void teardownAudio(void);
    void buildAudio(void);
};

#endif /* TETRA_BREW_CONNECTION_INCLUDED */
