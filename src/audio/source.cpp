#include "source.h"
#include "common/log.h"
#include <windows.h>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>

struct wolfdab_source {
    wolfdab_source_kind_t kind{};
    pa_source_t *device{};
    double phase{}, step{}, amplitude{};
    HANDLE process{}, pipe{}, meta_pipe{};
    uint64_t frames_in{}, frames_dropped{};
    int sample_rate{48000};
    bool network{};
    ULONGLONG next_restart_ms{};
    std::wstring ffmpeg_exe, media;
    std::string meta_buffer, metadata;
};

static std::wstring quote_arg(const wchar_t *arg) {
    std::wstring out=L"\""; unsigned slashes=0;
    for (const wchar_t *p=arg; *p; ++p) {
        if (*p==L'\\') { ++slashes; continue; }
        if (*p==L'\"') { out.append(slashes*2+1,L'\\'); out+=L'\"'; slashes=0; continue; }
        out.append(slashes,L'\\'); slashes=0; out+=*p;
    }
    out.append(slashes*2,L'\\'); out+=L'\"'; return out;
}

wolfdab_source_t *wolfdab_source_open_device(int device_index, size_t ring_samples) {
    return wolfdab_source_open_device_ex(device_index, ring_samples, 48000);
}
wolfdab_source_t *wolfdab_source_open_device_ex(int device_index, size_t ring_samples, int sample_rate) {
    auto *s=new wolfdab_source; s->kind=WOLFDAB_SOURCE_DEVICE;
    s->sample_rate=sample_rate;
    s->device=pa_source_open(device_index,ring_samples); if(!s->device){delete s;return nullptr;} return s;
}

wolfdab_source_t *wolfdab_source_open_tone(double frequency_hz,double level_dbfs) {
    return wolfdab_source_open_tone_ex(frequency_hz, level_dbfs, 48000);
}
wolfdab_source_t *wolfdab_source_open_tone_ex(double frequency_hz,double level_dbfs,int sample_rate) {
    if(frequency_hz<20.0||frequency_hz>20000.0||level_dbfs>0.0||level_dbfs<-90.0)return nullptr;
    auto*s=new wolfdab_source;s->kind=WOLFDAB_SOURCE_TONE;s->sample_rate=sample_rate;s->step=6.2831853071795864769*frequency_hz/sample_rate;s->amplitude=32767.0*std::pow(10.0,level_dbfs/20.0);return s;
}

wolfdab_source_t *wolfdab_source_open_media(const wchar_t *ffmpeg_exe,const wchar_t *media) {
    return wolfdab_source_open_media_ex(ffmpeg_exe, media, 48000);
}
static bool launch_media(wolfdab_source_t *s) {
    if(s->pipe){CloseHandle(s->pipe);s->pipe=nullptr;}if(s->meta_pipe){CloseHandle(s->meta_pipe);s->meta_pipe=nullptr;}if(s->process){WaitForSingleObject(s->process,1000);CloseHandle(s->process);s->process=nullptr;}
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE rd{},wr{},erd{},ewr{};
    if(!CreatePipe(&rd,&wr,&sa,1u<<18))return false;
    SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
    if(!CreatePipe(&erd,&ewr,&sa,1u<<14)){CloseHandle(rd);CloseHandle(wr);return false;}
    SetHandleInformation(erd,HANDLE_FLAG_INHERIT,0);
    std::wstring cmd=quote_arg(s->ffmpeg_exe.c_str())+L" -nostdin -hide_banner -nostats -loglevel verbose ";
    if(s->network) cmd+=L"-icy 1 -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 ";
    else cmd+=L"-stream_loop -1 ";
    cmd+=L"-i "+quote_arg(s->media.c_str())+L" -vn -sn -dn -ac 2 -ar "+std::to_wstring(s->sample_rate)+L" -f s16le -acodec pcm_s16le pipe:1";
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=wr;si.hStdError=ewr;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutable_cmd(cmd.begin(),cmd.end());mutable_cmd.push_back(0);
    BOOL ok=CreateProcessW(s->ffmpeg_exe.c_str(),mutable_cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);CloseHandle(wr);CloseHandle(ewr);
    if(!ok){CloseHandle(rd);CloseHandle(erd);LOGE("FFmpeg CreateProcess failed: %lu",GetLastError());return false;}
    CloseHandle(pi.hThread);s->process=pi.hProcess;s->pipe=rd;s->meta_pipe=erd;return true;
}
wolfdab_source_t *wolfdab_source_open_media_ex(const wchar_t *ffmpeg_exe,const wchar_t *media,int sample_rate) {
    if(!ffmpeg_exe||!media||!*media)return nullptr;
    auto*s=new wolfdab_source;s->kind=WOLFDAB_SOURCE_MEDIA;s->sample_rate=sample_rate;s->ffmpeg_exe=ffmpeg_exe;s->media=media;s->network=wcsncmp(media,L"http://",7)==0||wcsncmp(media,L"https://",8)==0;
    if(!launch_media(s)){delete s;return nullptr;}return s;
}

