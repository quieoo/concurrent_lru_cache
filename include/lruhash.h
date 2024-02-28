#ifndef __LRU_HASH__
#define __LRU_HASH__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t LA;
typedef struct PhysicalAddress{
    uint8_t data[20];
}PhysicalAddress;



void* lru_hash_build(int cache_size, int ht_len);
void lru_hash_bulk_load(void* ptr, LA* las, PhysicalAddress* pas, int num);
int lru_hash_get_pa(void* ptr, LA la, PhysicalAddress* pa);


#ifdef __cplusplus
}
#endif
#endif
