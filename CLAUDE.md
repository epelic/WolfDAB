# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projektziel

DAB+ Sender in C/C++ fuer HackRF One unter Windows 11. Komplette Kette:
PC-Audioquelle → HE-AAC v2 (libfdk-aac) → DAB+ Superframe (RS 204,188) →
ETI-artige Multiplex-Struktur → COFDM-Modulator (FFTW) → 2.048 MS/s IQ →
libhackrf TX. DAB-Kanal (5A–13F in Band III) und Audio-Input waehlbar.

**Keine Verwendung fuer regulaere Ausstrahlung** — das Band ist lizenzpflichtig.
Tests nur mit Dummy-Load, HF-Kaefig oder ausreichender Daempfung.

## Toolchain (Windows 11)

Das Projekt nutzt **MSYS2 UCRT64** als primaeren Toolchain. Alle Dev-Libs sind
dort bereits installiert. **Nicht** MSVC verwenden — libhackrf/fdk-aac/fftw sind
nur im MSYS2-Pfad via pkg-config aufloesbar.

- Compiler: `C:\msys64\ucrt64\bin\gcc.exe` / `g++.exe` (GCC 15.2)
- Build: CMake (`C:\Program Files\CMake\bin\cmake.exe`) + Ninja (MSYS2)
- pkg-config: `C:\msys64\ucrt64\bin\pkg-config.exe`
- HackRF CLI tools: `hackrf_info`, `hackrf_transfer`, `hackrf_sweep` (MSYS2)

### Environment-Setup (bash/MSYS2)

Von bash aus IMMER diese Pfade setzen, damit CMake/pkg-config MSYS2 findet:

```bash
export PATH="/c/msys64/ucrt64/bin:/c/Program Files/CMake/bin:$PATH"
export PKG_CONFIG_PATH="/c/msys64/ucrt64/lib/pkgconfig"
export CC=gcc CXX=g++
```

Die Projekt-CMakeLists.txt setzt diese Defaults ebenfalls, sofern nicht
ueberschrieben.

## Build-Befehle

```bash
# Configure (einmalig / nach CMakeLists-Aenderung)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Ausfuehren
./build/dabtx --list-audio            # PC-Audio-Devices auflisten
./build/dabtx --list-channels         # DAB-Kanaele (5A..13F) auflisten
./build/dabtx --channel 11D --audio 3 # Sendung starten auf 225.648 MHz

# Einzel-Test
./build/tests/test_rs204188
./build/tests/test_ofdm_frame
```

Debug-Build: `-DCMAKE_BUILD_TYPE=Debug`. Sanitizer (ASan/UBSan) ueber
`-DDABTX_SANITIZE=ON`.

## Abhaengigkeiten

Alle via pkg-config aus MSYS2 UCRT64:

| Lib           | Zweck                                            |
|---------------|--------------------------------------------------|
| libhackrf     | USB-Treiber / TX-Callback, 2–20 MS/s IQ          |
| libfdk-aac    | HE-AAC v2 Encoder (AAC-LC + SBR + PS)            |
| libfftw3f     | 2048-Punkt IFFT fuer COFDM (Mode I)              |
| portaudio     | PC-Audio-Input (Enumeration + Capture)           |
| libsndfile    | Optional: WAV-Eingang statt Live-Audio           |
| libfec        | Reed-Solomon(204,188), vendored in `third_party/`|

libfec ist nicht im MSYS2-Repo — wird beim ersten `cmake --build` aus
`third_party/libfec/` mitgebaut (Phil Karn, LGPL).

## HackRF-Voraussetzung

