/**
@file    TetraBrewConnection.cpp
@brief   BREW-Session: WS (Async::TcpClient) + HTTP-Digest + ACELP + Resampling.
@author  Dirk / DO1XX
@date    2026-08-24

Portiert aus dem erprobten Python-Injektor (relaisd/engine/tetra-bridge/
fm_tetra_injector.py). Läuft komplett in SvxLinks Async-Event-Loop:
kein eigener Thread, kein blockierendes I/O.
*/
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <AsyncAudioSink.h>
#include <AsyncAudioSource.h>
#include <AsyncAudioDecimator.h>
#include <AsyncAudioInterpolator.h>
#include <AsyncAudioFifo.h>

extern "C" {
#include <tetra-codec.h>
}

#include "TetraBrewConnection.h"
#include "multirate_filter_coeff.h"

using namespace std;
using namespace Async;

// Diagnose-Log (landet im svxlink-Log, greppbar mit "TetraBrew/net")
#define TBLOG(x) do { cout << "TetraBrew/net: " << x << endl; } while (0)


/****************************************************************************
 * BREW-Protokoll-Konstanten (identisch zum Python-Injektor)
 ****************************************************************************/
namespace {
  const uint8_t CLASS_SUBSCRIBER = 0xF0, CLASS_CALL = 0xF1, CLASS_FRAME = 0xF2;
  const uint8_t SUB_REGISTER = 0x01, SUB_AFFILIATE = 0x08, SUB_DEAFFILIATE = 0x09;
  const uint8_t CALL_GROUP_TX = 0x02, CALL_GROUP_IDLE = 0x03, CALL_SHORT_TRANSFER = 0x0B;
  const uint8_t FRAME_TRAFFIC = 0x00, FRAME_SDS_TRANSFER = 0x01, FRAME_SDS_REPORT = 0x02;
  const uint16_t STE_LENGTH_BITS = 288;
  const int ACELP_FULL_BYTES = 35;
  const int SAMPLES_PER_FRAME_8K = 480;   // 60 ms @ 8 kHz

  // ---- kleine LE-Pack-Helfer ----
  void put_u16(string &s, uint16_t v) { s.push_back(char(v & 0xff)); s.push_back(char((v >> 8) & 0xff)); }
  void put_u32(string &s, uint32_t v) { for (int i = 0; i < 4; i++) s.push_back(char((v >> (8*i)) & 0xff)); }
  void put_u64(string &s, uint64_t v) { for (int i = 0; i < 8; i++) s.push_back(char((v >> (8*i)) & 0xff)); }
  uint32_t get_u32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }

  void rand_bytes(uint8_t *out, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { size_t r = fread(out, 1, n, f); fclose(f); if (r == n) return; }
    for (size_t i = 0; i < n; i++) out[i] = uint8_t(rand() & 0xff);
  }

  const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  string base64(const uint8_t *in, size_t n) {
    string o;
    for (size_t i = 0; i < n; i += 3) {
      uint32_t v = in[i] << 16;
      if (i+1 < n) v |= in[i+1] << 8;
      if (i+2 < n) v |= in[i+2];
      o.push_back(B64[(v>>18)&0x3f]);
      o.push_back(B64[(v>>12)&0x3f]);
      o.push_back(i+1 < n ? B64[(v>>6)&0x3f] : '=');
      o.push_back(i+2 < n ? B64[v&0x3f]      : '=');
    }
    return o;
  }
}


/****************************************************************************
 * MD5 (public domain, kompakt) — für HTTP-Digest-Auth
 ****************************************************************************/
