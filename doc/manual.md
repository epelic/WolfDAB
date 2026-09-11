---
title: "dabtx -- DAB+ Sender fuer HackRF One"
subtitle: "Bedienungsanleitung"
author: "DM2TIM / DN4SK"
date: 2026-04-13
geometry: margin=2.5cm
fontsize: 11pt
lang: de
toc: true
toc-depth: 3
numbersections: true
---

# Uebersicht

**dabtx** ist ein DAB+ Sender in C/C++ fuer den HackRF One SDR-Transceiver
unter Windows 11. Er realisiert die komplette Signalkette von der
PC-Audioquelle bis zum HF-Signal:

    PC-Audio --> HE-AAC v1 --> DAB+ Superframe --> COFDM Modulator --> HackRF TX

## Einsatzzweck

Das Programm ist fuer **lizenzierte Amateurfunkexperimente** gedacht.
DAB-Band III (174--239 MHz) ist lizenzpflichtig -- Tests nur mit Dummy-Load,
HF-Kaefig oder ausreichender Daempfung. Amateurfunkbaender (70 cm, 23 cm,
13 cm, 6 cm) erfordern ein gueltige Amateurfunkzeugnis (Klasse A in DE).

## Features

- HE-AAC v1 (SBR) Encoder via libfdk-aac (DAB+ Fork)
- Bis zu 4 Audio-Services im Multiplex
- DLS (Dynamic Label Segment) Lauftext
- MOT SlideShow (JPEG/PNG Bilder)
- EPG Programm-Zeitplan via DLS-Rotation
- WASAPI-Loopback fuer bit-perfect Audio-Capture
- Alle DAB Band III Kanaele (5A--13F)
- Amateurfunk-Kanaele (70 cm, 23 cm, 13 cm, 6 cm)
- Konfigurationsdatei (dabtx.cfg)
- Signal-Handling (Ctrl-C fuer sauberes Shutdown)


# Installation

## Voraussetzungen

- **Windows 11** (x64)
- **MSYS2 UCRT64** Toolchain (GCC 15+, Ninja, pkg-config)
- **CMake** 3.20+
- **HackRF One** mit WinUSB-Treiber (via Zadig)

## MSYS2-Pakete

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-pkg-config \
          mingw-w64-ucrt-x86_64-hackrf \
          mingw-w64-ucrt-x86_64-fftw \
          mingw-w64-ucrt-x86_64-portaudio \
          mingw-w64-ucrt-x86_64-libsndfile
```

**Hinweis**: libfdk-aac wird NICHT aus MSYS2 installiert -- der
Opendigitalradio-Fork in `third_party/fdk-aac-dabplus/` wird mitgebaut
(unterstuetzt DAB+-spezifische Granule-Length 960).

## Build

```bash
export PATH="/c/msys64/ucrt64/bin:/c/Program Files/CMake/bin:$PATH"
export PKG_CONFIG_PATH="/c/msys64/ucrt64/lib/pkgconfig"
export CC=gcc CXX=g++

# Konfigurieren
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Bauen
cmake --build build

