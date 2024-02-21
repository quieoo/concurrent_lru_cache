#pragma once

#include <iostream>
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>

#include "lru_cache/LruCache.h"

template <typename LA, typename PA>
class LruHash
{
public:
    int cache_size;     // allowed memory size of lru cache
    int ht_len;         // num of bits in LA used to be id of hash table
    int allowed_ht_num; // num of cached hash tables
    int total_access;
    int hits;


    lru_cache::LruCache<uint32_t, std::unordered_map<LA, PA>> cht=0; // cached hash table

    int get_ht_id(LA key)
    {
        return key >> (sizeof(LA) * 8 - ht_len);
    }

    /**
     * Constructor for LruHash class.
     *
     * @param cache_size_ Allowed Memory Consumption
     * @param ht_len_ Num of bits in LA used to be id of hash tables
     *
     */
    LruHash(int cache_size_, int ht_len_){
        total_access=0;
        hits=0;

        cache_size = cache_size_;
        ht_len = ht_len_;
        int lrucache_size = 1 << (sizeof(LA) * 8 - ht_len);
        int allowed_ht_num = cache_size < lrucache_size ? 0 : (cache_size / lrucache_size);
        if (allowed_ht_num < 1)
        {
            printf("warning: allocted cache_size(%f MB) is not enough for even one hash table(%f MB)\n", (float)cache_size_ / (1024 * 1024), (float)lrucache_size / (1024 * 1024));
        }
        else
        {
            printf("LruHash: allocted cache_size(%f MB) \nNumber of Cached Hash Table: %d(%f MB for each)\n", (float)cache_size_ / (1024 * 1024), allowed_ht_num, (float)lrucache_size / (1024 * 1024));

            cht = lru_cache::LruCache<uint32_t, std::unordered_map<LA, PA>>(allowed_ht_num, [](uint32_t ht_id, std::shared_ptr<std::unordered_map<LA, PA>> evit){
                // store evit hash table to a file named by ha
                std::string filename = "./hash_tables/" + std::to_string(ht_id);
                std::ofstream outFile(filename);
                for (const auto &pair : *evit)
                {
                    outFile << pair.first << " " << pair.second << "\n";
                }
                outFile.close();
                printf("evict hash table %x\n", ht_id);
                });
        }
    }

    void BulkLoad(LA *key, PA *value, int num)
    {
        std::unordered_map<int, std::unordered_map<LA, PA>> map;
        // Load current hash tables from files
        // Traverse all files under "./hash_tables/" directory
        DIR* dir=opendir("./hash_tables");
        if(dir==NULL){
            std::cout<<"Local Hash Tables not exist"<<std::endl;
            mkdir("./hash_tables", 0777);
        }else{
            dirent* entry;
            while((entry=readdir(dir))!=NULL){
                if(entry->d_type==DT_REG){
                    int ht_id = std::stoi(entry->d_name);
                    std::string filename = "./hash_tables/" + std::to_string(ht_id);
                    std::ifstream inFile(filename);
                    if (inFile){
                        std::unordered_map<LA, PA> restoredMap;
                        LA k;
                        PA v;
                        std::string line;
                        while(std::getline(inFile, line)){
                            std::istringstream iss(line);
                            iss >> k >> v;
                            restoredMap[k] = v;
                        }
                        inFile.close();
                        map[ht_id] = restoredMap;
                    }
                }
            }
            closedir(dir);
        }
        std::cout<<"Existsing hash tables: "<<map.size()<<std::endl;

        // Insert new kvs
        for (int i = 0; i < num; ++i)
        {
            int ht_id = get_ht_id(key[i]);
            if (map.count(ht_id)<=0)
            {
                map[ht_id] = std::unordered_map<LA, PA>();
            }
            map[ht_id][key[i]] = value[i];
        }
        std::cout<<"Current hash tables: "<<map.size()<<std::endl;

        // Build LruCache or save to files
        for(auto it=map.begin();it!=map.end();++it){
            int ht_id=it->first;
            std::unordered_map<LA, PA> ht_map=it->second;
            if(allowed_ht_num>=1){
                *cht[ht_id]=ht_map;
            }else{
                // save to file
                std::string filename = "./hash_tables/" + std::to_string(ht_id);
                std::ofstream outFile(filename);
                for (const auto &pair : ht_map)
                {
                    outFile << pair.first << " " << pair.second << "\n";
                }
                outFile.close();
            }
        }
    }

    int GetPA(LA key, PA *ret){
        total_access++;
        // Hit in LruCache
        int ht_id = get_ht_id(key);
        if (cht.contains(ht_id)){
            hits++;
            if ((*cht[ht_id]).count(key)<=0){
                return 0;
            }
            *ret = (*cht[ht_id])[key];
            return 1;
        }
        else{
            // Miss, load from file
            std::string filename = "./hash_tables/" + std::to_string(ht_id);
            std::ifstream inFile(filename);
            if (inFile){
                std::unordered_map<LA, PA> restoredMap;
                LA k;
                PA v;
                std::string line;
                while(std::getline(inFile, line)){
                    std::istringstream iss(line);
                    iss >> k >> v;
                    restoredMap[k] = v;
                    // std::cout<<"load: "<<k<<" "<<v<<std::endl;
                }
                inFile.close();
                if (restoredMap.count(key)<=0){
                    return 0;
                }
                *ret = restoredMap[key];
                if (allowed_ht_num >= 1){
                    *cht[ht_id] = restoredMap;
                }
                return 1;
            }
            else{
                return 0;
            }
        }
    }

    void Insert(LA key, PA value)
    {
        // get hash table
        std::unordered_map<LA, PA> restoredMap;
        if (allowed_ht_num >= 1)
        {
            if (cht.contains(get_ht_id(key)))
            {
                restoredMap = *cht[get_ht_id(key)];
            }
        }
        else
        {
            // restore from file if exist
            std::string filename = "./hash_tables/" + std::to_string(get_ht_id(key));
            std::ifstream inFile(filename);
            if (inFile)
            {
                LA k;
                PA v;
                while (inFile >> k >> v)
                {
                    restoredMap[k] = v;
                }
                inFile.close();
            }
        }
        // insert
        restoredMap[key] = value;

        // store hash table
        if (allowed_ht_num >= 1)
        {
            *cht[get_ht_id(key)] = restoredMap;
        }
        else
        {
            // save to file
            std::string filename = "./hash_tables/" + std::to_string(get_ht_id(key));
            std::ofstream outFile(filename);
            for (const auto &pair : restoredMap)
            {
                outFile << pair.first << " " << pair.second << "\n";
            }
            outFile.close();
        }
    }
};