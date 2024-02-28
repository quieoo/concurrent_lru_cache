#include "lruhash.hpp"
#include "lruhash.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <iostream>

std::ostream& operator<<(std::ostream& os, const PhysicalAddress& pa){
    for(int i=0;i<20;++i){
        os << (int)pa.data[i]<<" ";
    }
    return os;
}
std::istream& operator>>(std::istream& is, PhysicalAddress& pa){
    std::string temp;
    for(int i=0;i<20;++i){
        is >> temp;
        pa.data[i]=std::stoi(temp);
    }
    return is;
}

typedef LruHash<LA, PhysicalAddress> LruHash_FTL;

void* lru_hash_build(int cache_size, int ht_len){
    LruHash_FTL *ftl=new LruHash_FTL(cache_size, ht_len);
    return ftl;
}
void lru_hash_bulk_load(void* ptr, LA* las, PhysicalAddress* pas, int num){
    LruHash_FTL* ftl=(LruHash_FTL*)ptr;
    ftl->BulkLoad(las, pas, num);
}
int lru_hash_get_pa(void* ptr, LA la, PhysicalAddress* pa){
    LruHash_FTL* ftl=(LruHash_FTL*)ptr;
    return ftl->GetPA(la, pa);
}

#ifdef __cplusplus
}
#endif