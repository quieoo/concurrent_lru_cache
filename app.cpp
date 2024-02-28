#include <iostream>
#include <unordered_map>
#include <fstream>
#include <vector>

#include "lru_cache/LruCache.h"
#include "lruhash.hpp"

using namespace lru_cache;

void simple_cache_test() {
    LruCache<int, int> cache(3);
    *cache[0] = 0;
    *cache[1] = 1;
    *cache[2] = 2;
    *cache[3] = 3;
    *cache[4] = 4;
    *cache[5] = 5;

    // verify
    for(int i=0;i<6;++i){
        if(cache.contains(i)){
            std::cout << "cache[" << i << "] = " << *cache[i] << std::endl;
        }
    }


    LruCache<int, std::unordered_map<int,int>> cache2(2);
    std::unordered_map<int,int> map1;
    map1[0] = 0;
    
    std::unordered_map<int,int> map2;
    map2[0] = 1;
    
    std::unordered_map<int,int> map3;
    map3[0] = 2;
    
    *cache2[0] = map1;
    *cache2[1] = map2;
    *cache2[2] = map3;

    // verify
    for(int i=0;i<3;++i){
        if(cache2.contains(i)){
            std::cout << "cache2[" << i << "] = " << (*cache2[i])[0] << std::endl;
        }
    }
}

void simple_map_test(){
    std::unordered_map<int, std::string> myMap = {{1, "apple"}, {2, "banana"}, {3, "orange"}};

    // 保存到文件
    std::ofstream outFile("data.txt");
    for (const auto& pair : myMap) {
        outFile << pair.first << " " << pair.second << "\n";
    }
    outFile.close();

    // 从文件中重新读取和恢复
    std::ifstream inFile("data.txt");
    std::unordered_map<int, std::string> restoredMap;
    int key;
    std::string value;
    while (inFile >> key >> value) {
        restoredMap[key] = value;
    }
    inFile.close();

    // 恢复后的内容
    for (const auto& pair : restoredMap) {
        std::cout << pair.first << " " << pair.second << "\n";
    }
}

void lru_recall_test(){
    
    LruCache<int, std::unordered_map<int,int>> cache2(2, [](int key,std::shared_ptr<std::unordered_map<int, int>> evit){
        std::cout<<"evict table: "<<key<<std::endl;
        // 遍历并打印evit
        for (const auto& pair : *evit) {
            std::cout << pair.first << " " << pair.second << "\n";
        }
    });

    std::unordered_map<int,int> map1;
    map1[0] = 0;
    
    std::unordered_map<int,int> map2;
    map2[0] = 1;
    
    std::unordered_map<int,int> map3;
    map3[0] = 2;
    
    std::unordered_map<int,int> map4;
    map4[0] = 3;

    std::unordered_map<int,int> map5;
    map5[0] = 4;

    *cache2[0] = map1;
    *cache2[1] = map2;
    *cache2[2] = map3;
    *cache2[3] = map4;
    *cache2[4] = map5;
}

void lruhash_test(){

    int ht_len=8;
    int ht_size=1<<(sizeof(uint32_t)*8-ht_len);
    int cache_size=ht_size*2; // 2 hash table allowd in cache
    LruHash<uint32_t, uint64_t> lruhash(cache_size, ht_len);

    std::vector<uint32_t> keys = {0x12345678, 0x22345678, 0x32345678, 0x42345678, 0x52345678, 0x62345678, 0x72345678, 0x82345678, 0x92345678, 0xa2345678, 0xb2345678, 0xc2345678, 0xd2345678, 0xe2345678, 0xf2345678};
    std::vector<uint64_t> values = {0x12345678, 0x22345678, 0x32345678, 0x42345678, 0x52345678, 0x62345678, 0x72345678, 0x82345678, 0x92345678, 0xa2345678, 0xb2345678, 0xc2345678, 0xd2345678, 0xe2345678, 0xf2345678};

    lruhash.BulkLoad(&keys[0], &values[0], keys.size());

    for(int i=0;i<keys.size();++i){
        uint64_t ret;
        if(lruhash.GetPA(keys[i], &ret)){
            printf("get %x %lx\n", keys[i], ret);
        }
        lruhash.GetPA(keys[i], &ret);
    }

    printf("total access: %d, hit: %d\n", lruhash.total_access, lruhash.hits);
}


int main() {
    // lru_recall_test();
    lruhash_test();
    return 0;
}