# Tests
ctest --test-dir build
```

## HackRF-Treiber

Der HackRF braucht den **WinUSB**-Treiber. Pruefen:

```bash
hackrf_info
```

Falls "No HackRF boards found": Zadig (https://zadig.akeo.ie) starten,
HackRF auswaehlen, WinUSB installieren.


# Schnellstart

## Audio-Geraete auflisten

```bash
./build/src/dabtx.exe --list-audio
```

Zeigt alle Eingabegeraete (PortAudio) und WASAPI-Loopback-Endpoints.
Spalte `dir`: `in` = echtes Eingabegeraet, `lb` = WASAPI-Loopback.

## DAB-Kanaele auflisten

```bash
./build/src/dabtx.exe --list-channels
```

47 Kanaele: 41 Band III (5A--13F) + 6 Amateurfunk (ham-70a bis ham-6a).

## HackRF pruefen

```bash
./build/src/dabtx.exe --probe-hackrf
```

## Senden auf Kanal 5A

```bash
./build/src/dabtx.exe --tx 5A 38 120 35
```

- `5A` = Kanal (174.928 MHz)
- `38` = Audio-Device-Index (aus `--list-audio`)
- `120` = Sendedauer in Sekunden
- `35` = TX VGA Gain (0--47 dB)

**WICHTIG**: Vor dem Start Audio abspielen auf dem gewaehlten
Ausgabegeraet! Stereo Mix / WASAPI Loopback capturen nur, wenn
aktiv etwas abgespielt wird.


# Kommandoreferenz

## Informationsbefehle

| Befehl | Beschreibung |
|--------|-------------|
| `--list-audio` | Audio-Eingabegeraete und Loopback-Endpoints |
| `--list-channels` | DAB- und Amateurfunk-Kanaele |
| `--probe-hackrf` | HackRF Board-Info |
| `-h`, `--help` | Hilfe |

## Testbefehle

| Befehl | Beschreibung |
|--------|-------------|
| `--capture-test <dev> <sec>` | Audio-Capture testen (Peak/RMS) |
| `--encode-test <dev> <sec> <out.aac>` | Capture + AAC-Encoding |
| `--eti-test <dev> <sec> <out.eti>` | Volle Pipeline bis ETI-Datei |

## Sendebefehle

| Befehl | Beschreibung |
|--------|-------------|
| `--tx <ch> <dev> <sec> [txvga]` | Live-TX ueber HackRF |
| `--tx-file <ch> <dev> <sec> <out.raw>` | Pipeline in IQ-Datei |
| `--tx-zero <ch> <sec> [txvga]` | Null-Signal senden (HF-Pfad-Test) |

## Optionale Flags

Alle optionalen Flags muessen **vor** dem Hauptbefehl stehen.

| Flag | Beschreibung | Default |
|------|-------------|---------|
| `--ensemble <name>` | Ensemble-Label (max 16 Zeichen) | dabtx Ensemble |
| `--service <name>` | Service-1-Label | dabtx Service |
| `--service2 <name>` | Service-2-Label | dabtx Service 2 |
| `--audio2 <idx>` | Audio-Device fuer Service 2 | (aus, nur 1 Service) |
| `--dls <text>` | DLS-Lauftext Service 1 | dabtx - DAB+ Sender fuer HackRF One |
| `--dls2 <text>` | DLS-Lauftext Service 2 | dabtx - DAB+ Sender fuer HackRF One |
| `--slide <file>` | MOT SlideShow Bild (JPEG/PNG, max 32 KB). MOT-Uebertragung dauert bei 10 KB ca. 43 Sekunden (80 Data Groups). Groessere Bilder dauern laenger. Max 32 KB. | (aus) |
| `--config <file>` | Konfigurationsdatei | dabtx.cfg |


# Konfigurationsdatei

Alle optionalen Flags koennen auch in einer Konfigurationsdatei gesetzt
werden. Standardmaessig wird `dabtx.cfg` im aktuellen Verzeichnis geladen.

## Format

```ini
# Kommentar
ensemble = MYCALL Ensemble
service  = MYCALL INFO
service2 = MYCALL SVC2
audio2   = 38
dls  = MYCALL - QTH Musterstadt - JO31XX
dls2 = MYCALL - Zweiter Service
slide = logo.jpg
```

Regeln:

- Zeilen mit `#` = Kommentar
- Key und Value durch `=` getrennt
- Leerzeichen um `=` werden ignoriert
- CLI-Flags ueberschreiben Config-Werte
- Max 64 Eintraege

## EPG (Programm-Zeitplan)

EPG-Eintraege werden als `epg.1`, `epg.2`, ... definiert:

```ini
epg.1 = 14:00 | 15:00 | QSO Runde 145.500 MHz
epg.2 = 15:00 | 16:30 | Contest CQ auf 432.200 MHz
epg.3 = 16:30 | 17:00 | Fieldday Abbau
```

### Vollstaendiges Beispiel mit EPG

```ini
# dabtx.cfg -- Beispiel mit EPG
ensemble = MYCALL Ensemble
service  = MYCALL INFO
dls      = MYCALL - QTH Musterstadt - JO31XX
slide    = logo.jpg

# Programmzeitplan
epg.1 = 09:00 | 10:00 | Morgenrunde 145.500 MHz
epg.2 = 10:00 | 12:00 | Bastelstunde
epg.3 = 12:00 | 13:00 | Mittagspause
epg.4 = 13:00 | 15:00 | Fieldday Aufbau
epg.5 = 15:00 | 17:00 | Contest CQ auf 432.200 MHz
```

Format pro Zeile: `Startzeit | Endzeit | Titel`

Der aktuelle und naechste Programmpunkt werden als DLS-Lauftext
angezeigt, z.B.:

    JETZT 14:00: QSO Runde 145.500 MHz /// DANACH 15:00: Contest CQ auf 432.200 MHz