static void poll_metadata(wolfdab_source_t *s){
    if(!s||!s->meta_pipe)return;DWORD avail=0;
    while(PeekNamedPipe(s->meta_pipe,nullptr,0,nullptr,&avail,nullptr)&&avail){char b[4096];DWORD got=0,want=std::min<DWORD>(avail,sizeof(b));if(!ReadFile(s->meta_pipe,b,want,&got,nullptr)||!got)break;s->meta_buffer.append(b,got);avail=0;}
    size_t nl;while((nl=s->meta_buffer.find_first_of("\r\n"))!=std::string::npos){std::string line=s->meta_buffer.substr(0,nl);size_t eat=nl;while(eat<s->meta_buffer.size()&&(s->meta_buffer[eat]=='\r'||s->meta_buffer[eat]=='\n'))++eat;s->meta_buffer.erase(0,eat);auto k=line.find("StreamTitle");if(k==std::string::npos)continue;auto c=line.find(':',k+11);if(c==std::string::npos)continue;std::string v=line.substr(c+1);while(!v.empty()&&(v.back()==' '||v.back()=='\t'))v.pop_back();size_t a=v.find_first_not_of(" \t");if(a!=std::string::npos){v.erase(0,a);if(!v.empty())s->metadata=v;}}
}

const char *wolfdab_source_metadata(wolfdab_source_t*s){poll_metadata(s);return s&&!s->metadata.empty()?s->metadata.c_str():nullptr;}

/* FFmpeg normally reconnects streams itself.  If a server closes it hard and
 * FFmpeg exits, relaunch it once per second.  This is intentionally called
 * only after the child has exited, so the real-time DAB path never waits for
 * a network timeout or for a still-running decoder. */
static void restart_if_ended(wolfdab_source_t *s) {
    if(!s || !s->process || WaitForSingleObject(s->process,0)!=WAIT_OBJECT_0)return;
    ULONGLONG now=GetTickCount64();
    if(now<s->next_restart_ms)return;
    s->next_restart_ms=now+1000;
    if(launch_media(s)) { s->next_restart_ms=0; LOGI("FFmpeg source restarted"); }
}

size_t wolfdab_source_read(wolfdab_source_t*s,int16_t*dst,size_t samples){
    if(!s||!dst)return 0;
    if(s->kind==WOLFDAB_SOURCE_DEVICE){
        if(s->sample_rate==48000)return pa_source_read(s->device,dst,samples);
        const size_t out_frames=samples/2, in_frames=(out_frames*3+1)/2;
        std::vector<int16_t> in(in_frames*2);size_t got=pa_source_read(s->device,in.data(),in.size());size_t frames=got/2;
        size_t produced=0;for(size_t o=0;o<out_frames;++o){size_t k=(o*3)/2;if(k>=frames)break;dst[2*o]=in[2*k];dst[2*o+1]=in[2*k+1];produced+=2;}return produced;
    }
    if(s->kind==WOLFDAB_SOURCE_TONE){for(size_t i=0;i<samples;i+=2){int16_t v=(int16_t)std::lrint(std::sin(s->phase)*s->amplitude);dst[i]=v;if(i+1<samples)dst[i+1]=v;s->phase+=s->step;if(s->phase>=6.2831853071795864769)s->phase-=6.2831853071795864769;}s->frames_in+=samples/2;return samples;}
    /* Never block the DAB framing thread on an HTTP source.  FFmpeg fills a
     * pipe asynchronously; if it has no PCM right now, the caller inserts
     * digital silence for that audio block and the RF clock keeps running.
     * Blocking ReadFile here was able to empty the HackRF ring when one of a
     * full 864-CU ensemble briefly stalled or reconnected. */
    DWORD available=0;
    if(!PeekNamedPipe(s->pipe,nullptr,0,nullptr,&available,nullptr)) {
        restart_if_ended(s);
        return 0;
    }
    if(!available) {
        restart_if_ended(s);
        return 0;
    }
    DWORD want=(DWORD)std::min<size_t>(samples*sizeof(int16_t),available),got=0;
    if(!ReadFile(s->pipe,dst,want,&got,nullptr)||!got)return 0;
    s->frames_in+=got/(2*sizeof(int16_t));return got/sizeof(int16_t);
}
void wolfdab_source_get_stats(const wolfdab_source_t*s,pa_source_stats_t*out){if(!s||!out)return;if(s->kind==WOLFDAB_SOURCE_DEVICE){pa_source_get_stats(s->device,out);return;}out->frames_in=s->frames_in;out->frames_dropped=s->frames_dropped;out->ring_fill=0;out->ring_capacity=0;}
void wolfdab_source_close(wolfdab_source_t*s){if(!s)return;if(s->device)pa_source_close(s->device);if(s->pipe)CloseHandle(s->pipe);if(s->meta_pipe)CloseHandle(s->meta_pipe);if(s->process){TerminateProcess(s->process,0);WaitForSingleObject(s->process,1000);CloseHandle(s->process);}delete s;}
