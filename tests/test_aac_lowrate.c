#include "encoder/aac_enc.h"
#include <stdint.h>
#include <stdio.h>

static int test_rate(int kbps) {
    aac_enc_t *enc=aac_enc_open_ex_channels(DABTX_AAC_MODE_DABPLUS_SBR,kbps*1000,48000,1);
    if(!enc){fprintf(stderr,"cannot open HE-AAC v1 mono at %d kbps\n",kbps);return 1;}
    int16_t pcm[DABTX_AAC_GRANULE]={0};uint8_t out[8192];size_t out_len=0;
    for(int i=0;i<12&&out_len==0;++i)if(aac_enc_frame(enc,pcm,out,sizeof(out),&out_len,NULL,0)!=0){aac_enc_close(enc);return 1;}
    aac_enc_close(enc);
    if(out_len!=(size_t)(110*(kbps/8))){fprintf(stderr,"%d kbps output %zu bytes\n",kbps,out_len);return 1;}
    return 0;
}

int main(void){return test_rate(8)||test_rate(16)||test_rate(24);}
