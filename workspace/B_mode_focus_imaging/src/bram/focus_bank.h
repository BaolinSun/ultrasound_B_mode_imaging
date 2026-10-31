#ifndef UB_FOCUS_BANK_H
#define UB_FOCUS_BANK_H
#include <stdint.h>
#include <stddef.h>
#define FOCUS_WORDS 16384U
#define FOCUS_COUNT 16U
typedef struct { uint32_t depth,crc; const uint32_t *words; } focus_entry_t;
extern const focus_entry_t focus_entries[FOCUS_COUNT];
typedef int (*focus_writer_t)(uint32_t index,uint32_t value,void *context);
int focus_bank_init(void);
int focus_bank_ready(void);
const focus_entry_t *focus_bank_find(uint32_t depth);
uint32_t focus_words_crc(const uint32_t *words,size_t count);
const char *focus_bank_apply(uint32_t depth,uint32_t crc,focus_writer_t writer,void *context);
void focus_bank_invalidate(void);
uint32_t focus_bank_selected(void);
uint32_t focus_bank_selected_crc(void);
/* Hardware admission is implemented by the existing BRAM/control adapter. */
const char *Focus_Apply(uint32_t depth,uint32_t crc);
#endif
