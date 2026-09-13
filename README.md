<img width="512" height="512" alt="Immagine Codex 10 set 2026, 15_42_13" src="https://github.com/user-attachments/assets/90032916-957e-4145-a93d-257b77f727cc" />
# WolfDAB

## Italiano

WolfDAB è un trasmettitore multiplex DAB/DAB+ multi-servizio per Windows 10/11 x64 e HackRF One. Include una GUI nativa, fino a 64 servizi entro 864 CU, FIC/MSC, modulazione COFDM Mode I a 2,048 MS/s e uscita diretta libhackrf.

- Audio locale/WASAPI, tono e stream HTTP/HTTPS tramite FFmpeg
- DAB+ AAC e DAB classico MP2 per singolo servizio, anche combinati nello stesso ensemble
- HE-AAC v1/v2, bitrate, sampling ed EEP configurabili con calcolo CU live
- Service ID, etichetta lunga, etichetta breve DAB e SubCh
- DLS manuale e titoli ICY automatici; cartella MOT Slideshow per ogni servizio con rotazione temporizzata e aggiornamenti live
- Immagini MOT: PNG oppure JPEG JFIF baseline (non JPEG Exif/progressive), massimo 32 KB per file
- Tutti i blocchi DAB Band III, gain e amplificatore HackRF
- Configurazioni `.wolfdab`; registrazione gratuita obbligatoria legata alla macchina
- Richiesta codice integrata: nome, email, città/Paese e identificativo pseudonimo vengono inviati a Freewaves.it; nessun dato di pagamento
- Registrazione e setup in italiano, inglese, tedesco e francese

Scaricare `WolfDAB-Trial-Setup-x64.exe` da Releases. La build Trial non ha scadenza, ma non si avvia finché non viene inserito il codice gratuito richiesto dal modulo interno. Usare HackRF con driver WinUSB e un carico fittizio o impianto autorizzato. Ogni trasmissione RF deve rispettare le norme applicabili.

## English

WolfDAB is a multi-service DAB/DAB+ multiplex transmitter for Windows 10/11 x64 and HackRF One. It includes a native GUI, up to 64 services within 864 CU, FIC/MSC generation, Mode I COFDM modulation at 2.048 MS/s, and direct libhackrf output.

- Local/WASAPI audio, test tone, and HTTP/HTTPS streams through FFmpeg
- Per-service DAB+ AAC and classic DAB MP2, including mixed ensembles
- Configurable HE-AAC v1/v2, bitrate, sampling, and EEP with live CU calculation
- Service ID, long label, DAB short label, and SubCh
- Manual DLS and automatic ICY titles; per-service MOT Slideshow folders with timed rotation and live updates
- MOT images: PNG or baseline JFIF JPEG (not Exif/progressive JPEG), maximum 32 KB per file
- All DAB Band III blocks, HackRF gain, and amplifier control
- `.wolfdab` configurations; mandatory free machine-bound registration
- Built-in code request: name, email, city/country and a pseudonymous machine ID are sent to Freewaves.it; no payment data
- Registration and installer in Italian, English, German, and French

Download `WolfDAB-Trial-Setup-x64.exe` from Releases. The Trial build does not expire, but it will not start until the free code requested through the built-in form is entered. Use HackRF with the WinUSB driver and a dummy load or authorized RF system. RF transmissions must comply with applicable regulations.

## Build / Compilazione

Requires MSYS2 UCRT64, CMake, Ninja, GCC, libhackrf, FFTW3f, and PortAudio. The DAB+ FDK-AAC fork is included as a submodule.

```bash
git clone --recursive <repository-url>
./setup.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

See `THIRD_PARTY.md` for dependency licenses. WolfDAB is GPL-3.0-or-later; see `LICENSE`.