Unter Windows muss der HackRF-USB-Treiber **WinUSB** sein (via Zadig
https://zadig.akeo.ie gesetzt). Check:

```bash
hackrf_info
```

Zeigt Serial + Board ID an, wenn OK. Kein Geraet → Zadig.

## Architektur

Die DAB+ Signalkette ist in klar trennbare Stufen zerlegt; jede Stufe hat eine
eigene Library in `src/` und ist einzeln testbar. Datenfluss ist Pull-basiert
(HackRF-TX-Callback zieht IQ-Samples, Modulator zieht ETI-Frames, MUX zieht
Superframes, Encoder zieht PCM).

```
  PortAudio        fdk-aac         RS+DAB         ETI-MUX         COFDM          HackRF
  (PCM 48k) ──▶ (HE-AACv2) ──▶ (Superframe) ──▶ (Frame 24ms) ──▶ (IQ 2.048MS) ──▶ (USB TX)
  src/audio     src/encoder     src/dabplus     src/mux         src/mod        src/radio
```

### Stufen im Detail

1. **src/audio** — PortAudio-Wrapper. Geraete-Enumeration (`Pa_GetDeviceInfo`),
   Capture in einen Lock-Free-Ring (48 kHz, S16_LE, stereo). Resampling nur bei
   Bedarf.

2. **src/encoder** — libfdk-aac. HE-AAC v2 (AAC-LC + SBR + PS) bei z.B. 64 kbps.
   Output: 5 AAC-Frames = 1 DAB+ Superframe (120 ms, 5 × 960 Samples @ 48 kHz).

3. **src/dabplus** — Superframe-Assembly. Firecode (CRC-16) auf Header, dann
   **Reed-Solomon(120,110)** virtuelles Interleaving ueber 110 Bytes pro Spalte.
   WICHTIG: DAB+ nutzt RS(120,110) NICHT (204,188); letzteres ist DVB. Die libfec-
   API wird mit init_rs_char(8, 0x11d, 0, 1, 10, 135) fuer RS(120,110) verwendet.

4. **src/mux** — Minimal-MUX: eine Audio-Subkanal-Komponente, ein Service, eine
   Ensemble-Beschreibung. Erzeugt FIC (Fast Information Channel, FIBs mit FIG
   0/0, 0/1, 0/2, 0/8, 1/0, 1/1) und MSC (Main Service Channel, CIF = Common
   Interleaved Frame). Ein Logical Frame ist 24 ms.

5. **src/mod** — COFDM-Modulator (DAB **Transmission Mode I**, fuer Band III):
   - Energie-Dispergierung (PRBS x^9+x^5+1)
   - Convolutional Encoder (Mutter-Rate 1/4, generators 133/171/145/133 oktal)
   - Punktierung auf gewaehltes UEP/EEP-Profil (Default: EEP-3A ≈ 1/2)
   - Time Interleaving (MSC: 16 Frames tief)
   - Frequency Interleaving (Pi-Table)
   - DQPSK-Mapping auf 1536 Traeger
   - Null-Symbol + Phasen-Referenz-Symbol + 76 OFDM-Symbole (96 ms Frame)
   - 2048-pt IFFT (FFTW complex float), Cyclic Prefix 504 Samples
   - **Output-Raten-Notiz**: DAB-Rohrate 2.048 MS/s. HackRF minimum ist
     2 MS/s; wir senden direkt auf 2.048 MS/s (gueltig fuer HackRF).

6. **src/radio** — libhackrf-Wrapper. `hackrf_set_sample_rate(2048000)`,
   `hackrf_set_freq(...)` gemaess Kanal, Gains (`txvga` 0–47 dB), Amp off.
   TX-Callback zieht aus dem IQ-Ring und konvertiert `complex float` → `int8`.

### Kanaltabelle (Band III)

Die 41 DAB-Kanaele 5A bis 13F werden in `src/radio/channels.c` als statische
Tabelle gehalten (Label → Mittenfrequenz in Hz). Referenz: ETSI EN 300 401
Annex E. CLI akzeptiert `--channel 11D` usw.

### Threading

- **Audio-Thread** (PortAudio-Callback, Realtime): schreibt in audio-ring.
- **Encoder-Thread**: liest audio-ring, produziert Superframes → superframe-ring.
- **Mod-Thread**: liest superframe-ring, produziert IQ-Frames → iq-ring.
- **HackRF-TX-Callback** (USB-Thread, vom libhackrf aufgerufen): liest iq-ring,
  konvertiert nach int8 I/Q. Ring muss mindestens 200 ms puffern, sonst Underrun.

Alle Rings sind Single-Producer/Single-Consumer, Lock-Free (atomics). Unter-
und Ueberlauf werden geloggt, nicht gedroppt — bei Underrun sendet der TX-
Callback Null-Samples.

## Tests

Tests liegen unter `tests/` und werden mit CTest angelegt:

```bash
cmake --build build && ctest --test-dir build
ctest --test-dir build -R rs204188 -V   # Einzeltest verbose
```

Kern-Tests: RS-Codec-Roundtrip, Firecode-CRC, Conv-Encoder gegen Referenz-
Vektoren, OFDM-Symbol-Generator gegen bekannte Null-Symbol-Signatur, ETI-Frame-
Byte-Exakt gegen ODR-DabMux Referenz-Dumps (falls vorhanden).

## Referenzen

- **ETSI EN 300 401** — DAB System Layer 1 (COFDM, MUX, FIC/MSC)
- **ETSI TS 102 563** — DAB+ (Superframe, RS, HE-AAC v2 Bindung)
- **ETSI TS 101 499** — MOT SlideShow (optional, spaeter)
- **ODR-DabMod** https://github.com/Opendigitalradio/ODR-DabMod — Referenz-
  Modulator (C++, GPL). Beim Implementieren einzelner Stufen zum Quervergleich.
- **ODR-DabMux** https://github.com/Opendigitalradio/ODR-DabMux — Referenz-MUX.
- **libfec** https://github.com/quiet/libfec — Phil Karn, vendored.

## Wichtige Konventionen

- **Sample-Typen**: `complex float` (`_Complex float` / `std::complex<float>`)
  im ganzen Modulator. Erst im HackRF-Callback Konvertierung auf `int8` I/Q.
- **Endianess**: DAB-Bitstreams sind MSB-first. Konsequent
  `bit_writer`/`bit_reader` Helpers in `src/common/bitbuf.h` nutzen, keine
  handgerollten Shifts verstreuen.
- **Sample-Rate**: genau 2.048 MS/s, kein Resampling. Wenn HackRF-Clock-Drift
  stoert, spaeter ueber `hackrf_set_sample_rate_manual` feinjustieren.
- **Logging**: stderr-only, Level via `-v/-vv`. Kein Logging im TX-Callback
  (Realtime-Path).
- **C vs. C++**: DSP-Stufen bevorzugt C (einfache ABI, einfach testbar). CLI
  und Orchestrierung in C++17. Kein C++ im HackRF-/PortAudio-Callback.
