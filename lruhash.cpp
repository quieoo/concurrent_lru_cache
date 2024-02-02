#include <iostream>
#include <unordered_map>
#include <fstream>
#include <string>
#include "include/lru_cache/LruCache.h"
using namespace lru_cache;

template<typename LA, typename PA>
class LruHash{
public:
    int cache_size; // max number of lru cache
    int ht_len;  // num of bits in LA used to be id of hash table
    LruCache<uint32_t, std::unordered_map<LA, PA>> cht; //cached hash table
    

    LruHash(int cache_size_, int ht_len_){
        cache_size = cache_size_;
        ht_len = ht_len_;
        cht=LruCache<uint32_t, std::unordered_map<LA, PA>>(cache_size, [](uint32_t ht_id, std::shared_ptr<std::unordered_map<LA, PA>> evit){
            // store evit hash table to a file named by ha
            std::string filename=std::string(ht_id);
            std::ofstream outFile(filename);
            for (const auto& pair : *evit) {
                outFile << pair.first << " " << pair.second << "\n";
            }
            outFile.close();
        });
    }
    

};