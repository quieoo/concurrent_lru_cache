#include "lruhash.hpp"
#include "lruhash.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <iostream>

std::ostream& operator<<(std::ostream& os, const PhysicalAddr& pa){
    for(int i=0;i<20;++i){
        os << (int)pa.data[i]<<" ";
    }
    return os;
}
std::istream& operator>>(std::istream& is, PhysicalAddr& pa){
    std::string temp;
    for(int i=0;i<20;++i){
        is >> temp;
        pa.data[i]=std::stoi(temp);
    }
    return is;
}

typedef LruHash<LA, PhysicalAddr> LruHash_FTL;

void* build_lru_hash(int cache_size, int ht_len){
    LruHash_FTL *ftl=new LruHash_FTL(cache_size, ht_len);
    return ftl;
}
void bulk_load(void* ptr, LA* las, PhysicalAddr* pas, int num){
    LruHash_FTL* ftl=(LruHash_FTL*)ptr;
    ftl->BulkLoad(las, pas, num);
}
int get_pa(void* ptr, LA la, PhysicalAddr* pa){
    LruHash_FTL* ftl=(LruHash_FTL*)ptr;
    return ftl->GetPA(la, pa);
}

#ifdef __cplusplus
}
#endif