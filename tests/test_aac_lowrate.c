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

/* Regression: PAD is an ancillary DSE.  For HE-AAC v1, a 120 ms DAB+
 * superframe has three AUs.  The transmitter must put the field into the
 * final AU only, otherwise each copy consumes part of the audio budget. */
static int test_pad_once(void) {
    aac_enc_t *enc=aac_enc_open_ex_channels(DABTX_AAC_MODE_DABPLUS_SBR,64000,48000,2);
    if(!enc){fprintf(stderr,"cannot open HE-AAC v1 encoder\n");return 1;}
    int16_t pcm[DABTX_AAC_GRANULE*2]={0};
    uint8_t out[8192], pad[]={0x11,0x22,0x33,0x44,0x20,0x02};
    size_t out_len=0;
    int calls=aac_enc_calls_per_sf(enc), phase=0;
    for(int i=0;i<calls+1 && out_len==0;++i) {
        int final_input=((phase+1)%calls)==0;
        if(aac_enc_frame(enc,pcm,out,sizeof(out),&out_len,
                         final_input?pad:NULL,final_input?sizeof(pad):0)!=0) {
            aac_enc_close(enc);return 1;
        }
        phase=(phase+1)%calls;
    }
    aac_enc_close(enc);
    if(out_len!=880){fprintf(stderr,"64 kbps output %zu bytes\n",out_len);return 1;}

    int starts[4]={6,0,0,880};
    int hdr_bits=24;
    for(int a=1;a<3;++a){
        int bp=hdr_bits/8,bo=hdr_bits%8;
        starts[a]=(((out[bp]<<8)|out[bp+1])>>(4-bo))&0xFFF;
        hdr_bits+=12;
    }
    int dse_count=0;
    for(int a=0;a<3;++a)if(((out[starts[a]]>>5)&7)==4)dse_count++;
    if(dse_count!=1 || ((out[starts[2]]>>5)&7)!=4){
        fprintf(stderr,"PAD DSE placement invalid: %d DSEs\n",dse_count);return 1;
    }
    return 0;
}

int main(void){return test_rate(8)||test_rate(16)||test_rate(24)||test_pad_once();}
