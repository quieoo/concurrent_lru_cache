#include <iostream>
#include <unordered_map>
#include <fstream>

#include "include/lru_cache/LruCache.h"
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

int main() {
    lru_recall_test();
    return 0;
}