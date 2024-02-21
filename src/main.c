#include "lruhash.h"
#include <stdio.h>
#include <string.h>

int main(){
    int ht_len=8;
    int ht_size=1<<(sizeof(uint32_t)*8-ht_len);
    int cache_size=ht_size*2; // 2 hash table allowd in cache

    void* index=build_lru_hash(cache_size, ht_len);
    uint32_t las[]={0x12345678, 0x22345678, 0x32345678, 0x42345678, 0x52345678, 0x62345678, 0x72345678, 0x82345678, 0x92345678, 0xa2345678, 0xb2345678, 0xc2345678, 0xd2345678, 0xe2345678, 0xf2345678};
    int num=sizeof(las)/sizeof(las[0]);
    
    PhysicalAddr pas[num];
    for(int i=0;i<num;i++){
        memset(pas[i].data,0,20);
        pas[i].data[0]=i;
    }
    bulk_load(index, las, pas,num);

    printf("----------------------\n");
    PhysicalAddr pa;
    for(int i=0;i<num;i++){
        get_pa(index, las[i], &pa);
        printf("key: %x, pa: %d\n", las[i], pa.data[0]);
    }

}