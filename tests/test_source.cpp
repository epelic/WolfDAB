#include "audio/source.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char **argv) {
    wolfdab_source_t *s = wolfdab_source_open_tone(1000.0, -12.0);
    if (!s) return 1;
    std::vector<int16_t> pcm(9600);
    if (wolfdab_source_read(s, pcm.data(), pcm.size()) != pcm.size()) return 2;
    double sum = 0; int crossings = 0;
    for (size_t i=0; i<pcm.size(); i+=2) {
        sum += (double)pcm[i] * pcm[i];
        if (i >= 2 && pcm[i-2] <= 0 && pcm[i] > 0) crossings++;
    }
    wolfdab_source_close(s);
    double rms = std::sqrt(sum / (pcm.size()/2));
    if (rms < 5700 || rms > 5900 || crossings < 99 || crossings > 101) return 3;
    if (argc == 3) {
        wchar_t ffmpeg[1024], media[1024];
        MultiByteToWideChar(CP_UTF8,0,argv[1],-1,ffmpeg,1024);
        MultiByteToWideChar(CP_UTF8,0,argv[2],-1,media,1024);
        s=wolfdab_source_open_media(ffmpeg,media); if(!s)return 4;
        size_t got=wolfdab_source_read(s,pcm.data(),pcm.size()); wolfdab_source_close(s);
        if(got==0)return 5;
    }
    return 0;
}
