#include <twolame.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x,m) do{if(!(x)){fprintf(stderr,"FAIL: %s\n",m);return 1;}}while(0)
int main(void){
 twolame_options*o=twolame_init();CHECK(o,"init");
 CHECK(!twolame_set_in_samplerate(o,48000)&&!twolame_set_out_samplerate(o,48000),"rate");
 CHECK(!twolame_set_num_channels(o,2)&&!twolame_set_mode(o,TWOLAME_STEREO),"channels");
 CHECK(!twolame_set_bitrate(o,128)&&!twolame_set_DAB(o,1),"DAB mode");
 CHECK(!twolame_set_DAB_xpad_length(o,52)&&!twolame_set_error_protection(o,1),"PAD");
 CHECK(twolame_init_params(o)>=0&&twolame_set_DAB_scf_crc_length(o)>=0,"params");
 int16_t pcm[1152*2]={0};uint8_t a[2048],b[2048];
 int na=twolame_encode_buffer_interleaved(o,pcm,1152,a,sizeof(a));
 int nb=twolame_encode_buffer_interleaved(o,pcm,1152,b,sizeof(b));
 CHECK(na==384&&nb==384,"128 kbps DAB frame must be 384 bytes");
 CHECK(a[0]==0xff&&(a[1]&0xf0)==0xf0,"MP2 sync");
 CHECK(twolame_set_DAB_scf_crc(o,a,na)>=0,"ScF CRC");
 twolame_close(&o);
 o=twolame_init();CHECK(o,"LSF init");
 CHECK(!twolame_set_in_samplerate(o,24000)&&!twolame_set_out_samplerate(o,24000),"LSF rate");
 CHECK(!twolame_set_num_channels(o,1)&&!twolame_set_mode(o,TWOLAME_MONO),"LSF mono");
 CHECK(!twolame_set_bitrate(o,64)&&!twolame_set_DAB(o,1),"LSF DAB mode");
 CHECK(!twolame_set_DAB_xpad_length(o,52)&&!twolame_set_error_protection(o,1),"LSF PAD");
 CHECK(twolame_init_params(o)>=0&&twolame_set_DAB_scf_crc_length(o)>=0,"LSF params");
 na=twolame_encode_buffer(o,pcm,pcm,1152,a,sizeof(a));
 nb=twolame_encode_buffer(o,pcm,pcm,1152,b,sizeof(b));
 CHECK(na==384&&nb==384,"64 kbps/24 kHz frame must span two 192-byte CIF parts");
 CHECK(twolame_set_DAB_scf_crc(o,a,na)>=0,"LSF ScF CRC");
 twolame_close(&o);puts("test_mp2: PASS");return 0;
}
