# Third-Party Code and References

## Vendored Code (im Repository enthalten)

### libfec — Reed-Solomon Codec
- **Autor**: Phil Karn, KA9Q
- **Quelle**: https://github.com/quiet/libfec
- **Lizenz**: LGPL-2.1-or-later
- **Verwendete Dateien**: `init_rs_char.c`, `encode_rs_char.c`,
  `decode_rs_char.c`, `fec.h`, `char.h`, `init_rs.h`
- **Zweck**: RS(120,110) Codec fuer DAB+ Superframe-Fehlerkorrektur
- **Anmerkung**: Nur die char-Variante des RS-Codecs wird verwendet.
  Das vollstaendige libfec-Buildsystem ist nicht eingebunden.

## Separat gebaute Abhaengigkeit

### fdk-aac-dabplus — HE-AAC Encoder (Opendigitalradio Fork)
- **Originalprojekt**: Fraunhofer FDK AAC Codec Library for Android
- **Fork**: https://github.com/Opendigitalradio/fdk-aac
- **Lizenz**: Fraunhofer FDK AAC Codec Library License
  (basierend auf Android Open Source Project)
- **Zweck**: HE-AAC v1 Encoder mit DAB+-spezifischer Granule-Length 960
  und TT_DABPLUS Transport
- **Anmerkung**: Das MSYS2-Systempaket `fdk-aac` unterstuetzt
  Granule-Length 960 NICHT. Daher muss der Opendigitalradio-Fork
  separat gebaut werden. Siehe Build-Anleitung im README.

## System-Bibliotheken (via MSYS2 UCRT64)

| Bibliothek | Lizenz | Zweck |
|---|---|---|
| libhackrf | GPL-2.0-or-later | HackRF USB-Treiber, TX-Callback |
| FFTW3 (float) | GPL-2.0-or-later | 2048-Punkt IFFT fuer COFDM |
| PortAudio v19 | MIT | Audio-Capture (Enumeration + Input) |
| libsndfile | LGPL-2.1 | Optional: WAV-Eingang statt Live-Audio |

## Referenz-Implementierungen (nicht im Repository)

Die folgenden Open-Source-Projekte wurden als Referenz fuer die
Implementierung der DAB+ Signalverarbeitung herangezogen. Es wurde
kein Quellcode kopiert, aber Algorithmen, Byte-Layouts und
Tabellenstrukturen basieren auf dem Studium dieser Quellen.

### ODR-DabMux — DAB Multiplexer
- **Quelle**: https://github.com/Opendigitalradio/ODR-DabMux
- **Lizenz**: GPL-2.0-or-later
- **Referenziert fuer**: FIG-Byte-Layouts (0/0, 0/1, 0/2, 0/8, 1/0, 1/1),
  ETI-Frame-Struktur, Subchannel-Konfiguration (EEP-A/B Formeln,
  Protection-Level Mapping), Service-Component-Descriptors

### ODR-DabMod — DAB Modulator
- **Quelle**: https://github.com/Opendigitalradio/ODR-DabMod
- **Lizenz**: GPL-2.0-or-later
- **Referenziert fuer**: Convolutional Encoder (Polynome
  133/171/145/133 oktal), Puncturing-Pattern (P1..P24),
  Frequency-Interleaver Pi-Table, OFDM-Symbol-Generierung,
  Phasenreferenz-Symbol

## ETSI-Standards

Die Implementierung basiert auf folgenden ETSI-Normen:

- **EN 300 401** — DAB System (COFDM, MUX, FIC/MSC)
- **TS 102 563** — DAB+ Audio (Superframe, Reed-Solomon, HE-AAC)
- **TS 101 499** — MOT SlideShow
- **TS 102 818** — Service and Programme Information (SPI/EPG)
- **ETS 300 799** — ETI Frame-Format

Keine ETSI-Quellcode-Uebernahme; Implementierung basiert auf den
Spezifikationstexten.
