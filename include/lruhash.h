#ifndef __LRU_HASH__
#define __LRU_HASH__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t LA;
typedef struct PhysicalAddr{
    uint8_t data[20];
}PhysicalAddr;



void* build_lru_hash(int cache_size, int ht_len);
void bulk_load(void* ptr, LA* las, PhysicalAddr* pas, int num);
int get_pa(void* ptr, LA la, PhysicalAddr* pa);

#ifdef __cplusplus
}
#endif
#endif
