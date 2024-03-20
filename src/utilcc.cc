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
 * 
 * TOO SLOW
 */
size_t try_build_pthash(compact_pthash& pthash, pthash::build_configuration config, LVA* lvas, size_t l, size_t r){
    size_t num_key=r-l;
    printf("trying to build pthash with %lld keys\n", num_key);
    // create a vector with offset from lvas and try to build pthash
    while(1){
        auto ret=pthash.build_in_internal_memory(std::make_move_iterator(lvas+l), num_key, config);
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


size_t build_pthash(compact_pthash& pthash, pthash::build_configuration config, LVA* lvas, size_t l, size_t r, size_t* intercept, int max_table_size){
    size_t num_key=r-l;
    while(1){
        while(1){
            printf("building pthash with %lld keys. ", r-l);
            auto ret=pthash.build_in_internal_memory(std::make_move_iterator(lvas+l), r-l, config);
            if(ret.encoding_seconds==-1){
                config.alpha *= config.alpha;
            }else{
                break;
            }
        }
        printf("| table_size: %lld", pthash.table_size());
        if(pthash.table_size()>max_table_size){
            num_key*=config.alpha;
            continue;
        }
        *intercept=pthash.table_size();
        break;
    }
    printf("\n");
    return num_key;
}

void* build_index(LVA* lvas, PhysicalAddr* pas, size_t number, int left_epsilon, int right_epsilon, int SM_capacity, int DMA_capacity, int min_accurate_th){
    // --- build hybrid translation layer ---
    std::vector<LVA> segment_first_key;
    std::vector<LVA> segment_intercept;
    std::vector<bool> segment_accurate;
    std::vector<compact_pthash> pthashs;
    size_t global_intercept=0;
    pthash::build_configuration config;
    config.c=3.0;
    config.alpha=0.99f;
    config.minimal_output=true;
    config.verbose_output=false;
    config.seed=0x123456789;
    config.num_threads=1;
    config.dynamic_alpha=true;
    
    // approximate segment buffer
    LVA asb_first_key_offset=UINT64_MAX;
    size_t asb_num_key=0;

    int acuseg_keylb=(number)/(SM_capacity/Approximate_Segment_Length);
    acuseg_keylb=acuseg_keylb < min_accurate_th ? min_accurate_th : acuseg_keylb;
    int acuseg_keyub=(DMA_capacity/sizeof(PhysicalAddr));
    int apxseg_keylb=acuseg_keylb;
    int apxseg_keyub=(int)(acuseg_keyub * config.alpha);

    printf("==== accurate segment key number lower bound: %d, upper bound: %d, approximate segment key upper bound: %d\n", acuseg_keylb, acuseg_keyub, apxseg_keyub);
    // printf("====building index: number of keys: %lld, left epsilon: %d, right epsilon: %d, smart memory limit: %d, DMA memory limit: %d, accurate threshold: %d\n", number, left_epsilon, right_epsilon, SM_capacity, DMA_capacity, acuseg_keylb);

    size_t l=0,r;
    while(l<number){
        r=l+1;
        // iterate lvas, divide lvas into continuous segments. In each segment, each lva is 1 larger than the previous one.
        while(r<number && (r-l)<acuseg_keyub && lvas[r]==lvas[r-1]+1) r++;
        // printf("l: %lld, r: %lld\n", l, r);
        // only create a accurate segment when the number of keys is large enough
        if(r-l >= acuseg_keylb){
            if(asb_num_key>0){
                // printf("        asb_num_key: %lld\n", asb_num_key);
                if(asb_num_key<=1){
                    // create a accurate segment when onle one key is in asb
                    pthashs.push_back(compact_pthash());    //add a empty pthash
                    segment_first_key.push_back(lvas[asb_first_key_offset]);
                    segment_intercept.push_back(global_intercept);
                    global_intercept += 1;
                    segment_accurate.push_back(true);
                }else{
                    // create a approximate segment from current asb
                    compact_pthash pthash;
                    size_t intercept_add;
                    size_t added_keys=build_pthash(pthash, config, lvas, asb_first_key_offset, l, &intercept_add, acuseg_keyub);
                    pthashs.push_back(pthash);
                    segment_first_key.push_back(lvas[asb_first_key_offset]);
                    segment_intercept.push_back(global_intercept);
                    segment_accurate.push_back(false);
                    global_intercept += intercept_add;
                    
                    if(added_keys<(l-asb_first_key_offset) && added_keys<asb_num_key){
                        // In some case, the added keys is shrinked due to pthash building time threshold.
                        asb_first_key_offset+=added_keys;
                        asb_num_key-=added_keys;
                        compact_pthash pthash;
                        added_keys=build_pthash(pthash, config, lvas, asb_first_key_offset, l, &intercept_add, acuseg_keyub);
                        pthashs.push_back(pthash);
                        segment_first_key.push_back(lvas[asb_first_key_offset]);
                        segment_intercept.push_back(global_intercept);
                        segment_accurate.push_back(false);
                        global_intercept += intercept_add;
                    }
                }
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
            // printf("    add keys to asb\n");
            // otherwise, add them to asb
            asb_num_key+=r-l;
            asb_first_key_offset= l<asb_first_key_offset ? l : asb_first_key_offset;
            // printf("        asb_first_key_offset: %lld, asb_num_key: %lld\n", asb_first_key_offset, asb_num_key);
            // create approximate segment when current keys in asb is large enough
            while(asb_num_key>=apxseg_keyub){
                // printf("            create an approximate segment\n");
                // try to pack apxseg_keyub keys at a time
                compact_pthash pthash;
                size_t intercept_add;
                size_t added_keys=build_pthash(pthash, config, lvas, asb_first_key_offset, asb_first_key_offset+apxseg_keyub, &intercept_add, apxseg_keyub); 

                pthashs.push_back(pthash);
                segment_first_key.push_back(lvas[asb_first_key_offset]);
                segment_intercept.push_back(global_intercept);
                segment_accurate.push_back(false);
                global_intercept+=intercept_add;

                asb_first_key_offset+=added_keys;
                asb_num_key-=added_keys;
            }
        }
        l=r;
    }

    // create the last segment from asb
    while(asb_num_key>0){
        compact_pthash pthash;
        size_t intercept_add;
        size_t added_keys==build_pthash(pthash, config, lvas, asb_first_key_offset, number, &intercept_add, apxseg_keyub);
        pthashs.push_back(pthash);
        segment_first_key.push_back(lvas[asb_first_key_offset]);
        segment_intercept.push_back(global_intercept);
        segment_accurate.push_back(false);
        global_intercept += intercept_add;
        asb_first_key_offset+=added_keys;
        asb_num_key-=added_keys;
    }

    printf("==== build hybrid translation layer, number of segments: %d, table_size: %llu\n", pthashs.size(), global_intercept);

    // --- build inner index ---

}


// only create accurate segment when the number of keys in asb in large enough
void* build_index_less_segment(LVA* lvas, PhysicalAddr* pas, size_t number, int left_epsilon, int right_epsilon, int SM_capacity, int DMA_capacity, int min_accurate_th){
    // --- build hybrid translation layer ---
    std::vector<LVA> segment_first_key;
    std::vector<LVA> segment_intercept;
    std::vector<bool> segment_accurate;
    std::vector<compact_pthash> pthashs;
    size_t global_intercept=0;
    pthash::build_configuration config;
    config.c=3.0;
    config.alpha=0.99f;
    config.minimal_output=true;
    config.verbose_output=false;
    config.seed=0x123456789;
    config.num_threads=1;
    config.dynamic_alpha=true;
    
    // approximate segment buffer
    LVA asb_first_key_offset=UINT64_MAX;
    size_t asb_num_key=0;

    int acuseg_keylb=(number)/(SM_capacity/Approximate_Segment_Length);
    acuseg_keylb=acuseg_keylb < min_accurate_th ? min_accurate_th : acuseg_keylb;
    int acuseg_keyub=(DMA_capacity/sizeof(PhysicalAddr));
    int apxseg_keylb=acuseg_keylb;
    int apxseg_keyub=(int)(acuseg_keyub * config.alpha);

    printf("==== accurate segment key number lower bound: %d, upper bound: %d, approximate segment key upper bound: %d\n", acuseg_keylb, acuseg_keyub, apxseg_keyub);

    size_t l=0,r;
    while(l<number){
        r=l+1;
        while(r<number && (r-l)<acuseg_keyub && lvas[r]==lvas[r-1]+1) r++;
        // printf("l: %lld, r: %lld\n", l, r);
        // only create a accurate segment when the number of keys is large enough. 
        // Besides, the number of keys in asb should be more than apxseg_keylb if not zero
        if(r-l >= acuseg_keylb && (asb_num_key<=0 || asb_num_key>=apxseg_keylb)){
            // create a approximate segment from current asb
            if(asb_num_key>0){
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
            // printf("    add keys to asb\n");
            // otherwise, add them to asb
            asb_num_key+=r-l;
            asb_first_key_offset= l<asb_first_key_offset ? l : asb_first_key_offset;
            // printf("        asb_first_key_offset: %lld, asb_num_key: %lld\n", asb_first_key_offset, asb_num_key);
            // create approximate segment when current keys in asb is large enough
            while(asb_num_key>=apxseg_keyub){
                // printf("            create an approximate segment\n");
                // try to pack apxseg_keyub keys at a time
                compact_pthash pthash;
                size_t intercept_add=build_pthash(pthash, config, lvas, asb_first_key_offset, asb_first_key_offset+apxseg_keyub); 

                pthashs.push_back(pthash);
                segment_first_key.push_back(lvas[asb_first_key_offset]);
                segment_intercept.push_back(global_intercept);
                segment_accurate.push_back(false);
                global_intercept+=intercept_add;

                asb_first_key_offset+=apxseg_keyub;
                asb_num_key-=apxseg_keyub;
            }
        }
        l=r;
    }

    // create the last segment from asb
    if(asb_num_key>0){
        compact_pthash pthash;
        size_t intercept_add=build_pthash(pthash, config, lvas, asb_first_key_offset, number);
        pthashs.push_back(pthash);
        segment_first_key.push_back(lvas[asb_first_key_offset]);
        segment_intercept.push_back(global_intercept);
        segment_accurate.push_back(false);
        global_intercept += intercept_add;
    }

    printf("==== build hybrid translation layer, number of segments: %d, table_size: %llu\n", pthashs.size(), global_intercept);

    // --- build inner index ---

}