Die Rotation zwischen Station-DLS und EPG-DLS erfolgt automatisch
(ca. alle 5 Sekunden).

## SPI (Service and Programme Information)

Zusaetzlich zum DLS-Lauftext wird der EPG-Zeitplan als **SPI** per
ETSI TS 102 818 uebertragen. SPI wird als MOT-Objekt (Content Type
Application, 0x07) via X-PAD ausgeliefert — derselbe Mechanismus wie
MOT SlideShow, aber mit anderem Content-Type und Transport-ID.

### Technische Details

- **FIG 0/13**: User Application Information im FIC
  - UAType = 0x007 (EPG per TS 101 756)
  - SPI Basic Profile (Datenbyte 0x01)
- **SPI-Daten**: XML-Dokument per TS 102 818
  - Schedule mit `<programme>` Elementen
  - Zeitangaben in ISO 8601
  - serviceScope = Ensemble/Service-ID
- **MOT-Transport**: transport_id=2 (SlideShow=1)
  - Content Type = 0x07 (Application), Subtype = 0x00
  - Segmentiert in MSC Data Groups wie SlideShow
- **PAD-Scheduler**: Rotation SlideShow ↔ SPI ↔ DLS
  - Nach jedem SlideShow-Zyklus folgt ein SPI-Zyklus

SPI wird automatisch erzeugt wenn EPG-Eintraege in der Config stehen.
Keine zusaetzlichen Flags noetig.

### Receiver-Kompatibilitaet

Qt-dab erkennt `MOTCTApplication = 0x0700` und leitet die Daten an
den EPG-Compiler weiter. Consumer-Receiver mit EPG-Support sollten
den Zeitplan anzeigen koennen.


# Audio-Quellen

## PortAudio (Standard)

Jedes von PortAudio erkannte Eingabegeraet kann verwendet werden.
Typisch: Stereo Mix (Loopback), Mikrofon, Line-In.

## WASAPI-Loopback

Fuer bit-perfect Capture von jedem Windows-Ausgabegeraet. In
`--list-audio` als `lb` (Loopback) gekennzeichnet.

Beispiel: Realtek Digital Output (SPDIF) direkt capturen:

```bash
./build/src/dabtx.exe --tx 5A 31 300 35
```

WASAPI-Loopback wird automatisch erkannt wenn das Device ein
Output-Endpoint mit WASAPI-Host-API ist.

**Voraussetzung**: Auf dem gewaehlten Ausgabegeraet muss aktiv
Audio abgespielt werden, sonst wird nur Stille aufgenommen.

## Empfohlene Audio-Quelle

WASAPI-Loopback-Endpoints (z.B. "Realtek Digital Output") liefern
**bit-perfect** Capture direkt vom Windows-Audio-Mixer. Das ist die
bevorzugte Methode.

Stereo Mix ueber USB-Audio-Interfaces mit Hardware-Crossmixern kann
EQ- und Crossover-Artefakte einfuehren, die die Audioqualitaet
verschlechtern. Diese Artefakte werden mit in den DAB+-Stream codiert.

**Empfehlung**: Immer WASAPI-Loopback-Endpoints bevorzugen (in
`--list-audio` mit `lb` gekennzeichnet) statt Stereo-Mix-Eingaenge.


# Amateurfunk-Kanaele

| Kanal | Frequenz | Band | Bemerkung |
|-------|----------|------|-----------|
| ham-70a | 434.500 MHz | 70 cm | 1 MHz Experimentalfenster, DAB+ zu breit |
| ham-23a | 1298.000 MHz | 23 cm | DATV-Segment, guter Fit |
| ham-23b | 1299.500 MHz | 23 cm | Zweiter DATV-Slot |
| ham-13a | 2345.000 MHz | 13 cm | DATV |
| ham-13b | 2395.000 MHz | 13 cm | DATV, oberes Ende |
| ham-6a | 5760.000 MHz | 6 cm | DATV, HackRF max ~6 GHz |

**Hinweise**:

- Nur mit gueltigem Amateurfunkzeugnis (Klasse A)
- Minimale Leistung, Dummy-Load oder Attenuator
- 70 cm: DAB+ bei 1.536 MHz Bandbreite passt nicht in das 1 MHz Fenster
  434.0--435.0; nur fuer sehr kurze koordinierte Tests
- 23 cm und 13 cm: beste Wahl fuer DAB+ Experimente
- 6 cm: erfordert gute Koaxkabel/Stecker (SMA-Verluste bei 5.7 GHz)


# Technische Details

## Signalkette