namespace {
  typedef struct { uint32_t a,b,c,d; uint64_t len; uint8_t buf[64]; size_t n; } MD5_CTX;
  inline uint32_t rol(uint32_t x, int c) { return (x<<c)|(x>>(32-c)); }
  void md5_block(MD5_CTX *m, const uint8_t *p) {
    static const uint32_t K[64] = {
      0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
      0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
      0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
      0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
      0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
      0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
      0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
      0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                              5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                              4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                              6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
      M[i] = p[i*4] | (p[i*4+1]<<8) | (p[i*4+2]<<16) | (uint32_t(p[i*4+3])<<24);
    uint32_t A=m->a,B=m->b,C=m->c,D=m->d;
    for (int i = 0; i < 64; i++) {
      uint32_t F; int g;
      if (i < 16)      { F=(B&C)|(~B&D);      g=i; }
      else if (i < 32) { F=(D&B)|(~D&C);      g=(5*i+1)&15; }
      else if (i < 48) { F=B^C^D;             g=(3*i+5)&15; }
      else             { F=C^(B|~D);          g=(7*i)&15; }
      uint32_t tmp = D; D = C; C = B;
      B += rol(A + F + K[i] + M[g], S[i]);
      A = tmp;
    }
    m->a+=A; m->b+=B; m->c+=C; m->d+=D;
  }
  void md5_init(MD5_CTX *m){ m->a=0x67452301; m->b=0xefcdab89; m->c=0x98badcfe; m->d=0x10325476; m->len=0; m->n=0; }
  void md5_update(MD5_CTX *m, const uint8_t *p, size_t len){
    m->len += len;
    while (len) {
      size_t take = min(len, size_t(64 - m->n));
      memcpy(m->buf + m->n, p, take); m->n += take; p += take; len -= take;
      if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
    }
  }
  void md5_final(MD5_CTX *m, uint8_t out[16]){
    uint64_t bits = m->len * 8;
    uint8_t pad = 0x80; md5_update(m, &pad, 1);
    uint8_t z = 0; while (m->n != 56) md5_update(m, &z, 1);
    uint8_t lb[8]; for (int i=0;i<8;i++) lb[i]=uint8_t((bits>>(8*i))&0xff);
    md5_update(m, lb, 8);
    uint32_t v[4]={m->a,m->b,m->c,m->d};
    for (int i=0;i<4;i++){ out[i*4]=v[i]&0xff; out[i*4+1]=(v[i]>>8)&0xff; out[i*4+2]=(v[i]>>16)&0xff; out[i*4+3]=(v[i]>>24)&0xff; }
  }
  string md5hex(const string &s){
    MD5_CTX m; md5_init(&m); md5_update(&m, (const uint8_t*)s.data(), s.size());
    uint8_t d[16]; md5_final(&m, d);
    static const char *H="0123456789abcdef"; string o;
    for (int i=0;i<16;i++){ o.push_back(H[d[i]>>4]); o.push_back(H[d[i]&0xf]); }
    return o;
  }
}


/****************************************************************************
 * ACELP-Bitpacking (identisch zum Python: 2x 137 Bit -> 274 Bit -> 35 Byte)
 ****************************************************************************/
namespace {
  void encode_full(tetra_codec *enc, const int16_t pcm480[480], uint8_t out35[35]) {
    uint8_t c1[18], c2[18];
    tetra_encode(enc, pcm480,       c1);   // 240 Samples -> Halbrahmen (137 Bit)
    tetra_encode(enc, pcm480 + 240, c2);
    uint8_t bits[35*8]; memset(bits, 0, sizeof(bits));
    int idx = 0;
    for (int i = 0; i < 137; i++) bits[idx++] = (c1[i/8] >> (7-(i%8))) & 1;
    for (int i = 0; i < 137; i++) bits[idx++] = (c2[i/8] >> (7-(i%8))) & 1;  // idx -> 274
    memset(out35, 0, 35);
    for (int i = 0; i < 274; i++) out35[i/8] |= bits[i] << (7-(i%8));
  }
  void decode_full(tetra_codec *dec, const uint8_t in35[35], int16_t pcm480[480]) {
    uint8_t bits[35*8];
    for (int i = 0; i < 35*8; i++) bits[i] = (in35[i/8] >> (7-(i%8))) & 1;
    uint8_t s1[18], s2[18]; memset(s1,0,18); memset(s2,0,18);
    for (int i = 0; i < 137; i++) {
      s1[i/8] |= bits[i]       << (7-(i%8));
      s2[i/8] |= bits[i + 137] << (7-(i%8));
    }
    tetra_decode(dec, s1, pcm480,       0);
    tetra_decode(dec, s2, pcm480 + 240, 0);
  }
}


/****************************************************************************
 * AcelpEncoderSink — 8k float rein, 35-Byte-ACELP-Frames raus
 ****************************************************************************/
class AcelpEncoderSink : public Async::AudioSink
{
  public:
    AcelpEncoderSink(TetraBrewConnection *conn, tetra_codec *enc)
      : m_conn(conn), m_enc(enc) {}

    virtual int writeSamples(const float *samples, int count)
    {
      // Nur encoden, wenn dieser Endpunkt gerade wirklich sendet — spart CPU auf
      // inaktiven Endpunkten und solange der FM-Squelch zu ist.
      if (!m_conn->txActive()) { m_buf.clear(); return count; }
      for (int i = 0; i < count; i++) m_buf.push_back(samples[i]);
      while (int(m_buf.size()) >= SAMPLES_PER_FRAME_8K)
      {
        int16_t pcm[SAMPLES_PER_FRAME_8K];
        for (int i = 0; i < SAMPLES_PER_FRAME_8K; i++)
        {
          float v = m_buf[i] * 32767.0f;
          if (v > 32767.0f) v = 32767.0f; else if (v < -32768.0f) v = -32768.0f;
          pcm[i] = int16_t(v);
        }
        m_buf.erase(m_buf.begin(), m_buf.begin() + SAMPLES_PER_FRAME_8K);
        uint8_t acelp[35];
        encode_full(m_enc, pcm, acelp);
        m_conn->handleEncodedFrame(acelp);
      }
      return count;
    }

