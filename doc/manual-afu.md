---
title: "dabtx -- DAB+ auf Amateurfunkbaendern"
subtitle: "Anleitung fuer den Amateurfunk-Betrieb"
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

**dabtx** ermoeglicht DAB+ Aussendungen auf Amateurfunkbaendern mit dem
HackRF One SDR-Transceiver. Dieses Dokument beschreibt den Betrieb
ausschliesslich auf Amateurfunkfrequenzen.

## Rechtliche Voraussetzungen

- **Gueltige Zulassung**: Amateurfunkzeugnis Klasse A (DE) oder
  aequivalente Lizenz
- **Rufzeichenpflicht**: Callsign muss als Ensemble- oder Service-Label
  im DAB+ Signal enthalten sein
- **Leistungsbegrenzung**: Minimale Sendeleistung verwenden. HackRF
  liefert max. ~10 mW an der SMA-Buchse
- **Bandplan beachten**: Nur in den fuer DATV/Experimentalfunk
  vorgesehenen Segmenten senden
- **Kennung**: CW-Kennung oder Rufzeichen als DLS-Lauftext genuegt
  bei digitalen Betriebsarten

## Was ist DAB+?

DAB+ (Digital Audio Broadcasting) nutzt COFDM-Modulation (1536 Traeger)
mit einer Bandbreite von ca. 1.536 MHz. Die Audiocodierung erfolgt
mit HE-AAC v1 (SBR). Die Datenrate betraegt typisch 72 kbps pro
Audio-Service. Zusaetzlich koennen DLS-Lauftext, Bilder (MOT SlideShow)
und Programminformationen (EPG/SPI) uebertragen werden.


# Amateurfunk-Kanaele

| Kanal | Frequenz | Band | Bandbreite | Eignung |
|-------|----------|------|-----------|---------|
| ham-70a | 434.500 MHz | 70 cm | 1.536 MHz | Schlecht -- zu breit fuer 1 MHz Fenster |
| ham-23a | 1298.000 MHz | 23 cm | 1.536 MHz | Gut -- DATV-Segment |
| ham-23b | 1299.500 MHz | 23 cm | 1.536 MHz | Gut -- zweiter DATV-Slot |
| ham-13a | 2345.000 MHz | 13 cm | 1.536 MHz | Gut -- DATV-Segment |
| ham-13b | 2395.000 MHz | 13 cm | 1.536 MHz | Gut -- oberes Ende |
| ham-6a | 5760.000 MHz | 6 cm | 1.536 MHz | Moeglich -- HackRF max ~6 GHz |

## Empfohlene Baender

**23 cm (1240--1300 MHz)** und **13 cm (2320--2450 MHz)** sind die
beste Wahl:

- Ausreichend Bandbreite fuer DAB+ (1.536 MHz)
- DATV-Segmente im Bandplan vorgesehen
- Gute Koaxkabel-Verhaeltnisse (SMA-Verluste noch akzeptabel)
- Genug Stationen mit 23/13 cm Ausstattung fuer QSOs

**70 cm**: DAB+ mit 1.536 MHz Bandbreite passt nicht in das
1 MHz Experimentalfenster (434.0--435.0 MHz). Nur fuer sehr kurze,
koordinierte Tests verwendbar.

**6 cm (5650--5850 MHz)**: Moeglich, aber HackRF-Ausgangsleistung
sinkt bei 5.7 GHz deutlich. SMA-Verbindungen und Koaxkabel muessen
fuer diese Frequenz geeignet sein.


# Schnellstart

## Audio vorbereiten

Audioquelle auf einem Windows-Ausgabegeraet starten (Musik, Sprache,
Testton). WASAPI-Loopback-Endpoints (in `--list-audio` als `lb`
markiert) liefern die beste Qualitaet.

```bash
./build/src/dabtx.exe --list-audio
```

## Senden auf 23 cm

```bash
./build/src/dabtx.exe \
    --ensemble "DL0XYZ DAB+"  \
    --service  "DL0XYZ Info"  \
    --dls "DL0XYZ - QTH Musterstadt - JO31AB - 73!" \
    --tx ham-23a 31 300 35
```

- `ham-23a` = 1298.000 MHz
- `31` = Audio-Device-Index (WASAPI Loopback)
- `300` = 5 Minuten Sendedauer
- `35` = TX VGA Gain (mit Dummy-Load/Attenuator)

## Senden auf 13 cm

```bash
./build/src/dabtx.exe --config afu.cfg --tx ham-13a 31 300 35
```


# Konfigurationsdatei

Fuer regelmaessigen Betrieb empfiehlt sich eine Konfigurationsdatei.

## Beispiel: Fieldday-Konfiguration

```ini
# afu.cfg -- Fieldday DAB+ auf 13 cm
ensemble = DL0XYZ FIELDDAY
service  = DL0XYZ 13cm
dls      = DL0XYZ de DL1ABC - Fieldday Musterstadt JO31AB - 73!
slide    = fieldday_logo.jpg

# Programm-Zeitplan
epg.1 = 09:00 | 10:00 | Aufbau und Antennentest
epg.2 = 10:00 | 12:00 | Contest-Betrieb CW/SSB
epg.3 = 12:00 | 13:00 | Mittagspause - QSO-Runde
epg.4 = 13:00 | 16:00 | Contest-Betrieb Digital
epg.5 = 16:00 | 17:00 | Abbau und Ergebnisse
```

