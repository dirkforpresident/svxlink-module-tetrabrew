# svxheaders — SvxLink-Server-Header für den Out-of-Tree-Modulbau

Diese Header werden IN-TREE aus dem SvxLink-Source auto-generiert bzw. sind Server-intern
(nicht in `/usr/include/svxlink` installiert). Für den Out-of-Tree-Bau hier hinterlegt.

**⚠️ Version halten:** `version/SVXLINK.h` (`SVXLINK_APP_VERSION`) MUSS exakt zur laufenden
SvxLink-Version passen (sonst lädt das Modul nicht — Module.cpp prüft strikt). Aktuell: **1.7.0**
(DO0RAM). `Module.h`/`Logic.h` aus dem passenden Release-Tag (19.09.2 = svxlink 1.7.0).
Die Async-Header kommen aus `/usr/include/svxlink` (= die installierte Version, passt automatisch).
