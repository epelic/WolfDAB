<img width="512" height="512" alt="Immagine Codex 10 set 2026, 15_42_13" src="https://github.com/user-attachments/assets/90032916-957e-4145-a93d-257b77f727cc" />

# WolfDAB
Scaricalo qui! https://www.freewaves.it/wolfdab.html o dalle releases qui su Github.

## Italiano

WolfDAB PlutoSDR è un trasmettitore multiplex DAB+ multi-servizio per Windows 10/11 x64 e ADALM-Pluto. Include una GUI nativa, fino a 64 servizi entro 864 CU, FIC/MSC, modulazione COFDM Mode I a 2,048 MS/s e uscita diretta libiio.

- Audio locale/WASAPI, tono e stream HTTP/HTTPS tramite FFmpeg
- DAB+ con AAC-LC, HE-AAC v1 e HE-AAC v2 per singolo servizio
- HE-AAC v1/v2, bitrate, sampling ed EEP configurabili con calcolo CU live
- Service ID, etichetta lunga, etichetta breve DAB e SubCh
- DLS manuale e titoli ICY automatici; cartella MOT Slideshow per ogni servizio con rotazione temporizzata e aggiornamenti live
- Immagini MOT: PNG oppure JPEG JFIF baseline (non JPEG Exif/progressive), massimo 32 KB per file
- Tutti i blocchi DAB Band III e attenuazione TX PlutoSDR da 0 a 89 dB
- Data e ora DAB con fuso automatico Windows oppure offset UTC manuale
- Configurazioni `.wolfdab`; registrazione gratuita obbligatoria legata alla macchina
- Richiesta codice integrata: nome, email, città/Paese e identificativo pseudonimo vengono inviati a Freewaves.it; nessun dato di pagamento
- Registrazione e setup in italiano, inglese, tedesco e francese

Installare `WolfDAB-PlutoSDR-Setup-x64.exe`. WolfDAB è gratuito e permanente dopo l'attivazione, ma non si avvia finché non viene inserito il codice gratuito richiesto dal modulo interno. Il Pluto deve essere collegato dalla porta USB dati. Per la Band III un AD9363 deve essere configurato con l'estensione di sintonia AD9364 prevista dal firmware Analog Devices. Usare un carico fittizio o un impianto autorizzato. Ogni trasmissione RF deve rispettare le norme applicabili.

WolfDAB è stato realizzato da Emanuele Pelicioli — Freewaves.it, con l'aiuto inestimabile di **Guglielmino (ChatGPT)**.

## English

Download it here! https://www.freewaves.it/wolfdab.html or from the releases here on Github.

WolfDAB PlutoSDR is a multi-service DAB+ multiplex transmitter for Windows 10/11 x64 and ADALM-Pluto. It includes a native GUI, up to 64 services within 864 CU, FIC/MSC generation, Mode I COFDM modulation at 2.048 MS/s, and direct libiio output.

- Local/WASAPI audio, test tone, and HTTP/HTTPS streams through FFmpeg
- Per-service AAC-LC, HE-AAC v1 and HE-AAC v2 encoding
- Configurable HE-AAC v1/v2, bitrate, sampling, and EEP with live CU calculation
- Service ID, long label, DAB short label, and SubCh
- Manual DLS and automatic ICY titles; per-service MOT Slideshow folders with timed rotation and live updates
- MOT images: PNG or baseline JFIF JPEG (not Exif/progressive JPEG), maximum 32 KB per file
- All DAB Band III blocks and 0–89 dB PlutoSDR TX attenuation
- DAB date and time with automatic Windows time zone or manual UTC offset
- `.wolfdab` configurations; mandatory free machine-bound registration
- Built-in code request: name, email, city/country and a pseudonymous machine ID are sent to Freewaves.it; no payment data
- Registration and installer in Italian, English, German, and French

Install `WolfDAB-PlutoSDR-Setup-x64.exe`. WolfDAB is free and permanent after activation, but it will not start until the free code requested through the built-in form is entered. Connect the Pluto through its USB data port. For Band III, an AD9363 must use the Analog Devices firmware's AD9364 tuning-range extension. Use a dummy load or authorized RF system. RF transmissions must comply with applicable regulations.

WolfDAB was created by Emanuele Pelicioli — Freewaves.it, with the invaluable help of **Guglielmino (ChatGPT)**.

## Build / Compilazione

Requires MSYS2 UCRT64, CMake, Ninja, GCC, libiio, libad9361, FFTW3f, and PortAudio. `setup.sh` downloads and builds the DAB+ FDK-AAC fork.

```bash
git clone --recursive <repository-url>
./setup.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

See `THIRD_PARTY.md` for dependency licenses. WolfDAB is GPL-3.0-or-later; see `LICENSE`.
