#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <algorithm>
#include <vector>
#include <sstream>
#include <cstring>
#include <fstream>
#include "audio/pa_source.h"
#include "mux/eep_profile.h"

namespace {
constexpr const wchar_t* APP_VERSION=L"1.0.9";
constexpr int ID_LANG=100, ID_ADD=101, ID_REMOVE=102, ID_START=103, ID_STOP=104, ID_BROWSE=105, ID_APPLY_FORMAT=106;
constexpr int ID_ENSEMBLE=110, ID_EID=111, ID_ECC=112, ID_CHANNEL=113, ID_GAIN=114, ID_AMP=115;
constexpr int ID_SERVICE=120, ID_SID=121, ID_SOURCE=122, ID_BITRATE=123, ID_EEP=124, ID_DLS=125, ID_CODEC=126, ID_SAMPLING=127;
constexpr int ID_SOURCE_VALUE=128, ID_SCIDS=129, ID_SUBCH=130;
constexpr int ID_MOT_FOLDER=131, ID_MOT_INTERVAL=132, ID_MOT_BROWSE=133;
constexpr int IDM_NEW=200, IDM_OPEN=201, IDM_SAVE=202, IDM_SAVE_AS=203, IDM_EXIT=204, IDM_ABOUT=205;
constexpr int ID_ACCENT=900;

struct Service { std::wstring label=L"Nuovo servizio"; std::wstring shortLabel=L"Nuovo"; std::wstring sid=L"0xE001"; unsigned scids=0; unsigned subch=1; std::wstring source=L""; unsigned bitrate=72; unsigned codec=1; unsigned sampling=48000; dab_eep_profile_t eep=DAB_EEP_3A; std::wstring dls=L"";std::wstring motFolder=L"";unsigned motInterval=10;
    Service()=default;
    Service(std::wstring l,std::wstring id,std::wstring src,unsigned br,dab_eep_profile_t ep,std::wstring dl):label(std::move(l)),sid(std::move(id)),subch(sid==L"0xE002"?2:1),source(std::move(src)),bitrate(br),eep(ep),dls(std::move(dl)){}
    Service(std::wstring l,std::wstring id,std::wstring src,unsigned br,unsigned co,unsigned sr,dab_eep_profile_t ep,std::wstring dl):label(std::move(l)),sid(std::move(id)),subch(sid==L"0xE002"?2:1),source(std::move(src)),bitrate(br),codec(co),sampling(sr),eep(ep),dls(std::move(dl)){if(sid==L"0xE002"){static unsigned next=2;subch=next;scids=(next-1)%16;wchar_t b[16];swprintf(b,16,L"0x%04X",0xE000+next);sid=b;++next;if(next>16)next=2;}}
};
HWND wnd, list, statusbar, editService, editSid, editScids, editSubch, comboSource, editSource, editRate, comboCodec, comboSampling, comboEep, editDls, editMotFolder, editMotInterval, motFolderLabel, motIntervalLabel, editEnsemble, editEid, editEcc, comboChannel, editGain, ampCheck, applyFormatButton;
HINSTANCE instance;
HFONT uiFont, titleFont;
HBRUSH windowBrush, accentBrush;
HANDLE txProcess, txStopEvent;
std::vector<Service> services{{L"Radio 1", L"0xE001", L"34", 72, 1, 48000, DAB_EEP_3A, L"WolfDAB"}};
bool italian=true;
int selectedService=0;
std::wstring configPath;
std::wstring startupConfig;
const wchar_t* tr(const wchar_t* it, const wchar_t* en) { return italian ? it : en; }

void rememberLastConfig(const std::wstring& path){if(path.empty())return;HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\WolfDAB",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){RegSetValueExW(key,L"LastConfig",0,REG_SZ,(const BYTE*)path.c_str(),(DWORD)((path.size()+1)*sizeof(wchar_t)));RegCloseKey(key);}}
std::wstring readLastConfig(){wchar_t path[32768]{};DWORD bytes=sizeof(path);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\WolfDAB",L"LastConfig",RRF_RT_REG_SZ,nullptr,path,&bytes)!=ERROR_SUCCESS||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return {};return path;}

void setText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }
std::wstring text(HWND h) { int n=GetWindowTextLengthW(h); std::wstring s((size_t)n+1,L'\0'); GetWindowTextW(h,s.data(),n+1); s.resize((size_t)n); return s; }
unsigned capacity() { unsigned total=0; for (auto& s:services) total += dab_eep_cu_for_bitrate(s.bitrate,s.eep); return total; }
bool txRunning(){return txProcess&&WaitForSingleObject(txProcess,0)==WAIT_TIMEOUT;}
void status() { wchar_t b[200]; unsigned c=capacity(); swprintf(b,200,txRunning()?tr(L"TX ATTIVO   •   MUX: %u / 864 CU   •   %.1f%%",L"TX RUNNING   •   MUX: %u / 864 CU   •   %.1f%%"):tr(L"Pronto   •   MUX: %u / 864 CU   •   %.1f%%",L"Ready   •   MUX: %u / 864 CU   •   %.1f%%"),c,c*100.0/864.0); SendMessageW(statusbar,SB_SETTEXT,0,(LPARAM)b); }
void fillProfiles() { SendMessageW(comboEep,CB_RESETCONTENT,0,0); for(int p=0;p<8;++p) SendMessageA(comboEep,CB_ADDSTRING,0,(LPARAM)dab_eep_profile_name((dab_eep_profile_t)p)); }
void populateList() {
    ListView_DeleteAllItems(list);
    const int compactWidths[7]={136,78,70,96,68,78,42};
    for(int c=0;c<7;++c) ListView_SetColumnWidth(list,c,compactWidths[c]);
    for (size_t i=0;i<services.size();++i) { auto& s=services[i]; wchar_t cu[32], rate[32];
        swprintf(cu,32,L"%u",dab_eep_cu_for_bitrate(s.bitrate,s.eep)); swprintf(rate,32,L"%u kbps",s.bitrate);
        LVITEMW x{}; x.mask=LVIF_TEXT; x.iItem=(int)i; x.pszText=s.label.data(); ListView_InsertItem(list,&x);
        const wchar_t* codec=s.codec==0?L"AAC-LC":s.codec==2?L"HE-AAC v2":L"HE-AAC v1";wchar_t sampling[32];swprintf(sampling,32,L"%u kHz",s.sampling/1000);
        ListView_SetItemText(list,(int)i,1,s.sid.data()); ListView_SetItemText(list,(int)i,2,rate);ListView_SetItemText(list,(int)i,3,(LPWSTR)codec);ListView_SetItemText(list,(int)i,4,sampling);
        std::wstring p; for(int k=0;k<8;k++) if(s.eep==(dab_eep_profile_t)k) { const char* a=dab_eep_profile_name((dab_eep_profile_t)k); p.assign(a,a+strlen(a)); }
        ListView_SetItemText(list,(int)i,5,p.data()); ListView_SetItemText(list,(int)i,6,cu);
    } status();
}
int sel() { return ListView_GetNextItem(list,-1,LVNI_SELECTED); }
void createLabel(const wchar_t* s,int x,int y,int w);
HWND edit(int id,int x,int y,int w);
void saveSelected(bool refresh);
void loadSelected();
void ensureBulkControl(){if(!applyFormatButton)applyFormatButton=CreateWindowW(L"BUTTON",tr(L"Applica formato a tutti",L"Apply format to all"),WS_CHILD|WS_VISIBLE,240,334,230,32,wnd,(HMENU)ID_APPLY_FORMAT,0,0);}
void applyFormatToAll(){saveSelected(false);if(selectedService<0||(size_t)selectedService>=services.size())return;const auto& source=services[(size_t)selectedService];for(auto& s:services){s.bitrate=source.bitrate;s.codec=source.codec;s.sampling=source.sampling;s.eep=source.eep;}populateList();ListView_SetItemState(list,selectedService,LVIS_SELECTED,LVIS_SELECTED);loadSelected();}
void ensureMotControls(){if(editMotFolder)return;motFolderLabel=CreateWindowW(L"STATIC",tr(L"Cartella immagini MOT",L"MOT image folder"),WS_CHILD|WS_VISIBLE,666,450,250,20,wnd,0,0,0);motIntervalLabel=CreateWindowW(L"STATIC",tr(L"Intervallo",L"Interval"),WS_CHILD|WS_VISIBLE,1030,450,72,20,wnd,0,0,0);editMotFolder=edit(ID_MOT_FOLDER,666,472,310);CreateWindowW(L"BUTTON",L"…",WS_CHILD|WS_VISIBLE,984,472,37,24,wnd,(HMENU)ID_MOT_BROWSE,0,0);editMotInterval=edit(ID_MOT_INTERVAL,1030,472,48);createLabel(L"s",1082,475,20);SetWindowPos(ampCheck,nullptr,666,508,280,26,SWP_NOZORDER);SetWindowPos(GetDlgItem(wnd,ID_START),nullptr,666,540,160,42,SWP_NOZORDER);SetWindowPos(GetDlgItem(wnd,ID_STOP),nullptr,840,540,160,42,SWP_NOZORDER);SendMessageW(editMotFolder,WM_SETFONT,(WPARAM)uiFont,TRUE);SendMessageW(editMotInterval,WM_SETFONT,(WPARAM)uiFont,TRUE);SendMessageW(motFolderLabel,WM_SETFONT,(WPARAM)uiFont,TRUE);SendMessageW(motIntervalLabel,WM_SETFONT,(WPARAM)uiFont,TRUE);}
void browseMotFolder(){BROWSEINFOW bi{};bi.hwndOwner=wnd;bi.lpszTitle=tr(L"Seleziona la cartella delle immagini MOT",L"Select the MOT image folder");bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);if(pid){wchar_t path[MAX_PATH]{};if(SHGetPathFromIDListW(pid,path))setText(editMotFolder,path);CoTaskMemFree(pid);}}
void ensureIdentityControls(){if(editScids)return;SendMessageW(comboChannel,CB_RESETCONTENT,0,0);const wchar_t*blocks[]={L"5A",L"5B",L"5C",L"5D",L"6A",L"6B",L"6C",L"6D",L"7A",L"7B",L"7C",L"7D",L"8A",L"8B",L"8C",L"8D",L"9A",L"9B",L"9C",L"9D",L"10A",L"10N",L"10B",L"10C",L"10D",L"11A",L"11N",L"11B",L"11C",L"11D",L"12A",L"12N",L"12B",L"12C",L"12D",L"13A",L"13B",L"13C",L"13D",L"13E",L"13F",L"LA",L"LB",L"LC",L"LD",L"LE",L"LF",L"LG",L"LH",L"LI",L"LJ",L"LK",L"LL",L"LM",L"LN",L"LO",L"LP",L"LQ",L"LR",L"LS",L"LT",L"LU",L"LV",L"LW"};for(auto*s:blocks)SendMessageW(comboChannel,CB_ADDSTRING,0,(LPARAM)s);SendMessageW(comboChannel,CB_SETCURSEL,0,0);SetWindowLongPtrW(comboChannel,GWL_STYLE,GetWindowLongPtrW(comboChannel,GWL_STYLE)|WS_VSCROLL|CBS_NOINTEGRALHEIGHT);SendMessageW(comboChannel,CB_SETDROPPEDWIDTH,90,0);SendMessageW(comboChannel,CB_SETMINVISIBLE,10,0);SetWindowPos(comboChannel,nullptr,460,532,90,240,SWP_NOZORDER|SWP_FRAMECHANGED);COMBOBOXINFO ci{sizeof(ci)};if(GetComboBoxInfo(comboChannel,&ci)&&ci.hwndList){SetWindowLongPtrW(ci.hwndList,GWL_STYLE,GetWindowLongPtrW(ci.hwndList,GWL_STYLE)|WS_VSCROLL);ShowScrollBar(ci.hwndList,SB_VERT,TRUE);}SetWindowTextW(ampCheck,tr(L"Amplificatore ON",L"Amplifier ON"));SetWindowPos(editSid,nullptr,666,194,110,24,SWP_NOZORDER);createLabel(tr(L"ID breve",L"Short ID"),790,172,110);editScids=edit(129,790,194,110);createLabel(L"SubCh",914,172,110);editSubch=edit(130,914,194,110);SendMessageW(editScids,WM_SETFONT,(WPARAM)uiFont,TRUE);SendMessageW(editSubch,WM_SETFONT,(WPARAM)uiFont,TRUE);}
void addClonedService(){saveSelected(true);if(services.size()>=64){MessageBoxW(wnd,tr(L"Sono disponibili 64 SubCh (0–63).",L"64 SubCh values are available (0–63)."),L"WolfDAB",MB_ICONINFORMATION);return;}if(services.empty()){services.emplace_back();}else{Service s=services.back();unsigned long sid=std::wcstoul(s.sid.c_str(),nullptr,0);sid=(sid+1)&0xffff;wchar_t b[16];swprintf(b,16,L"0x%04lX",sid);s.sid=b;s.subch=(s.subch+1)&63;s.scids=(s.scids+1)&15;services.push_back(std::move(s));}populateList();int i=(int)services.size()-1;selectedService=i;ListView_SetItemState(list,i,LVIS_SELECTED,LVIS_SELECTED);loadSelected();}
void loadSelected() {
    ensureIdentityControls();ensureBulkControl();ensureMotControls(); int i=selectedService;
    SendMessageW(editScids,EM_SETLIMITTEXT,8,0);
    if(i<0 || (size_t)i>=services.size())return; auto&s=services[i];
    setText(editService,s.label);setText(editSid,s.sid);setText(editScids,s.shortLabel);setText(editSubch,std::to_wstring(s.subch));setText(editRate,std::to_wstring(s.bitrate));setText(editDls,s.dls);setText(editMotFolder,s.motFolder);setText(editMotInterval,std::to_wstring(s.motInterval));
    int type=0;std::wstring v=s.source;if(v.rfind(L"tone:",0)==0){type=1;v=v.substr(5);}else if(v.rfind(L"file:",0)==0){type=2;v=v.substr(5);}else if(v.rfind(L"stream:",0)==0){type=3;v=v.substr(7);}else if(v.rfind(L"device:",0)==0)v=v.substr(7);
    SendMessageW(comboSource,CB_SETCURSEL,type,0);setText(editSource,v);SendMessageW(comboCodec,CB_SETCURSEL,s.codec,0);SendMessageW(comboSampling,CB_SETCURSEL,s.sampling==32000?0:1,0);SendMessageW(comboEep,CB_SETCURSEL,s.eep,0);
}
void saveSelected(bool refresh=true) {
    int i=selectedService;
    if(i<0 || (size_t)i>=services.size())return;selectedService=i;auto&s=services[i];s.label=text(editService);s.sid=text(editSid);s.dls=text(editDls);
    s.shortLabel=text(editScids);if(s.shortLabel.size()>8)s.shortLabel.resize(8);
    s.motFolder=text(editMotFolder);try{s.motInterval=std::max(1u,(unsigned)std::stoul(text(editMotInterval)));}catch(...){s.motInterval=10;}
    wchar_t*end=nullptr;std::wstring subText=text(editSubch);unsigned long sub=wcstoul(subText.c_str(),&end,10);if(end&&end!=subText.c_str()&&*end==0&&sub<=63)s.subch=(unsigned)sub;
    int type=(int)SendMessageW(comboSource,CB_GETCURSEL,0,0);const wchar_t* prefix=type==1?L"tone:":type==2?L"file:":type==3?L"stream:":L"device:";s.source=prefix+text(editSource);
    try{s.bitrate=(unsigned)std::stoul(text(editRate));}catch(...){s.bitrate=0;}s.codec=(unsigned)SendMessageW(comboCodec,CB_GETCURSEL,0,0);s.sampling=SendMessageW(comboSampling,CB_GETCURSEL,0,0)==0?32000:48000;int p=(int)SendMessageW(comboEep,CB_GETCURSEL,0,0);if(p>=0)s.eep=(dab_eep_profile_t)p;
    if(refresh){populateList();ListView_SetItemState(list,i,LVIS_SELECTED,LVIS_SELECTED);}
}
void createLabel(const wchar_t* s,int x,int y,int w=120) { CreateWindowW(L"STATIC",s,WS_CHILD|WS_VISIBLE,x,y,w,20,wnd,0,0,0); }
HWND edit(int id,int x,int y,int w) { return CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,x,y,w,24,wnd,(HMENU)(INT_PTR)id,0,0); }
BOOL CALLBACK applyFont(HWND h,LPARAM f){SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);return TRUE;}
void browseAudio(){wchar_t p[MAX_PATH]{};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=wnd;d.lpstrFile=p;d.nMaxFile=MAX_PATH;d.lpstrFilter=L"Audio files\0*.wav;*.mp3;*.aac;*.m4a;*.flac;*.ogg;*.opus;*.wma;*.aiff;*.m3u;*.m3u8\0All files\0*.*\0";d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;if(GetOpenFileNameW(&d)){SendMessageW(comboSource,CB_SETCURSEL,2,0);setText(editSource,p);}}
void refreshLanguage(){std::wstring caption=L"WolfDAB ";caption+=APP_VERSION;caption+=tr(L" — Controllo multiplex DAB/DAB+",L" — DAB/DAB+ multiplex control");SetWindowTextW(wnd,caption.c_str());SetWindowTextW(ampCheck,tr(L"Amplificatore ON",L"Amplifier ON"));if(applyFormatButton)SetWindowTextW(applyFormatButton,tr(L"Applica formato a tutti",L"Apply format to all"));if(motFolderLabel)SetWindowTextW(motFolderLabel,tr(L"Cartella immagini MOT",L"MOT image folder"));if(motIntervalLabel)SetWindowTextW(motIntervalLabel,tr(L"Intervallo",L"Interval")); status(); }
bool saveConfig(const std::wstring& path) {
    saveSelected(false);
    std::wofstream out(path.c_str()); if (!out) return false;
    out << L"wolfdab=1\nensemble=" << text(editEnsemble) << L"\nensemble_id=" << text(editEid) << L"\necc=" << text(editEcc) << L"\nchannel=" << text(comboChannel) << L"\ntx_gain=" << text(editGain) << L"\namp_on=" << (SendMessageW(ampCheck,BM_GETCHECK,0,0)==BST_CHECKED) << L"\nservices=" << services.size() << L"\n";
    for (size_t i=0;i<services.size();++i) { auto&s=services[i]; out << L"service."<<i<<L".label="<<s.label<<L"\nservice."<<i<<L".short_label="<<s.shortLabel<<L"\nservice."<<i<<L".sid="<<s.sid<<L"\nservice."<<i<<L".scids="<<s.scids<<L"\nservice."<<i<<L".subch="<<s.subch<<L"\nservice."<<i<<L".source="<<s.source<<L"\nservice."<<i<<L".bitrate="<<s.bitrate<<L"\nservice."<<i<<L".codec="<<s.codec<<L"\nservice."<<i<<L".sampling="<<s.sampling<<L"\nservice."<<i<<L".eep="<<(unsigned)s.eep<<L"\nservice."<<i<<L".dls="<<s.dls<<L"\nservice."<<i<<L".mot_folder="<<s.motFolder<<L"\nservice."<<i<<L".mot_interval="<<s.motInterval<<L"\n"; }
    bool ok=(bool)out;if(ok)rememberLastConfig(path);return ok;
}
bool loadConfig(const std::wstring& path) {
    std::wifstream in(path.c_str()); if (!in) return false; std::vector<Service> next; std::wstring line;
    while (std::getline(in,line)) {
        auto cut=line.find(L'='); if(cut==std::wstring::npos)continue; auto k=line.substr(0,cut),v=line.substr(cut+1);
        if(k==L"ensemble")setText(editEnsemble,v); else if(k==L"ensemble_id")setText(editEid,v); else if(k==L"ecc")setText(editEcc,v); else if(k==L"channel")SendMessageW(comboChannel,CB_SELECTSTRING,-1,(LPARAM)v.c_str()); else if(k==L"tx_gain")setText(editGain,v); else if(k==L"amp_on")SendMessageW(ampCheck,BM_SETCHECK,std::wcstoul(v.c_str(),nullptr,10)?BST_CHECKED:BST_UNCHECKED,0);
        else if(k.rfind(L"service.",0)==0) { auto p=k.find(L'.',8); if(p==std::wstring::npos)continue; size_t n=(size_t)std::wcstoul(k.substr(8,p-8).c_str(),nullptr,10); while(next.size()<=n){Service d;d.subch=(unsigned)next.size()+1;next.push_back(d);} auto f=k.substr(p+1); auto&s=next[n]; if(f==L"label")s.label=v; else if(f==L"short_label")s.shortLabel=v.substr(0,8); else if(f==L"sid")s.sid=v; else if(f==L"scids")s.scids=(unsigned)std::wcstoul(v.c_str(),nullptr,0); else if(f==L"subch")s.subch=(unsigned)std::wcstoul(v.c_str(),nullptr,0); else if(f==L"source")s.source=v; else if(f==L"bitrate")s.bitrate=(unsigned)std::wcstoul(v.c_str(),nullptr,10); else if(f==L"codec")s.codec=(unsigned)std::wcstoul(v.c_str(),nullptr,10); else if(f==L"sampling")s.sampling=(unsigned)std::wcstoul(v.c_str(),nullptr,10); else if(f==L"eep")s.eep=(dab_eep_profile_t)std::wcstoul(v.c_str(),nullptr,10); else if(f==L"dls")s.dls=v; else if(f==L"mot_folder")s.motFolder=v; else if(f==L"mot_interval")s.motInterval=std::max(1u,(unsigned)std::wcstoul(v.c_str(),nullptr,10)); }
    }
    if(next.empty())return false;
    for(auto& s:next)if(s.shortLabel==L"Nuovo"){s.shortLabel.clear();for(wchar_t c:s.label)if(c!=L' '&&s.shortLabel.size()<8)s.shortLabel+=c;}
    services=std::move(next); selectedService=0; populateList(); ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED); loadSelected();rememberLastConfig(path); return true;
}
bool chooseConfig(bool saveAs) { wchar_t p[MAX_PATH]{}; if(!configPath.empty())wcsncpy_s(p,configPath.c_str(),_TRUNCATE); OPENFILENAMEW d{}; d.lStructSize=sizeof(d);d.hwndOwner=wnd;d.lpstrFile=p;d.nMaxFile=MAX_PATH;d.lpstrFilter=L"WolfDAB configuration (*.wolfdab)\0*.wolfdab\0All files\0*.*\0";d.Flags=OFN_PATHMUSTEXIST|(saveAs?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST); if(!(saveAs?GetSaveFileNameW(&d):GetOpenFileNameW(&d)))return false;configPath=p;return true; }
std::wstring quote(const std::wstring& s){std::wstring o=L"\"";unsigned bs=0;for(wchar_t c:s){if(c==L'\\'){++bs;continue;}if(c==L'\"'){o.append(bs*2+1,L'\\');o+=c;bs=0;continue;}o.append(bs,L'\\');bs=0;o+=c;}o.append(bs*2,L'\\');return o+L"\"";}
void stopTx(){if(!txProcess)return;if(txRunning()&&txStopEvent){SetEvent(txStopEvent);if(WaitForSingleObject(txProcess,5000)==WAIT_TIMEOUT)TerminateProcess(txProcess,2);}CloseHandle(txProcess);txProcess=nullptr;if(txStopEvent){CloseHandle(txStopEvent);txStopEvent=nullptr;}EnableWindow(GetDlgItem(wnd,ID_START),TRUE);EnableWindow(GetDlgItem(wnd,ID_STOP),FALSE);status();}
void startTx(){
    saveSelected(false);
    if(txRunning())return;
    if(services.empty()||services.size()>64){MessageBoxW(wnd,tr(L"Sono ammessi da 1 a 64 servizi, entro il limite di 864 CU.",L"1 to 64 services are allowed, within the 864 CU limit."),L"WolfDAB",MB_ICONERROR);return;}
    if(capacity()>864){MessageBoxW(wnd,tr(L"Il multiplex supera 864 CU.",L"The multiplex exceeds 864 CU."),L"WolfDAB",MB_ICONERROR);return;}
    for(auto&s:services)if(!s.bitrate||dab_eep_cu_for_bitrate(s.bitrate,s.eep)==0){MessageBoxW(wnd,tr(L"Bitrate non valido per il profilo EEP selezionato.",L"Invalid bitrate for the selected EEP profile."),L"WolfDAB",MB_ICONERROR);return;}
    for(size_t i=0;i<services.size();++i){auto&s=services[i];unsigned long sid=std::wcstoul(s.sid.c_str(),nullptr,0);if(sid>0xffff||s.scids>15||s.subch>63){MessageBoxW(wnd,tr(L"Service ID, ID breve o SubCh non valido.",L"Invalid Service ID, Short ID or SubCh."),L"WolfDAB",MB_ICONERROR);return;}for(size_t j=0;j<i;++j)if(services[j].sid==s.sid||services[j].subch==s.subch){MessageBoxW(wnd,tr(L"Service ID e SubCh devono essere univoci.",L"Service ID and SubCh must be unique."),L"WolfDAB",MB_ICONERROR);return;}}
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);wchar_t*slash=wcsrchr(exe,L'\\');std::wstring dir;if(slash){dir.assign(exe,slash+1);wcscpy_s(slash+1,MAX_PATH-(slash+1-exe),L"dabtx.exe");}
    std::wstring ev=L"Local\\WolfDAB.Stop."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());txStopEvent=CreateEventW(nullptr,TRUE,FALSE,ev.c_str());
    std::wstring cmd=quote(exe)+L" --stop-event "+quote(ev)+L" --ensemble "+quote(text(editEnsemble))+L" --ensemble-id "+quote(text(editEid))+L" --ecc "+quote(text(editEcc))+L" --amp-flag "+(SendMessageW(ampCheck,BM_GETCHECK,0,0)==BST_CHECKED?L"0":L"1");
    int active=selectedService;std::wstring shownSid=text(editSid);for(size_t k=0;k<services.size();++k)if(services[k].sid==shownSid){active=(int)k;break;}
    for(size_t k=0;k<services.size();++k){auto&s=services[k];unsigned liveSub=s.subch;if((int)k==active){s.shortLabel=text(editScids);if(s.shortLabel.size()>8)s.shortLabel.resize(8);std::wstring v=text(editSubch);wchar_t*e=nullptr;unsigned long n=wcstoul(v.c_str(),&e,10);if(e&&e!=v.c_str()&&*e==0&&n<=63)liveSub=(unsigned)n;s.subch=liveSub;}std::wstring packed=s.sid+L"~|~"+std::to_wstring(s.scids)+L"~|~"+std::to_wstring(liveSub)+L"~|~"+std::to_wstring(s.bitrate)+L"~|~"+std::to_wstring(s.codec)+L"~|~"+std::to_wstring(s.sampling)+L"~|~"+std::to_wstring((unsigned)s.eep)+L"~|~"+s.label+L"~|~"+s.source+L"~|~"+s.dls+L"~|~"+s.shortLabel+L"~|~"+s.motFolder+L"~|~"+std::to_wstring(s.motInterval);cmd+=L" --svc "+quote(packed);}
    cmd+=L" --tx "+quote(text(comboChannel))+L" 0 0 "+quote(text(editGain));
    std::vector<wchar_t> buf(cmd.begin(),cmd.end());buf.push_back(0);STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
    BOOL ok=CreateProcessW(exe,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi);if(!ok){CloseHandle(txStopEvent);txStopEvent=nullptr;MessageBoxW(wnd,tr(L"Impossibile avviare il motore TX.",L"Unable to start the TX engine."),L"WolfDAB",MB_ICONERROR);return;}CloseHandle(pi.hThread);txProcess=pi.hProcess;EnableWindow(GetDlgItem(wnd,ID_START),FALSE);EnableWindow(GetDlgItem(wnd,ID_STOP),TRUE);SetTimer(wnd,1,500,nullptr);status();
}
void addMenus(){ HMENU bar=CreateMenu(),file=CreatePopupMenu(),help=CreatePopupMenu(); AppendMenuW(file,MF_STRING,IDM_NEW,tr(L"Nuova configurazione",L"New configuration"));AppendMenuW(file,MF_STRING,IDM_OPEN,tr(L"Apri…",L"Open…"));AppendMenuW(file,MF_STRING,IDM_SAVE,tr(L"Salva",L"Save"));AppendMenuW(file,MF_STRING,IDM_SAVE_AS,tr(L"Salva con nome…",L"Save as…"));AppendMenuW(file,MF_SEPARATOR,0,0);AppendMenuW(file,MF_STRING,IDM_EXIT,tr(L"Esci",L"Exit"));AppendMenuW(help,MF_STRING,IDM_ABOUT,tr(L"Informazioni",L"About"));AppendMenuW(bar,MF_POPUP,(UINT_PTR)file,tr(L"File",L"File"));AppendMenuW(bar,MF_POPUP,(UINT_PTR)help,tr(L"Aiuto",L"Help"));SetMenu(wnd,bar); }
void createUi(){InitCommonControls();CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1140,5,wnd,(HMENU)ID_ACCENT,instance,0);HICON icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,56,56,LR_DEFAULTCOLOR);HWND logo=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ICON,16,14,60,60,wnd,0,instance,0);SendMessageW(logo,STM_SETICON,(WPARAM)icon,0);HWND title=CreateWindowW(L"STATIC",L"WolfDAB",WS_CHILD|WS_VISIBLE,88,16,220,34,wnd,0,0,0);CreateWindowW(L"STATIC",tr(L"Multiplex DAB/DAB+ per HackRF One",L"DAB/DAB+ multiplex for HackRF One"),WS_CHILD|WS_VISIBLE,90,48,360,22,wnd,0,0,0);createLabel(tr(L"Servizi nel multiplex",L"Services in multiplex"),16,86,260);list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL,16,110,620,210,wnd,0,0,0);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);const wchar_t*h[]={tr(L"Etichetta",L"Label"),L"Service ID",L"Bitrate",L"Codec",tr(L"Sampling",L"Sampling"),L"EEP",L"CU"};int widths[]={145,82,76,105,76,85,48};for(int i=0;i<7;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)h[i];c.cx=widths[i];ListView_InsertColumn(list,i,&c);}CreateWindowW(L"BUTTON",tr(L"Aggiungi",L"Add"),WS_CHILD|WS_VISIBLE,16,334,100,32,wnd,(HMENU)ID_ADD,0,0);CreateWindowW(L"BUTTON",tr(L"Rimuovi",L"Remove"),WS_CHILD|WS_VISIBLE,126,334,100,32,wnd,(HMENU)ID_REMOVE,0,0);CreateWindowW(L"BUTTON",L"Italiano / English",WS_CHILD|WS_VISIBLE,490,334,146,32,wnd,(HMENU)ID_LANG,0,0);
createLabel(tr(L"Servizio selezionato",L"Selected service"),666,86,300);createLabel(tr(L"Etichetta",L"Label"),666,116);editService=edit(ID_SERVICE,666,138,436);createLabel(L"Service ID",666,172);editSid=edit(ID_SID,666,194,150);createLabel(tr(L"Tipo sorgente",L"Source type"),666,228);comboSource=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,666,250,436,180,wnd,(HMENU)ID_SOURCE,0,0);for(auto*s:{tr(L"Dispositivo / WASAPI",L"Device / WASAPI"),tr(L"Generatore di tono",L"Tone generator"),tr(L"File audio locale",L"Local audio file"),L"Stream HTTP / HTTPS"})SendMessageW(comboSource,CB_ADDSTRING,0,(LPARAM)s);createLabel(tr(L"Dispositivo, tono o indirizzo",L"Device, tone or address"),666,284,320);editSource=edit(128,666,306,391);CreateWindowW(L"BUTTON",L"…",WS_CHILD|WS_VISIBLE,1065,306,37,24,wnd,(HMENU)ID_BROWSE,0,0);createLabel(L"Bitrate",666,340,76);editRate=edit(ID_BITRATE,666,362,76);createLabel(L"CODEC",753,340,126);comboCodec=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,753,362,126,160,wnd,(HMENU)ID_CODEC,0,0);for(auto*s:{L"AAC-LC",L"HE-AAC v1",L"HE-AAC v2"})SendMessageW(comboCodec,CB_ADDSTRING,0,(LPARAM)s);createLabel(L"Sampling",890,340,90);comboSampling=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,890,362,90,100,wnd,(HMENU)ID_SAMPLING,0,0);SendMessageW(comboSampling,CB_ADDSTRING,0,(LPARAM)L"32 kHz");SendMessageW(comboSampling,CB_ADDSTRING,0,(LPARAM)L"48 kHz");createLabel(L"EEP",992,340,110);comboEep=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,992,362,110,180,wnd,(HMENU)ID_EEP,0,0);createLabel(L"DLS",666,396);editDls=edit(ID_DLS,666,418,436);
createLabel(tr(L"Ensemble",L"Ensemble"),16,480);createLabel(tr(L"Etichetta",L"Label"),16,510);editEnsemble=edit(ID_ENSEMBLE,16,532,220);setText(editEnsemble,L"WolfDAB Ensemble");createLabel(L"Ensemble ID",250,510);editEid=edit(ID_EID,250,532,110);setText(editEid,L"0xE001");createLabel(L"ECC",375,510);editEcc=edit(ID_ECC,375,532,70);setText(editEcc,L"0xE0");createLabel(tr(L"Blocco",L"Block"),460,510);comboChannel=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,460,532,90,180,wnd,(HMENU)ID_CHANNEL,0,0);for(int n=5;n<=13;n++)for(wchar_t l:L"ABCD"){wchar_t q[8];swprintf(q,8,L"%d%c",n,l);SendMessageW(comboChannel,CB_ADDSTRING,0,(LPARAM)q);}SendMessageW(comboChannel,CB_SELECTSTRING,-1,(LPARAM)L"5A");createLabel(tr(L"Gain TX",L"TX gain"),565,510);editGain=edit(ID_GAIN,565,532,70);setText(editGain,L"0");ampCheck=CreateWindowW(L"BUTTON",tr(L"Amplificatore ON",L"Amplifier ON"),WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,666,474,280,26,wnd,(HMENU)ID_AMP,0,0);SendMessageW(ampCheck,BM_SETCHECK,BST_UNCHECKED,0);CreateWindowW(L"BUTTON",tr(L"Avvia TX",L"Start TX"),WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,666,510,160,42,wnd,(HMENU)ID_START,0,0);CreateWindowW(L"BUTTON",tr(L"Ferma TX",L"Stop TX"),WS_CHILD|WS_VISIBLE,840,510,160,42,wnd,(HMENU)ID_STOP,0,0);statusbar=CreateWindowW(STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE,0,0,0,0,wnd,0,0,0);uiFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");titleFont=CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");EnumChildWindows(wnd,applyFont,(LPARAM)uiFont);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);fillProfiles();populateList();ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);loadSelected(); }
void info(){std::wstring message=L"WolfDAB ";message+=APP_VERSION;message+=L"\nDAB/DAB+ multiplex controller for HackRF One\n\n© Freewaves.it\nEmanuele Pelicioli\nmax@freewaves.it\n\nNative Windows x64 • Mode I • 2.048 MS/s\n\nTransmit only where authorised.";std::wstring title=L"WolfDAB ";title+=APP_VERSION;MessageBoxW(wnd,message.c_str(),title.c_str(),MB_OK|MB_ICONINFORMATION);}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 switch(m){
 case WM_CREATE:wnd=h;windowBrush=CreateSolidBrush(RGB(248,249,251));accentBrush=CreateSolidBrush(RGB(158,235,55));{BOOL dark=FALSE;DwmSetWindowAttribute(h,20,&dark,sizeof(dark));DWORD corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof(corner));}addMenus();createUi();if(startupConfig.empty())startupConfig=readLastConfig();if(!startupConfig.empty()){configPath=startupConfig;loadConfig(configPath);}refreshLanguage();EnableWindow(GetDlgItem(h,ID_STOP),FALSE);return 0;
 case WM_CTLCOLORSTATIC:{HDC dc=(HDC)w;SetBkMode(dc,TRANSPARENT);if(GetDlgCtrlID((HWND)l)==ID_ACCENT)return(LRESULT)accentBrush;return(LRESULT)windowBrush;}
 case WM_NOTIFY:if(((LPNMHDR)l)->hwndFrom==list&&((LPNMHDR)l)->code==NM_CLICK){auto*n=(NMITEMACTIVATE*)l;if(n->iItem>=0&&n->iItem!=selectedService){saveSelected(false);selectedService=n->iItem;loadSelected();}}break;
 case WM_TIMER:if(txProcess&&!txRunning())stopTx();else status();return 0;
 case WM_COMMAND:{int id=LOWORD(w),code=HIWORD(w);if(code==EN_CHANGE&&(id==ID_SCIDS||id==ID_SUBCH)){int i=selectedService;if(i>=0&&(size_t)i<services.size()){std::wstring v=text((HWND)l);if(id==ID_SCIDS){services[(size_t)i].shortLabel=v.substr(0,8);}else{wchar_t*end=nullptr;unsigned long n=wcstoul(v.c_str(),&end,10);if(end&&*end==0&&n<=63)services[(size_t)i].subch=(unsigned)n;}}}if(code==EN_CHANGE&&id==ID_BITRATE&&selectedService>=0&&(size_t)selectedService<services.size()){std::wstring v=text(editRate);wchar_t*end=nullptr;unsigned long n=wcstoul(v.c_str(),&end,10);if(end&&end!=v.c_str()&&*end==0){services[(size_t)selectedService].bitrate=(unsigned)n;int keep=selectedService;populateList();selectedService=keep;ListView_SetItemState(list,keep,LVIS_SELECTED,LVIS_SELECTED);status();}}if((code==EN_KILLFOCUS&&(id==ID_SERVICE||id==ID_SID||id==ID_SOURCE_VALUE||id==ID_BITRATE||id==ID_DLS||id==ID_SCIDS||id==ID_SUBCH||id==ID_MOT_FOLDER||id==ID_MOT_INTERVAL))||(code==CBN_SELCHANGE&&(id==ID_SOURCE||id==ID_CODEC||id==ID_SAMPLING||id==ID_EEP)))saveSelected(false);switch(id){
  case ID_BROWSE:browseAudio();break;case ID_MOT_BROWSE:browseMotFolder();break;case ID_LANG:saveSelected();italian=!italian;refreshLanguage();break;case ID_ADD:addClonedService();break;case ID_APPLY_FORMAT:applyFormatToAll();break;
  case ID_CODEC:case ID_SAMPLING:case ID_EEP:if(code==CBN_SELCHANGE){saveSelected(false);int keep=selectedService;populateList();if(keep>=0){selectedService=keep;ListView_SetItemState(list,keep,LVIS_SELECTED,LVIS_SELECTED);}status();}break;
  case ID_REMOVE:{int i=sel();if(i>=0){services.erase(services.begin()+i);selectedService=services.empty()?-1:((size_t)i<services.size()?i:(int)services.size()-1);populateList();if(selectedService>=0){ListView_SetItemState(list,selectedService,LVIS_SELECTED,LVIS_SELECTED);loadSelected();}}}break;
  case IDM_NEW:services={{L"Radio 1",L"0xE001",L"device:",72,DAB_EEP_3A,L"WolfDAB"}};configPath.clear();populateList();break;
  case IDM_OPEN:if(chooseConfig(false)&&!loadConfig(configPath))MessageBoxW(h,L"Unable to load configuration.",L"WolfDAB",MB_ICONERROR);break;
  case IDM_SAVE:saveSelected();if(configPath.empty())chooseConfig(true);if(!configPath.empty()&&!saveConfig(configPath))MessageBoxW(h,L"Unable to save configuration.",L"WolfDAB",MB_ICONERROR);break;
  case IDM_SAVE_AS:saveSelected();if(chooseConfig(true)&&!saveConfig(configPath))MessageBoxW(h,L"Unable to save configuration.",L"WolfDAB",MB_ICONERROR);break;
  case IDM_EXIT:DestroyWindow(h);break;case IDM_ABOUT:info();break;case ID_START:startTx();break;case ID_STOP:stopTx();break;}break;}
 case WM_CLOSE:stopTx();DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,1);if(uiFont)DeleteObject(uiFont);if(titleFont)DeleteObject(titleFont);if(windowBrush)DeleteObject(windowBrush);if(accentBrush)DeleteObject(accentBrush);PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE i,HINSTANCE, PWSTR cmd,int){instance=i;if(cmd&&wcsncmp(cmd,L"--config",8)==0){const wchar_t*p=cmd+8;while(*p==L' ')++p;if(*p==L'\"'){++p;const wchar_t*e=wcschr(p,L'\"');startupConfig.assign(p,e?e:p+wcslen(p));}else startupConfig=p;}WNDCLASSW c{};c.hInstance=i;c.lpszClassName=L"WolfDAB";c.lpfnWndProc=proc;c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hIcon=LoadIconW(i,MAKEINTRESOURCEW(101));c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;constexpr DWORD style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE;HWND h=CreateWindowW(c.lpszClassName,L"WolfDAB 1.0.9",style,80,35,1160,700,nullptr,nullptr,i,nullptr);if(!h)return 2;MSG m;while(GetMessageW(&m,nullptr,0,0)){TranslateMessage(&m);DispatchMessageW(&m);}return 0;}