    virtual void flushSamples(void) { m_buf.clear(); sourceAllSamplesFlushed(); }

  private:
    TetraBrewConnection *m_conn;
    tetra_codec         *m_enc;
    vector<float>        m_buf;
};


/****************************************************************************
 * AcelpDecoderSource — ACELP-Frames rein, 8k float raus (mit Rückstau-Puffer)
 ****************************************************************************/
class AcelpDecoderSource : public Async::AudioSource
{
  public:
    void pushSamples(const float *samples, int n)
    {
      m_buf.insert(m_buf.end(), samples, samples + n);
      flush();
    }
    void endStream(void) { m_ending = true; flush(); }

    virtual void resumeOutput(void) { flush(); }
    virtual void allSamplesFlushed(void) {}

  private:
    void flush(void)
    {
      while (!m_buf.empty())
      {
        int wr = sinkWriteSamples(&m_buf[0], int(m_buf.size()));
        if (wr <= 0) return;
        m_buf.erase(m_buf.begin(), m_buf.begin() + wr);
      }
      if (m_ending) { m_ending = false; sinkFlushSamples(); }
    }
    vector<float> m_buf;
    bool          m_ending = false;
};


/****************************************************************************
 * TetraBrewConnection
 ****************************************************************************/
TetraBrewConnection::TetraBrewConnection(const string& host, int port,
                                         const string& host_header,
                                         const string& user, const string& pass,
                                         const string& realm, uint32_t src_issi,
                                         float rx_gain, int prebuf_ms)
  : m_host(host), m_host_header(host_header.empty() ? host : host_header),
    m_user(user), m_pass(pass), m_realm(realm),
    m_port(port), m_rx_gain(rx_gain), m_prebuf_ms(prebuf_ms), m_src_issi(src_issi)
{
  m_encoder = tetra_encoder_create();
  m_decoder = tetra_decoder_create();
  buildAudio();

  m_con.connected.connect(sigc::mem_fun(*this, &TetraBrewConnection::onConnected));
  m_con.disconnected.connect(sigc::mem_fun(*this, &TetraBrewConnection::onDisconnected));
  m_con.dataReceived.connect(sigc::mem_fun(*this, &TetraBrewConnection::onDataReceived));
}


TetraBrewConnection::~TetraBrewConnection(void)
{
  if (m_con.isConnected()) m_con.disconnect();
  teardownAudio();
  if (m_encoder) tetra_codec_destroy(m_encoder);
  if (m_decoder) tetra_codec_destroy(m_decoder);
}


void TetraBrewConnection::buildAudio(void)
{
  // TX: 16k -> Decimator(2) -> 8k -> AcelpEncoderSink
  m_down = new AudioDecimator(2, coeff_16_8, coeff_16_8_taps);
  m_enc  = new AcelpEncoderSink(this, m_encoder);
  m_down->registerSink(m_enc);

  // RX: AcelpDecoderSource -> Fifo(Jitter) -> Interpolator(2) -> 16k
  m_dec  = new AcelpDecoderSource;
  m_fifo = new AudioFifo(8000);           // 1 s @ 8 kHz
  int pre = m_prebuf_ms * 8;              // 8 Samples/ms @ 8 kHz
  if (pre < 240) pre = 240; if (pre > 6400) pre = 6400;   // 30 ms .. 800 ms
  m_fifo->setPrebufSamples(pre);          // Jitter-Vorpuffer (RX_JITTER_MS)
  m_fifo->setOverwrite(true);
  m_up   = new AudioInterpolator(2, coeff_16_8, coeff_16_8_taps);
  m_dec->registerSink(m_fifo);
  m_fifo->registerSink(m_up);
}


void TetraBrewConnection::teardownAudio(void)
{
  // Reihenfolge: Senken zuerst (jeder Dtor meldet sich automatisch ab).
  delete m_enc;  m_enc  = 0;
  delete m_down; m_down = 0;
  delete m_up;   m_up   = 0;
  delete m_fifo; m_fifo = 0;
  delete m_dec;  m_dec  = 0;
}


Async::AudioSink   *TetraBrewConnection::txSink(void)   { return m_down; }
Async::AudioSource *TetraBrewConnection::rxSource(void) { return m_up; }


/*--------------------------- Verbindungsauf-/abbau -------------------------*/
void TetraBrewConnection::connect(void)
{
  if (m_state != ST_IDLE && m_state != ST_CLOSED) return;
  m_rxbuf.clear();
  m_session.clear();
  m_www_auth.clear();
  m_reconnect_as = ST_IDLE;
  m_state = ST_HTTP_CHALLENGE;
  TBLOG("connect() -> " << m_host << ":" << m_port << " (Challenge)");
  m_con.connect(m_host, uint16_t(m_port));
}


void TetraBrewConnection::disconnect(void)
{
  if (m_state == ST_WS_UP)
  {
    if (m_local_tx) sendGroupIdle();
    if (m_cur_tg) sendAffiliate(m_cur_tg, false);
  }
  m_state = ST_CLOSED;
  m_reconnect_as = ST_IDLE;
  if (m_con.isConnected()) m_con.disconnect();
}


void TetraBrewConnection::onConnected(void)
{
  m_rxbuf.clear();

  // TCP-Keepalive: erkennt still gestorbene Verbindungen (z.B. naechtliche
  // DSL-Zwangstrennung/NAT-Drop). Ohne das haengt die Session halb-offen und
  // es kommt nie ein Reconnect. Tot nach ~45 + 3*15 = ~90 s -> onDisconnected.
  int fd = m_con.fd();
  if (fd >= 0)
  {
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
#ifdef TCP_KEEPIDLE
    int idle = 45, intvl = 15, cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
#endif
  }

  switch (m_state)
  {
    case ST_HTTP_CHALLENGE:
      TBLOG("TCP up -> GET /brew/ (ohne Auth)");
      sendHttpChallenge();
      break;
    case ST_HTTP_AUTH:
      TBLOG("TCP up -> GET /brew/ + Digest-Auth");
      sendHttpAuth(m_www_auth);
      break;
    case ST_WS_UPGRADE:
      TBLOG("TCP up -> WS-Upgrade auf " << m_session);
      sendWsUpgrade();
      break;
    default:
      break;
  }
}


void TetraBrewConnection::onDisconnected(Async::TcpConnection *,
                                         Async::TcpConnection::DisconnectReason reason)
{
  TBLOG("onDisconnected reason=" << Async::TcpConnection::disconnectReasonStr(reason)
        << " state=" << m_state << " reconnect_as=" << m_reconnect_as);
  if (m_reconnect_as != ST_IDLE)
  {
    // Nächster Handshake-Schritt auf FRISCHER Verbindung (Server schließt je Schritt).
    m_state = m_reconnect_as;
    m_reconnect_as = ST_IDLE;
    m_rxbuf.clear();
    m_con.connect(m_host, uint16_t(m_port));
    return;
  }
  bool was_up = (m_state == ST_WS_UP);
  m_state = ST_CLOSED;
  m_tetra_active = false;
  m_local_tx = false;
  if (was_up) sigDisconnected();
}


void TetraBrewConnection::abortHandshake(void)
{
  m_reconnect_as = ST_IDLE;
  m_state = ST_CLOSED;
  if (m_con.isConnected()) m_con.disconnect();
  sigDisconnected();
}


int TetraBrewConnection::onDataReceived(Async::TcpConnection *, void *buf, int count)
{
  m_rxbuf.append(static_cast<char*>(buf), count);

  if (m_state == ST_HTTP_CHALLENGE || m_state == ST_HTTP_AUTH || m_state == ST_WS_UPGRADE)
  {
    // Genau EINE HTTP-Antwort pro Verbindung — der Server schließt danach.
    int status; map<string,string> hdr; string body;
    if (parseHttpResponse(status, hdr, body))
    {
      TBLOG("HTTP status=" << status << " (phase=" << m_state << ")");
      if (m_state == ST_HTTP_CHALLENGE)
      {
        if (status == 401)
        {
          m_www_auth = hdr.count("www-authenticate") ? hdr["www-authenticate"] : "";
          TBLOG("401 Challenge -> warte auf Server-Close, dann Reconnect für Auth");
          m_reconnect_as = ST_HTTP_AUTH;   // Server schließt -> onDisconnected reconnectet
        }
        else { TBLOG("unerwartet statt 401 -> Abbruch"); abortHandshake(); }
      }
      else if (m_state == ST_HTTP_AUTH)
      {
        if (status == 200)
        {
          m_session = body;            // Body = WS-Pfad "/brew/<session>"
          while (!m_session.empty() &&
                 (m_session.back()=='\r' || m_session.back()=='\n' || m_session.back()==' '))
            m_session.pop_back();
          TBLOG("200 OK, Session='" << m_session << "' -> warte auf Server-Close, dann WS-Upgrade");
          m_reconnect_as = ST_WS_UPGRADE;   // Server schließt -> onDisconnected reconnectet
        }
        else { TBLOG("Auth abgelehnt status=" << status << " -> Abbruch"); abortHandshake(); }
      }
      else /* ST_WS_UPGRADE */
      {
        if (status == 101)
        {
          TBLOG("101 -> WS UP. REGISTER + AFFILIATE TG " << m_cur_tg);
          m_state = ST_WS_UP;
          sendRegister();
          if (m_cur_tg) sendAffiliate(m_cur_tg, true);   // wie Python register()
          sigConnected();
          processWsBuffer();           // evtl. schon mitgelieferte WS-Bytes
        }
        else { TBLOG("WS-Upgrade abgelehnt status=" << status << " -> Abbruch"); abortHandshake(); }
      }
    }
  }
  else if (m_state == ST_WS_UP)
  {
    processWsBuffer();
  }
  return count;
}


/*------------------------------- HTTP ------------------------------------*/
void TetraBrewConnection::sendHttpChallenge(void)
{
  ostringstream req;
  req << "GET /brew/ HTTP/1.1\r\n"
      << "Host: " << m_host_header << "\r\n"
      << "X-Brew-Version: 1\r\n"      // manche Server verlangen das -> sonst 403
      << "X-Brew-Mode: Basestation\r\n"
      << "Connection: close\r\n"      // Server (auch nginx) schließt -> Reconnect für Auth
      << "\r\n";
  string s = req.str();
  m_con.write(s.data(), int(s.size()));
}


void TetraBrewConnection::sendHttpAuth(const string& www_authenticate)
{
  // Digest-Parameter AUS DER CHALLENGE ziehen — realm/opaque sind server-spezifisch!
  // (manche Server nutzen einen eigenen Realm statt "brew" -> sonst falscher Hash -> 403.)
  const string w = www_authenticate;
  struct Ex {
    const string& s;
    string operator()(const char* key) const {
      string k = string(key) + "=";
      size_t p = s.find(k);
      if (p == string::npos) return string();
      p += k.size();
      if (p < s.size() && s[p] == '"') { p++; size_t e = s.find('"', p); return s.substr(p, e - p); }
      size_t e = s.find(',', p); return s.substr(p, e == string::npos ? string::npos : e - p);
    }
  } ex{w};

  string realm = ex("realm"); if (realm.empty()) realm = m_realm;
  string nonce = ex("nonce");
  bool   has_opaque = (w.find("opaque=") != string::npos);
  string opaque = ex("opaque");

  const string uri="/brew/", qop="auth", nc="00000001";
  uint8_t rb[8]; rand_bytes(rb, 8);
  static const char *H="0123456789abcdef"; string cnonce;
  for (int i=0;i<8;i++){ cnonce.push_back(H[rb[i]>>4]); cnonce.push_back(H[rb[i]&0xf]); }

  string ha1 = md5hex(m_user + ":" + realm + ":" + m_pass);
  string ha2 = md5hex("GET:" + uri);
  string resp = md5hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);

