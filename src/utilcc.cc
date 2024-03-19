#include <cstdint>
#include <vector>
#include "pthash_mock.hpp"

typedef uint64_t LVA;
typedef struct PhysicalAddr{
    uint8_t data[20];
}PhysicalAddr;
typedef pthash::single_phf<pthash::murmurhash2_hash, pthash::compact, false> compact_pthash;
#define Approximate_Segment_Size 24

/**
 * Generate a compact perfect hash table using the given configuration and range of elements from the input array, lvas.
 *
 * @param pthash The compact_pthash to be built
 * @param config The build configuration for the pthash
 * @param lvas Pointer to the array of elements
 * @param l Index indicating the start of the range
 * @param r Index indicating the end of the range
 *
 * @return The number of keys successfully processed to build the pthash
 */
size_t try_build_pthash(compact_pthash& pthash, pthash::build_configuration config, LVA* lvas, size_t l, size_t r){
    size_t num_key=r-l;
    // create a vector with offset from lvas and try to build pthash
    while(1){
        auto ret=pthash.build_in_internal_memory(std::begin(lvas+l), num_key, config);
        if(ret.encoding_seconds==-1){
            --num_key;
            if(num_key<=0){
                printf("failed to build pthash\n");
                return 0;
            }
        }else{
            break;
        }
    }
    return num_key;
}

/**
 * Builds a compact_pthash using the provided configuration and range of LVA elements.
 *
 * @param pthash the compact_pthash to build
 * @param config the build configuration for the pthash
 * @param lvas pointer to the array of LVA elements
 * @param l the starting index of the range
 * @param r the ending index of the range
 *
 * @return the table size of the compact_pthash after building
 */
size_t build_pthash(compact_pthash& pthash, pthash::build_configuration config, LVA* lvas, size_t l, size_t r){
    while(1){
        auto ret=pthash.build_in_internal_memory(std::begin(lvas+l), r-l, config);
        if(ret.encoding_seconds==-1){
            config.alpha *= config.alpha;
        }else{
            break;
        }
    }
    return f.table_size();
}

void* build_index(LVA* lvas, PhysicalAddr* pas, size_t number, int left_epsilon, int right_epsilon, int SM_capacity, int DMA_capacity, int min_accurate_th){
    // --- build hybrid translation layer ---
    std::vector<LVA> segment_first_key;
    std::vector<LVA> segment_intercept;
    std::vector<bool> segment_accurate;
    std::vector<compact_pthash> pthashs;
    size_t global_intercept=0;
    pthash::build_configuration config;
    
    // approximate segment buffer
    LVA asb_first_key_offset=UINT64_MAX;
    size_t asb_num_key=0;

    int acuseg_keylb=(number)/(SM_capacity/Approximate_Segment_Size);
    acuseg_keylb=acuseg_keylb < min_accurate_th ? min_accurate_th : acuseg_keylb;
    int acuseg_keyub=(DMA_capacity/sizeof(PhysicalAddr));
    int apxseg_keyub=(int)(acuseg_keyub * config.alpha);


    printf("====building index: number of keys: %lld, left epsilon: %d, right epsilon: %d, smart memory limit: %d, DMA memory limit: %d, accurate threshold: %d\n", number, left_epsilon, right_epsilon, SM_capacity, DMA_capacity, acuseg_keylb);

    size_t l=0,r;
    while(l<number){
        r=l+1;
        // iterate lvas, divide lvas into continuous segments. In each segment, each lva is 1 larger than the previous one.
        while(r<number && (r-l)<acuseg_keyub && lvas[r]==lvas[r-1]+1) r++;

        // only create a accurate segment when the number of keys is large enough
        if(r-l >= acuseg_keylb){
            if(asb_num_key>0){
                // create a approximate segment from current asb
                compact_pthash pthash;
                size_t intercept_add=build_pthash(pthash, config, lvas, asb_first_key_offset, l);
                pthashs.push_back(pthash);
                segment_first_key.push_back(lvas[asb_first_key_offset]);
                segment_intercept.push_back(global_intercept);
                segment_accurate.push_back(false);
                global_intercept += intercept_add;
                //clear asb
                asb_first_key_offset=UINT64_MAX;
                asb_num_key=0;
            }
            // create a accurate segment
            pthashs.push_back(compact_pthash());    //add a empty pthash
            segment_first_key.push_back(lvas[l]);
            segment_intercept.push_back(global_intercept);
            global_intercept += r-l;
            segment_accurate.push_back(true);
        }else{
            // otherwise, add them to asb
            asb_num_key+=r-l;
            asb_first_key_offset= l<asb_first_key_offset ? l : asb_first_key_offset;

            // create approximate segment when current keys in asb is large enough
            while(asb_num_key>=apxseg_keyub){
                // try to pack apxseg_keyub keys at a time
                compact_pthash pthash;
                size_t added_keys=try_build_pthash(pthash, config, lvas, asb_first_key_offset, apxseg_keyub);   //actually, added_keys < apxseg_keyub

                pthashs.push_back(pthash);
                segment_first_key.push_back(lvas[asb_first_key_offset]);
                segment_intercept.push_back(global_intercept);
                segment_accurate.push_back(false);
                global_intercept+=added_keys;

                asb_first_key_offset+=added_keys;
                asb_num_key-=added_keys;
            }
        }
        l=r;
    }
    printf("==== build hybrid translation layer, number of segments: %d, table_size: %llu\n", pthashs.size(), global_intercept);

    // --- build inner index ---
    
}
