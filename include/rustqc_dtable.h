/*
    Based on rust rep quick cache: https://github.com/arthurprs/quick-cache
    Create full mapping table on Disk files and cache some of them in quick_cache
*/
#ifndef __RUSTQC_DTABLE__
#define __RUSTQC_DTABLE__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rustqc_dtable {
    void* quick_cache;
    int directory_suffix;
}rustqc_dtable;

typedef struct RC_PhysicalAddr{
    uint8_t data[20];
}RC_PhysicalAddr;

// cache ppn
void* rustqc_build_index(uint64_t max_cache_entry, int ht_len, uint64_t* lvas, RC_PhysicalAddr* pas, uint64_t key_num);
int rustqc_get_pa(void* index, uint64_t lva, RC_PhysicalAddr* pa);
void get_status();

// cache ppn table

void* rustqc_dtable_build_index(int max_cache_entry, int ht_len, uint64_t* lvas, RC_PhysicalAddr* pas, uint64_t key_num);
int rustqc_dtable_get_pa(void* index, uint64_t lva, RC_PhysicalAddr* pa);
void rustqc_dtable_clean_local_files();

void test_rustqc();
#ifdef __cplusplus
}
#endif
#endif