  ostringstream req;
  req << "GET /brew/ HTTP/1.1\r\n"
      << "Host: " << m_host_header << "\r\n"
      << "X-Brew-Version: 1\r\n"
      << "X-Brew-Mode: Basestation\r\n"
      << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << realm
      << "\", nonce=\"" << nonce << "\", uri=\"" << uri << "\", qop=" << qop
      << ", nc=" << nc << ", cnonce=\"" << cnonce << "\", response=\"" << resp << "\"";
  if (has_opaque) req << ", opaque=\"" << opaque << "\"";
  req << ", algorithm=MD5\r\n"
      << "Connection: close\r\n"      // Server schließt -> Reconnect für WS-Upgrade
      << "\r\n";
  string s = req.str();
  m_con.write(s.data(), int(s.size()));
}


void TetraBrewConnection::sendWsUpgrade(void)
{
  uint8_t key[16]; rand_bytes(key, 16);
  string secKey = base64(key, 16);
  ostringstream req;
  req << "GET " << m_session << " HTTP/1.1\r\n"
      << "Host: " << m_host_header << "\r\n"
      << "X-Brew-Version: 1\r\n"
      << "X-Brew-Mode: Basestation\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << secKey << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n"
      << "Sec-WebSocket-Protocol: brew\r\n"
      << "\r\n";
  string s = req.str();
  m_con.write(s.data(), int(s.size()));
}


