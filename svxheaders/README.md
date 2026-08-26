# svxheaders — SvxLink-Server-Header für den Out-of-Tree-Modulbau

Diese Header werden IN-TREE aus dem SvxLink-Source auto-generiert bzw. sind Server-intern
(nicht in `/usr/include/svxlink` installiert). Für den Out-of-Tree-Bau hier hinterlegt.

**⚠️ Version halten:** `version/SVXLINK.h` (`SVXLINK_APP_VERSION`) MUSS exakt zur laufenden
SvxLink-Version passen (sonst lädt das Modul nicht — Module.cpp prüft strikt). Aktuell: **1.10.1**
(DO0RAM läuft SvxLink 26.05.1 = libsvxlink 1.10.1, aus Source). `Module.h`/`Logic.h` stammen noch
vom 1.7.0-Tag, sind aber ABI-kompatibel genug (Modul lädt + läuft) — bei Problemen aus dem 26.05-Tag ziehen.
Die Async-Header kommen aus `/usr/include/svxlink` (= die installierte Version, passt automatisch).
