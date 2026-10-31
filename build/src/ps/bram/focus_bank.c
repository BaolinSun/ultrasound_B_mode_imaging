#include "focus_bank.h"
static int ready;
static uint32_t selected,selected_crc;
uint32_t focus_words_crc(const uint32_t *words,size_t count) {
    uint32_t crc=0xffffffffU;
    for(size_t i=0;i<count;i++) for(unsigned byte=0;byte<4;byte++) {
        crc^=(words[i]>>(8*byte))&255U;
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1U)?0xedb88320U:0U);
    }
    return crc^0xffffffffU;
}
void focus_bank_invalidate(void){selected=selected_crc=0;}
int focus_bank_ready(void){return ready;}
uint32_t focus_bank_selected(void){return selected;}
uint32_t focus_bank_selected_crc(void){return selected_crc;}
const focus_entry_t *focus_bank_find(uint32_t depth) {
    if(depth<20 || depth>170 || depth%10) return NULL;
    const focus_entry_t *e=&focus_entries[(depth-20)/10];
    return e->depth==depth && e->words?e:NULL;
}
int focus_bank_init(void) {
    ready=0;focus_bank_invalidate();
    for(unsigned i=0;i<FOCUS_COUNT;i++) {
        const focus_entry_t *e=focus_bank_find(20+10*i);
        if(!e || focus_words_crc(e->words,FOCUS_WORDS)!=e->crc) return -1;
    }
    ready=1;return 0;
}
const char *focus_bank_apply(uint32_t depth,uint32_t crc,focus_writer_t writer,void *context) {
    if(!ready) return "FOCUS_BANK_NOT_READY";
    const focus_entry_t *e=focus_bank_find(depth);
    if(!e || !writer) return "FOCUS_ARGS";
    if(e->crc!=crc) return "FOCUS_CATALOG_MISMATCH";
    if(focus_words_crc(e->words,FOCUS_WORDS)!=crc) return "FOCUS_DATA_CRC";
    focus_bank_invalidate();
    for(unsigned i=0;i<FOCUS_WORDS;i++) if(writer(i,e->words[i],context)) return "FOCUS_WRITE";
    selected=depth;selected_crc=crc;return NULL;
}