bool TetraBrewConnection::parseHttpResponse(int &status,
                                            map<string,string>& hdr, string& body)
{
  size_t hdr_end = m_rxbuf.find("\r\n\r\n");
  if (hdr_end == string::npos) return false;
  string head = m_rxbuf.substr(0, hdr_end);
  size_t body_start = hdr_end + 4;

  // Statuszeile
  status = 0;
  size_t sp = head.find(' ');
  if (sp != string::npos) status = atoi(head.c_str() + sp + 1);

  // Header (lowercase keys)
  hdr.clear();
  size_t pos = head.find("\r\n");
  while (pos != string::npos)
  {
    size_t next = head.find("\r\n", pos + 2);
    string line = head.substr(pos + 2, (next==string::npos?head.size():next) - (pos + 2));
    size_t colon = line.find(':');
    if (colon != string::npos)
    {
      string k = line.substr(0, colon), v = line.substr(colon + 1);
      transform(k.begin(), k.end(), k.begin(), ::tolower);
      while (!v.empty() && (v.front()==' '||v.front()=='\t')) v.erase(v.begin());
      hdr[k] = v;
    }
    pos = next;
  }

  size_t clen = hdr.count("content-length") ? size_t(atoi(hdr["content-length"].c_str())) : 0;
  if (m_rxbuf.size() < body_start + clen) return false;   // Body noch nicht komplett
  body = m_rxbuf.substr(body_start, clen);
  m_rxbuf.erase(0, body_start + clen);
  return true;
}


