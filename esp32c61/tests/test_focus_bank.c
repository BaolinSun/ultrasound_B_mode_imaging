#include "focus_bank.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef FOCUS_BANK_FIXTURE
static uint32_t fixture[FOCUS_WORDS];
#define ENTRY(d) {d,0xd7978eebU,fixture}
const focus_entry_t focus_entries[FOCUS_COUNT]={ENTRY(20),ENTRY(30),ENTRY(40),ENTRY(50),ENTRY(60),ENTRY(70),ENTRY(80),ENTRY(90),ENTRY(100),ENTRY(110),ENTRY(120),ENTRY(130),ENTRY(140),ENTRY(150),ENTRY(160),ENTRY(170)};
#endif
static unsigned count,fail_at=FOCUS_WORDS;
static int writer(uint32_t i,uint32_t v,void *ctx){const focus_entry_t *e=ctx;assert(i==count++ && v==e->words[i]);return i==fail_at;}
int main(void){
 assert(!focus_bank_ready());assert(focus_bank_apply(80,0,writer,NULL));
 assert(!focus_bank_init());assert(focus_bank_ready());
 assert(!focus_bank_find(0)&&!focus_bank_find(21)&&!focus_bank_find(180));
 for(unsigned d=20;d<=170;d+=10){const focus_entry_t *e=focus_bank_find(d);count=0;
  assert(!focus_bank_apply(d,e->crc,writer,(void*)e));assert(count==FOCUS_WORDS&&focus_bank_selected()==d&&focus_bank_selected_crc()==e->crc);
  count=0;assert(focus_bank_apply(d,e->crc^1,writer,(void*)e)&&!count);
 }
 assert(focus_bank_apply(21,0,writer,NULL));assert(focus_bank_apply(80,0,NULL,NULL));
 const focus_entry_t *e=focus_bank_find(80);fail_at=123;count=0;
 assert(focus_bank_apply(80,e->crc,writer,(void*)e)&&count==124&&!focus_bank_selected());
 focus_bank_invalidate();assert(!focus_bank_selected_crc());
#ifdef FOCUS_BANK_FIXTURE
 fixture[10]^=1;count=0;assert(!strcmp(focus_bank_apply(80,e->crc,writer,(void*)e),"FOCUS_DATA_CRC")&&!count);
 assert(focus_bank_init()!=0&&!focus_bank_ready());fixture[10]^=1;assert(!focus_bank_init());
#endif
 puts("PASS focus bank: 16 tables, CRC/range/metadata/full ordered writes/failure invalidation");return 0;
}