Starten:

```bash
./build/src/dabtx.exe --config afu.cfg --tx ham-13a 31 3600 35
```

## Beispiel: Ortsverband-Relais

```ini
# ov.cfg -- OV-Abend Streaming
ensemble = DL0XYZ OV-DAB+
service  = DL0XYZ OV
dls      = DL0XYZ OV N99 - Monatsversammlung - Gaeste willkommen!
slide    = ov_logo.jpg

epg.1 = 19:00 | 19:30 | Begruessung und Aktuelles
epg.2 = 19:30 | 20:30 | Vortrag: SDR-Technik
epg.3 = 20:30 | 21:00 | Diskussion und Ausklang
```


# Empfang

## Consumer-Receiver

Handelsueblische DAB+ Radios empfangen nur Band III (174--240 MHz).
Fuer Amateurfunkbaender sind sie **nicht geeignet**.

## SDR-Empfaenger

Mit einem zweiten HackRF, RTL-SDR (bis 1.7 GHz) oder anderem
SDR-Empfaenger und Software wie **qt-dab** oder **welle.io** kann
das Signal empfangen werden:

1. SDR auf die gleiche Frequenz abstimmen (z.B. 1298.000 MHz)
2. IQ-Daten mit 2.048 MS/s aufnehmen
3. In qt-dab als Raw-File abspielen

**Hinweis**: qt-dab unterstuetzt standardmaessig nur Band III Kanaele.
Die Amateurfunk-Frequenzen muessen im Quellcode ergaenzt werden
(siehe dabtx Projekt-README fuer Patches).

## Offline-Test

Ohne HF-Pfad testen:

```bash
./build/src/dabtx.exe --config afu.cfg --tx-file ham-13a 31 60 test.raw
```

Die erzeugte IQ-Datei kann in qt-dab abgespielt werden.


# Antennen und Leistung

## HackRF-Ausgangsleistung

| txvga | Leistung (approx.) | Empfehlung |
|-------|-------------------|------------|
| 20 dB | ~0.01 mW | Labor/Nahbereich |
| 30 dB | ~0.1 mW | Dummy-Load Test |
| 35 dB | ~0.5--1 mW | Mit Attenuator |
| 47 dB | ~10 mW | Maximum, nur mit Daempfung |

Die tatsaechliche Leistung haengt stark von der Frequenz ab und
nimmt oberhalb von 2 GHz deutlich ab.

## Antennentipps

- **23 cm**: Yagi, Patch-Antenne oder Hornstrahler. SMA-Kabel
  kurz halten (<1 m RG-316 oder besser Ecoflex/Aircell)
- **13 cm**: Parabolspiegel mit Feed, Patch-Array. Kabelversluste
  werden signifikant — moeglichst kurze Zuleitung
- **Dummy-Load**: Fuer erste Tests immer mit 50-Ohm-Abschluss
  oder 30 dB+ Attenuator arbeiten


# Fieldday / Contest Setup

## Checkliste

1. Antenne aufbauen und SWR pruefen (Netzwerkanalysator oder SWR-Meter)
2. HackRF per USB-3 anschliessen (dedizierter Root-Hub empfohlen)
3. Audio-Quelle vorbereiten (Laptop mit Musik/Moderation)
4. `afu.cfg` mit aktuellem Rufzeichen, QTH und EPG-Zeitplan anpassen
5. Kurztest mit `--tx-file` und qt-dab: Frame/RS/AAC = 100%?
6. Live-TX starten: `./build/src/dabtx.exe --config afu.cfg --tx ham-23a 31 7200 35`
7. Empfangsstationen informieren: Frequenz, Kanal, wie empfangen (SDR + qt-dab)

## Stromversorgung

HackRF braucht nur USB-Strom (~500 mA). Der Laptop ist der
Hauptverbraucher. Fuer laengere Fieldday-Sessions: Powerbank oder
Generator einplanen.

## Moderation

Mit DLS-Lauftext kann der aktuelle Zustand kommuniziert werden.
Per Config-Datei oder CLI-Flag `--dls` setzen:

```bash
./build/src/dabtx.exe --config afu.cfg \
    --dls "DL0XYZ Fieldday - aktuell auf 432.200 SSB QRV - 73!" \
    --tx ham-23a 31 3600 35
```


# Technische Eckdaten

| Parameter | Wert |
|-----------|------|
| Modulation | COFDM Mode I, DQPSK, 1536 Traeger |
| Bandbreite | ~1.536 MHz |
| Sample-Rate | 2.048 MS/s (IQ) |
| Audio-Codec | HE-AAC v1 (SBR), 72 kbps |
| Schutz | RS(120,110), EEP-3A |
| Frame | 96 ms (Null + Ref + 76 OFDM-Symbole) |
| Superframe | 120 ms (5 Frames, 3 AUs bei HE-AAC v1) |
| Max. Bild | 32 KB (JPEG/PNG), ~43 s Uebertragungszeit bei 10 KB |


# Referenzen

- ETSI EN 300 401 -- DAB System
- ETSI TS 102 563 -- DAB+ Audio
- IARU Region 1 Bandplan
- DARC Bandplan: https://www.darc.de/der-club/referate/vus/bandplaene/
- HackRF: https://greatscottgadgets.com/hackrf/