/*------------------------------ WebSocket --------------------------------*/
void TetraBrewConnection::wsSend(const uint8_t *data, size_t len)
{
  if (m_state != ST_WS_UP) return;
  string f;
  f.push_back(char(0x82));                 // FIN + Binary
  uint8_t mask_bit = 0x80;
  if (len < 126) f.push_back(char(mask_bit | len));
  else if (len < 65536) { f.push_back(char(mask_bit | 126)); f.push_back(char((len>>8)&0xff)); f.push_back(char(len&0xff)); }
  else { f.push_back(char(mask_bit | 127)); for (int i=7;i>=0;i--) f.push_back(char((uint64_t(len)>>(8*i))&0xff)); }
  uint8_t mk[4]; rand_bytes(mk, 4);
  for (int i=0;i<4;i++) f.push_back(char(mk[i]));
  for (size_t i=0;i<len;i++) f.push_back(char(data[i] ^ mk[i&3]));
  m_con.write(f.data(), int(f.size()));
}


void TetraBrewConnection::processWsBuffer(void)
{
  for (;;)
  {
    if (m_rxbuf.size() < 2) return;
    const uint8_t *p = reinterpret_cast<const uint8_t*>(m_rxbuf.data());
    uint8_t opcode = p[0] & 0x0f;
    bool masked = (p[1] & 0x80) != 0;
    uint64_t len = p[1] & 0x7f;
    size_t off = 2;
    if (len == 126) { if (m_rxbuf.size() < 4) return; len = (uint64_t(p[2])<<8)|p[3]; off = 4; }
    else if (len == 127) { if (m_rxbuf.size() < 10) return; len=0; for(int i=0;i<8;i++) len=(len<<8)|p[2+i]; off = 10; }
    size_t mask_off = off;
    if (masked) off += 4;
    if (m_rxbuf.size() < off + len) return;    // Frame noch nicht komplett

    vector<uint8_t> payload(len);
    for (uint64_t i=0;i<len;i++)
    {
      uint8_t b = p[off+i];
      if (masked) b ^= p[mask_off + (i&3)];
      payload[i] = b;
    }
    m_rxbuf.erase(0, off + len);

    if (opcode == 0x8) { disconnect(); return; }          // Close
    else if (opcode == 0x9)                               // Ping -> Pong
    {
      string f; f.push_back(char(0x8A));
      uint8_t mb=0x80;
      f.push_back(char(mb | (len<126?len:125)));
      uint8_t mk[4]; rand_bytes(mk,4); for(int i=0;i<4;i++) f.push_back(char(mk[i]));
      size_t n = len<126?len:125;
      for (size_t i=0;i<n;i++) f.push_back(char(payload[i]^mk[i&3]));
      m_con.write(f.data(), int(f.size()));
    }
    else if (opcode == 0x2 || opcode == 0x1)              // Binary / Text-Daten
    {
      onWsMessage(payload.data(), payload.size());
    }
    // opcode 0x0 (Fortsetzung) / 0xA (Pong) ignorieren — BREW nutzt nur Einzelframes
  }
}


