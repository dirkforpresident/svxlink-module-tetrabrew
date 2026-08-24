#!/usr/bin/env python3
"""Winziger TLS-Proxy: 127.0.0.1:<LPORT> -> TLS -> <RHOST>:<RPORT>.

Für ModuleTetraBrew gegen TLS-BREW-Server (z.B. freetetra.de), wenn das
installierte SvxLink/Async kein TLS kann. Nur python3-Stdlib, keine Abhängigkeiten.
System-CA-Verify + SNI + Hostname-Check (wie stunnel verifyChain).

Konfiguration über Env: TLSP_LHOST TLSP_LPORT TLSP_RHOST TLSP_RPORT.
"""
import os, socket, ssl, threading

LHOST = os.environ.get("TLSP_LHOST", "127.0.0.1")
LPORT = int(os.environ.get("TLSP_LPORT", "18443"))
RHOST = os.environ.get("TLSP_RHOST", "freetetra.de")
RPORT = int(os.environ.get("TLSP_RPORT", "443"))

ctx = ssl.create_default_context()  # System-CAs, prüft Cert + Hostname


def pipe(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except Exception:
        pass
    finally:
        try:
            b.shutdown(socket.SHUT_WR)
        except Exception:
            pass


def handle(client):
    try:
        raw = socket.create_connection((RHOST, RPORT), timeout=10)
        raw.settimeout(None)   # WICHTIG: sonst kappt der 10s-Connect-Timeout die Idle-WS-Verbindung
        tls = ctx.wrap_socket(raw, server_hostname=RHOST)
    except Exception:
        try:
            client.close()
        except Exception:
            pass
        return
    threading.Thread(target=pipe, args=(client, tls), daemon=True).start()
    pipe(tls, client)
    for s in (client, tls):
        try:
            s.close()
        except Exception:
            pass


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((LHOST, LPORT))
    srv.listen(64)
    print(f"TLS-Proxy {LHOST}:{LPORT} -> {RHOST}:{RPORT}", flush=True)
    while True:
        c, _ = srv.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