```
PortAudio  -->  fdk-aac  -->  Superframe  -->  MUX  -->  COFDM  -->  HackRF
(48 kHz)    (HE-AACv1)   (RS 120,110)    (FIC+MSC)  (Mode I)    (2.048 MS/s)
```

## Encoder

- HE-AAC v1 (AAC-LC + SBR) bei 72 kbps
- Granule-Length 960 (DAB+-spezifisch)
- 3 Access Units pro 120 ms Superframe
- TT_DABPLUS Transport (fdk-aac-dabplus Fork)

## Schutzcodierung

- Reed-Solomon RS(120,110) pro Spalte der Superframe-Matrix
- EEP-3A (Equal Error Protection, Level 3A)
- 54 Capacity Units pro Service bei 72 kbps

## COFDM-Modulator (Mode I)

- 1536 Traeger, 2048-Punkt IFFT (FFTW)
- DQPSK-Mapping
- Cyclic Prefix: 504 Samples
- Frame: Null-Symbol + Phasenreferenz + 76 OFDM-Symbole (96 ms)
- Sample-Rate: exakt 2.048 MS/s (IQ, complex float)

## HackRF TX

- Sample-Rate: 2.048 MS/s (Minimum des HackRF)
- TX VGA Gain: 0--47 dB (typ. 35 dB mit Attenuator)
- Amp: standardmaessig aus
- IQ-Format: int8 interleaved (im USB-Callback konvertiert)
- Ring-Buffer: 512 ki Samples (~250 ms)
- Pre-Fill gegen Cold-Start-Underrun

## Threading

- **Audio-Thread**: PortAudio-Callback / WASAPI-Event-Thread
- **Encoder-Thread**: PCM --> Superframe (im Hauptthread)
- **HackRF-TX-Callback**: Ring --> int8 IQ (USB-Thread)

Alle Ring-Buffer sind Single-Producer/Single-Consumer, lock-free (atomics).


# Fehlerbehebung

## "No HackRF boards found"

WinUSB-Treiber nicht installiert. Zadig starten, HackRF auswaehlen,
WinUSB installieren.

## Underruns am Anfang

Normal: Pipeline-Warmup (~128 ms) verursacht initiale Underruns.
Steady-State sollte 0 Underruns zeigen. Bei anhaltenden Underruns:
HackRF an dedizierten USB-3.0 Root-Hub stecken.

## Stille im Empfaenger

Audio-Quelle auf dem gewaehlten Ausgabegeraet abspielen!
Stereo Mix / WASAPI Loopback capturen nur aktives Audio.

## Qt-dab zeigt kein Bild (MOT)

- `--slide` Flag pruefen (JPEG/PNG, max 32 KB)
- Qt-dab muss im Classic-Mode laufen (`guiMode=classic`)
- Bei jedem neuen Test-File: Qt-dab komplett neu starten

## Access denied beim HackRF

Ein vorheriger dabtx-Prozess haelt das USB-Device. Alle dabtx-Prozesse
beenden (`taskkill /IM dabtx.exe /F`) oder HackRF physisch abstecken
und wieder einstecken.


# Offline-Debugging mit qt-dab

`--tx-file` erzeugt eine IQ-Datei im osmocom-Format (8-bit unsigned,
2.048 MS/s), die von qt-dab als Raw-File-Input abspielbar ist.

```bash
./build/src/dabtx.exe --tx-file 5A 38 30 out.raw
```

In qt-dab: Device = "rawfiles", Datei auswaehlen, Service anklicken.
Technical-Data zeigt Frame%/RS%/AAC% -- alle sollten 100% sein.

**Wichtig**: Qt-dab im Classic-Mode muss fuer jede neue Datei
komplett neu gestartet werden.


# PDF-Erzeugung

Dieses Manual kann mit pandoc in PDF konvertiert werden:

```bash
pandoc doc/manual.md -o doc/manual.pdf --pdf-engine=xelatex
```

Oder ohne LaTeX (HTML-Zwischenschritt):

```bash
pandoc doc/manual.md -o doc/manual.html --standalone
```


# Referenzen

- ETSI EN 300 401 -- DAB System Layer 1
- ETSI TS 102 563 -- DAB+ (Superframe, RS, HE-AAC v2)
- ETSI TS 101 499 -- MOT SlideShow
- ODR-DabMod: https://github.com/Opendigitalradio/ODR-DabMod
- ODR-DabMux: https://github.com/Opendigitalradio/ODR-DabMux
- HackRF: https://greatscottgadgets.com/hackrf/