void TetraBrewConnection::onWsMessage(const uint8_t *data, size_t len)
{
  if (len < 2) return;
  uint8_t cls = data[0], typ = data[1];

  if (cls == CLASS_CALL && typ == CALL_GROUP_TX && len >= 26)
  {
    string cu(reinterpret_cast<const char*>(data+2), 16);
    uint32_t src = get_u32(data + 18);
    uint32_t g   = get_u32(data + 22);
    m_call_gssi[cu] = g;
    if (g == m_cur_tg && m_cur_tg != 0)
    {
      m_tetra_active = true;
      sigTalkStart(src);
    }
    if (m_call_gssi.size() > 64) m_call_gssi.erase(m_call_gssi.begin());
  }
  else if (cls == CLASS_FRAME && typ == FRAME_TRAFFIC && len >= 20)
  {
    string cu(reinterpret_cast<const char*>(data+2), 16);
    map<string,uint32_t>::iterator it = m_call_gssi.find(cu);
    if (it == m_call_gssi.end() || it->second != m_cur_tg || m_cur_tg == 0) return;
    // Layout: [0xF2 0x00 uuid16][len_bits:u16][STE: 0x00 + 35 ACELP]
    // ACELP beginnt bei Offset 21 (18 Header + 2 len + 1 STE-Präfix)
    if (len < size_t(21 + ACELP_FULL_BYTES)) return;
    const uint8_t *acelp = data + 21;
    int16_t pcm[SAMPLES_PER_FRAME_8K];
    decode_full(m_decoder, acelp, pcm);
    float f[SAMPLES_PER_FRAME_8K];
    for (int i=0;i<SAMPLES_PER_FRAME_8K;i++)
    {
      float v = (pcm[i] / 32768.0f) * m_rx_gain;
      if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
      f[i] = v;
    }
    if (m_dec && !m_rx_muted) m_dec->pushSamples(f, SAMPLES_PER_FRAME_8K);
  }
  else if (cls == CLASS_CALL && typ == CALL_GROUP_IDLE && len >= 18)
  {
    string cu(reinterpret_cast<const char*>(data+2), 16);
    map<string,uint32_t>::iterator it = m_call_gssi.find(cu);
    bool ours = (it != m_call_gssi.end() && it->second == m_cur_tg && m_cur_tg != 0);
    if (it != m_call_gssi.end()) m_call_gssi.erase(it);
    if (ours)
    {
      m_tetra_active = false;
      if (m_dec) m_dec->endStream();     // Fifo leerlaufen -> PTT löst nach Hangtime
      sigTalkStop();
    }
  }
  else if (cls == CLASS_CALL && typ == CALL_SHORT_TRANSFER && len >= 26)
  {
    // SDS-Vorspann: nur wenn an UNSERE ISSI adressiert, Absender merken (uuid->src).
    if (get_u32(data + 22) == m_src_issi)
    {
      string cu(reinterpret_cast<const char*>(data + 2), 16);
      m_sds_src[cu] = get_u32(data + 18);
      if (m_sds_src.size() > 32) m_sds_src.erase(m_sds_src.begin());
    }
  }
  else if (cls == CLASS_FRAME && typ == FRAME_SDS_TRANSFER && len >= 20)
  {
    // Text-SDS: nur wenn Vorspann an uns kam. Layout (nach 18-Byte-Header):
    // [len_bits u16][SDS-TL: 82(PID Text) 0c <ref> 01(8-bit)] + ASCII-Text.
    string cu(reinterpret_cast<const char*>(data + 2), 16);
    map<string,uint32_t>::iterator it = m_sds_src.find(cu);
    if (it == m_sds_src.end()) return;
    uint32_t from = it->second;
    m_sds_src.erase(it);
    size_t nbytes = (data[18] | (data[19] << 8)) / 8;   // len_bits -> Bytes
    if (nbytes < 5 || 20 + nbytes > len) return;
    const uint8_t *c = data + 20;
    if (c[0] != 0x82) return;                            // nur Text-Messaging
    sendSdsReport(data + 2, from);                       // ACK -> kein "failed" beim Absender
    sigSds(from, string(reinterpret_cast<const char*>(c + 4), nbytes - 4));
  }
}


/*------------------------------ BREW-Nachrichten -------------------------*/
void TetraBrewConnection::sendRegister(void)
{
  uint64_t ts = uint64_t(time(0));
  string m;
  m.push_back(char(CLASS_SUBSCRIBER)); m.push_back(char(SUB_REGISTER));
  put_u32(m, m_src_issi); put_u64(m, ts); put_u32(m, 0);
  wsSend(reinterpret_cast<const uint8_t*>(m.data()), m.size());
}


void TetraBrewConnection::sendSds(uint32_t dest_issi, const string& text)
{
  // Text-SDS an eine ISSI (Format wie von BlueStations gesehen): erst der
  // SHORT_TRANSFER-Vorspann (58 Byte, Rest 0), dann FRAME/SDS-TRANSFER mit
  // SDS-TL-Header [82 0c <ref> 01] + 8-bit-ASCII-Text. Gleiche uuid für beide.
  if (m_state != ST_WS_UP) return;
  uint8_t uuid[16]; rand_bytes(uuid, 16);
  string s;
  s.push_back(char(CLASS_CALL)); s.push_back(char(CALL_SHORT_TRANSFER));
  s.append(reinterpret_cast<const char*>(uuid), 16);
  put_u32(s, m_src_issi); put_u32(s, dest_issi);
  s.resize(58, 0);
  wsSend(reinterpret_cast<const uint8_t*>(s.data()), s.size());

  string f;
  f.push_back(char(CLASS_FRAME)); f.push_back(char(FRAME_SDS_TRANSFER));
  f.append(reinterpret_cast<const char*>(uuid), 16);
  put_u16(f, static_cast<uint16_t>((4 + text.size()) * 8));   // len in Bits
  f.push_back(char(0x82)); f.push_back(char(0x0c));
  f.push_back(char(++m_sds_ref)); f.push_back(char(0x01));
  f.append(text);
  wsSend(reinterpret_cast<const uint8_t*>(f.data()), f.size());
}


void TetraBrewConnection::sendSdsReport(const uint8_t *uuid, uint32_t dest_issi)
{
  // Empfangsbestätigung (Delivery-Report) für eine empfangene SDS, damit das
  // Absender-Radio kein "failed" zeigt. Format wie von echten Radios gesehen:
  // SHORT_TRANSFER-Vorspann (gleiche uuid -> routet zum Absender zurück) +
  // FRAME/SDS-REPORT (len_bits=8, Status 0x00 = empfangen).
  if (m_state != ST_WS_UP) return;
  string s;
  s.push_back(char(CLASS_CALL)); s.push_back(char(CALL_SHORT_TRANSFER));
  s.append(reinterpret_cast<const char*>(uuid), 16);
  put_u32(s, m_src_issi); put_u32(s, dest_issi);
  s.resize(58, 0);
  wsSend(reinterpret_cast<const uint8_t*>(s.data()), s.size());

  string f;
  f.push_back(char(CLASS_FRAME)); f.push_back(char(FRAME_SDS_REPORT));
  f.append(reinterpret_cast<const char*>(uuid), 16);
  put_u16(f, 8);
  f.push_back(char(0x00));
  wsSend(reinterpret_cast<const uint8_t*>(f.data()), f.size());
}


void TetraBrewConnection::sendAffiliate(uint32_t gssi, bool on)
{
  uint64_t ts = uint64_t(time(0));
  string m;
  m.push_back(char(CLASS_SUBSCRIBER));
  m.push_back(char(on ? SUB_AFFILIATE : SUB_DEAFFILIATE));
  put_u32(m, m_src_issi); put_u64(m, ts); put_u32(m, 0);
  put_u16(m, 1); put_u32(m, gssi);
  wsSend(reinterpret_cast<const uint8_t*>(m.data()), m.size());
}


void TetraBrewConnection::sendGroupTx(void)
{
  rand_bytes(m_tx_uuid, 16);
  string m;
  m.push_back(char(CLASS_CALL)); m.push_back(char(CALL_GROUP_TX));
  m.append(reinterpret_cast<const char*>(m_tx_uuid), 16);
  put_u32(m, m_src_issi); put_u32(m, m_cur_tg);
  m.push_back(0); m.push_back(0); put_u16(m, 0);
  wsSend(reinterpret_cast<const uint8_t*>(m.data()), m.size());
}


void TetraBrewConnection::sendGroupIdle(void)
{
  string m;
  m.push_back(char(CLASS_CALL)); m.push_back(char(CALL_GROUP_IDLE));
  m.append(reinterpret_cast<const char*>(m_tx_uuid), 16);
  m.push_back(0);
  wsSend(reinterpret_cast<const uint8_t*>(m.data()), m.size());
}


void TetraBrewConnection::sendVoiceFrame(const uint8_t acelp35[35])
{
  string m;
  m.push_back(char(CLASS_FRAME)); m.push_back(char(FRAME_TRAFFIC));
  m.append(reinterpret_cast<const char*>(m_tx_uuid), 16);
  put_u16(m, STE_LENGTH_BITS);
  m.push_back(0);                                   // STE-Präfix
  m.append(reinterpret_cast<const char*>(acelp35), 35);
  wsSend(reinterpret_cast<const uint8_t*>(m.data()), m.size());
}


/*------------------------------- Steuerung -------------------------------*/
void TetraBrewConnection::handleEncodedFrame(const uint8_t acelp35[35])
{
  if (m_state != ST_WS_UP || !m_local_tx || m_cur_tg == 0) return;
  sendVoiceFrame(acelp35);
}


void TetraBrewConnection::setLocalTx(bool on)
{
  if (on == m_local_tx) return;
  if (m_state != ST_WS_UP || m_cur_tg == 0) { m_local_tx = false; return; }
  m_local_tx = on;
  if (on) sendGroupTx();
  else    sendGroupIdle();
}


void TetraBrewConnection::selectTg(uint32_t gssi)
{
  if (gssi == m_cur_tg) return;
  if (m_state == ST_WS_UP)
  {
    if (m_local_tx) { sendGroupIdle(); m_local_tx = false; }
    if (m_cur_tg) sendAffiliate(m_cur_tg, false);
    if (gssi) sendAffiliate(gssi, true);
  }
  m_cur_tg = gssi;
  m_tetra_active = false;
}


/*
 * This file has not been truncated
 */
