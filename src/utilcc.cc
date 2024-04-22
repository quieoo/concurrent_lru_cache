#include "clhash.h"



typedef struct clhash_v5
{
    LVA first_key;
    LVA last_key;
    uint32_t num_levels;
    uint32_t *level_offsets;
    uint32_t num_segments;
    cSegmentv2 *segments;
    std::vector<compact_pthash> pthash_map;
    PhysicalAddr *data;
    uint32_t epsilon_recursive;
    uint32_t num_leaf_segment;
    LVA *htl_first_key;
    uint64_t *htl_intercept;
    
    uint64_t global_intercept;
    

}clhash_v5;

typedef struct cSegmentv2{
    LVA key;
    uint32_t slope;
    int32_t intercept;
}cSegmentv2;

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
size_t
try_build_pthash(compact_pthash &pthash, pthash::build_configuration config, LVA *lvas, size_t l, size_t r)
{
    size_t num_key = r - l;
    printf("trying to build pthash with %lld keys\n", num_key);
    // create a vector with offset from lvas and try to build pthash
    while (1)
    {
        auto ret = pthash.build_in_internal_memory(std::make_move_iterator(lvas + l), num_key, config);
        if (ret.encoding_seconds == -1)
        {
            --num_key;
            if (num_key <= 0)
            {
                printf("failed to build pthash\n");
                return 0;
            }
        }
        else
        {
            break;
        }
    }
    return num_key;
}



size_t build_pthash(compact_pthash &pthash, pthash::build_configuration config, LVA *lvas, size_t l, size_t r, size_t *intercept, int max_table_size)
{
    size_t num_key = r - l;
    while (1)
    {
        while (1)
        {
            // printf("num_key: %lld  ", num_key);
            auto ret = pthash.build_in_internal_memory(std::make_move_iterator(lvas + l), num_key, config);
            if (ret.encoding_seconds == -1)
            {
                config.alpha *= config.alpha;
            }
            else
            {
                break;
            }
        }
        // printf("| table_size: %lld.   ", pthash.table_size());
        if (pthash.table_size() > max_table_size)
        {
            num_key *= config.alpha;
            continue;
        }
        *intercept = pthash.table_size();
        break;
    }
    // printf("\n");
    return num_key;
}


uint32_t get_htl_idx(void *map, LVA la)
{
    clhash_v5 *clhash = (clhash_v5 *)map;
    LVA key = clhash->first_key < la ? la : clhash->first_key;
    uint32_t level = clhash->num_levels;
    uint32_t level_node_idx = (clhash->level_offsets)[level - 1];
    // search inner nodes
    while (level > 0)
    {
        // predict the pos of inner node in next level
        int64_t pos = int64_t(clhash->segments[level_node_idx].slope) * (key - (clhash->segments[level_node_idx].key)) / UintFloat_Mask + clhash->segments[level_node_idx].intercept;
        if (pos < 0)
            pos = 0;
        if(pos > clhash->segments[level_node_idx + 1].intercept){
            pos = clhash->segments[level_node_idx + 1].intercept;
        }
        if (level <= 1)
        {
            // reach the Hybrid Translation Layer
            level_node_idx = PGM_SUB_EPS(pos, clhash->epsilon_recursive+1);
            break;
        }
        uint32_t s_idx2=clhash->level_offsets[level - 2]+PGM_SUB_EPS(pos, clhash->epsilon_recursive+1);

        // find the last inner node in next level has a first_key <= key
        while ((clhash->segments[s_idx2 + 1].key) <= key)
        {
            ++s_idx2;
        }
        level_node_idx = (uint32_t)s_idx2;
        --level;
    }

    while ( (level_node_idx + 1 < clhash->num_leaf_segment)&&(clhash->htl_first_key[level_node_idx + 1] <= key))
    {
        ++level_node_idx;
    }

    return level_node_idx;
}

uint64_t get_pa_pos_v5(void* index, LVA lva){
    clhash_v5 *clhash = (clhash_v5 *)index;
    uint64_t pos;
    uint32_t htl_id=get_htl_idx(map, lva);
    uint64_t intercept=(clhash->htl_intercept)[htl_id];
    if(intercept>>63){
        pos=lva-clhash->htl_first_key[htl_id] + (intercept & ~(1ULL<<63));
    }else{
        pos=map->pthash_map[htl_id](lva)+intercept;
    }
    return pos;
}


void *build_index(LVA *lvas, PhysicalAddr *pas, size_t number, int left_epsilon, int right_epsilon, int SM_capacity, int DMA_capacity, int min_accurate_th)
{
    // --- build hybrid translation layer ---
    std::vector<LVA> segment_first_key;
    std::vector<LVA> segment_intercept;
    std::vector<int> segment_accurate;
    std::vector<compact_pthash> pthashs;
    size_t global_intercept = 0;
    pthash::build_configuration config;
    config.c = 3.0;
    config.alpha = 0.99f;
    config.minimal_output = true;
    config.verbose_output = false;
    config.seed = 0x123456789;
    config.num_threads = 1;
    config.dynamic_alpha = true;

    // approximate segment buffer
    LVA asb_first_key_offset = UINT64_MAX;
    size_t asb_num_key = 0;

    int acuseg_keylb = (number) / (SM_capacity / Approximate_Segment_Length);
    acuseg_keylb = acuseg_keylb < min_accurate_th ? min_accurate_th : acuseg_keylb;
    int acuseg_keyub = (DMA_capacity / sizeof(PhysicalAddr));
    int apxseg_keylb = acuseg_keylb;
    int apxseg_keyub = (int)(acuseg_keyub * config.alpha);

    printf(" ## Building Hybrid Translation Layer: accurate segment key number lower bound: %d, upper bound: %d, approximate segment key upper bound: %d\n", acuseg_keylb, acuseg_keyub, apxseg_keyub);
    // printf("====building index: number of keys: %lld, left epsilon: %d, right epsilon: %d, smart memory limit: %d, DMA memory limit: %d, accurate threshold: %d\n", number, left_epsilon, right_epsilon, SM_capacity, DMA_capacity, acuseg_keylb);

    size_t l = 0, r;
    while (l < number)
    {
        r = l + 1;
        // iterate lvas, divide lvas into continuous segments. In each segment, each lva is 1 larger than the previous one.
        while (r < number && (r - l) < acuseg_keyub && lvas[r] == lvas[r - 1] + 1)
            r++;

        float percent = (float)(l) / (float)number;
        printf("    \r%.1f%%\n", percent * 100.0f);
        std::cout << ANSI_CURSOR_UP(1);
        // printf("l: %lld, r: %lld\n", l, r);
        // only create a accurate segment when the number of keys is large enough

        /* TODO, One way to further decrease the number of segments:
        //  create a accurate segment when the number of keys in asb be more than apxseg_keylb if not zero
        */
        if (r - l >= acuseg_keylb)
        {
            if (asb_num_key > 0)
            {
                // printf("        asb_num_key: %lld\n", asb_num_key);
                if (asb_num_key <= 1)
                {
                    // create a accurate segment when onle one key is in asb
                    pthashs.push_back(compact_pthash()); // add a empty pthash
                    segment_first_key.push_back(lvas[asb_first_key_offset]);
                    segment_intercept.push_back(global_intercept);
                    global_intercept += 1;
                    segment_accurate.push_back(1);
                }
                else
                {
                    // create a approximate segment from current asb
                    compact_pthash pthash;
                    size_t intercept_add;
                    size_t added_keys = build_pthash(pthash, config, lvas, asb_first_key_offset, l, &intercept_add, acuseg_keyub);
                    pthashs.push_back(pthash);
                    segment_first_key.push_back(lvas[asb_first_key_offset]);
                    segment_intercept.push_back(global_intercept);
                    segment_accurate.push_back(0);
                    global_intercept += intercept_add;

                    if (added_keys < (l - asb_first_key_offset) && added_keys < asb_num_key)
                    {
                        // In some case, the added keys is shrinked due to pthash building time threshold.
                        asb_first_key_offset += added_keys;
                        asb_num_key -= added_keys;
                        compact_pthash pthash;
                        added_keys = build_pthash(pthash, config, lvas, asb_first_key_offset, l, &intercept_add, acuseg_keyub);
                        pthashs.push_back(pthash);
                        segment_first_key.push_back(lvas[asb_first_key_offset]);
                        segment_intercept.push_back(global_intercept);
                        segment_accurate.push_back(0);
                        global_intercept += intercept_add;
                    }
                }
                // clear asb
                asb_first_key_offset = UINT64_MAX;
                asb_num_key = 0;
            }
            // create a accurate segment
            pthashs.push_back(compact_pthash()); // add a empty pthash
            segment_first_key.push_back(lvas[l]);
            segment_intercept.push_back(global_intercept);
            global_intercept += r - l;
            segment_accurate.push_back(1);
        }
        else
        {
            // printf("    add keys to asb\n");
            // otherwise, add them to asb
            asb_num_key += r - l;
            asb_first_key_offset = l < asb_first_key_offset ? l : asb_first_key_offset;
            // printf("        asb_first_key_offset: %lld, asb_num_key: %lld\n", asb_first_key_offset, asb_num_key);
            // create approximate segment when current keys in asb is large enough
            while (asb_num_key >= apxseg_keyub)
            {
                // printf("            create an approximate segment\n");
                // try to pack apxseg_keyub keys at a time
                compact_pthash pthash;
                size_t intercept_add;
                size_t added_keys = build_pthash(pthash, config, lvas, asb_first_key_offset, asb_first_key_offset + apxseg_keyub, &intercept_add, apxseg_keyub);

                pthashs.push_back(pthash);
                segment_first_key.push_back(lvas[asb_first_key_offset]);
                segment_intercept.push_back(global_intercept);
                segment_accurate.push_back(0);
                global_intercept += intercept_add;

                asb_first_key_offset += added_keys;
                asb_num_key -= added_keys;
            }
        }
        l = r;
    }

    // create the last segment from asb
    while (asb_num_key > 0)
    {
        compact_pthash pthash;
        size_t intercept_add;
        size_t added_keys = build_pthash(pthash, config, lvas, asb_first_key_offset, number, &intercept_add, apxseg_keyub);
        pthashs.push_back(pthash);
        segment_first_key.push_back(lvas[asb_first_key_offset]);
        segment_intercept.push_back(global_intercept);
        segment_accurate.push_back(0);
        global_intercept += intercept_add;
        asb_first_key_offset += added_keys;
        asb_num_key -= added_keys;
    }

    printf("    Number of segments: %d, table_size: %llu\n", pthashs.size(), global_intercept);

    // --- build inner index ---
    printf(" ## Building inner index: min_epsilon=%d, max_epsilon=%d\n", left_epsilon, right_epsilon);

    std::vector<std::pair<LVA, uint64_t>> segs;
    segs.resize(segment_first_key.size());
    mspla::EPLAIndexMapV2<LVA, uint64_t, 128> clmap;
    int ans_er;

    for (uint64_t i = 0; i < segs.size(); ++i)
    {
        segs[i].first = segment_first_key[i];
        segs[i].second = i;
    }
    // check if segs is sorted
    for (uint64_t i = 1; i < segs.size(); ++i)
    {
        if (segs[i].first < segs[i - 1].first)
        {
            printf("error: segs is not sorted\n");
            return NULL;
        }
    }

    // binary search for a smallest epsion that make the smallest number of level of inner index
    mspla::EPLAIndexMapV2<LVA, uint64_t, 128> tmp_clmap(segs, segs.size(), right_epsilon, right_epsilon, 0);
    int min_level = tmp_clmap.levels_offsets.size() - 1;
    while (left_epsilon <= right_epsilon)
    {
        int mid = (left_epsilon + right_epsilon) / 2;
        mspla::EPLAIndexMapV2<LVA, uint64_t, 128> tmp_clmap(segs, segs.size(), mid, mid, 0);
        if (tmp_clmap.levels_offsets.size() - 1 == min_level)
        {
            ans_er = mid;
            clmap = tmp_clmap;
            right_epsilon = mid - 1;
        }
        else
        {
            left_epsilon = mid + 1;
        }
    }

    printf("    Found smallest epsilon: %d, minimum level of inner index: %d, number of nodes: %d\n", ans_er, min_level, clmap.segments.size());

    // --- build table
    printf(" ## Building table\n");
    clhash_v5 *map=new clhash_v5;
    compact_pthash ni_seg;
    compact_pthash pi_seg;
    map->pthash_map.push_back(ni_seg);
    map->pthash_map.push_back(pi_seg);

    for(int i=0;i<pthashs.size();++i){
        map->pthash_map.push_back(pthashs[i]);
    }
    map->epsilon_recursive=ans_er;
    map->first_key=clmap.first_key;
    map->last_key=lvas[number-1];
    map->num_levels=clmap.levels_offsets.size()-1;
    map->level_offsets=(uint32_t)malloc((map->num_levels)*sizeof(uint32_t));
    for(int i=0;i<map->num_levels;++i){
        map->level_offsets[i]=clmap.levels_offsets[i];
    }
    map->num_segments=clmap.segments.size();
    map->segments=(cSegmentv2*)malloc((map->num_segments)*sizeof(cSegmentv2));
    for(int i=0;i<map->num_segments;++i){
        map->segments[i].key=clmap.segments[i].key;
        map->segments[i].slope=(uint32_t)(UintFloat_Mask*(clmap.segments[i].slope));
        map->segments[i].intercept=clmap.segments[i].intercept;
    }
    map->num_leaf_segment= segment_intercept.size();
    map->htl_first_key=(LVA*)malloc((map->num_leaf_segment)*sizeof(LVA));
    map->htl_intercept=(uint64_t*)malloc((map->num_leaf_segment)*sizeof(uint64_t));
    for(int i=0;i<map->num_leaf_segment;++i){
        map->htl_first_key[i]=segment_first_key[i];
        map->htl_intercept[i]=segment_intercept[i];
        if(segment_accurate[i]){
            map->htl_intercept[i]|=1ULL<<63;
        }
    }
    map->data=(PhysicalAddr*)calloc((map->global_intercept)*sizeof(PhysicalAddr));

    for(size_t i=0;i<number;++i){
        uint64_t pos=get_pa_pos_v5(map, lvas[i]);
        memcpy(map->data[pos].data, pas[i].data, sizeof(PhysicalAddress));
    }

    return map;

}

void get_clpam(void*index, clpam* cindex){
    clhash_v5 *clhash = (clhash_v5 *)index;
    cindex->first_key=clhash->first_key;
    cindex->last_key=clhash->last_key;
    cindex->num_levels=clhash->num_levels;
    cindex->level_offsets=clhash->level_offsets;
    cindex->num_segments=clhash->num_segments;
    cindex->segments=clhash->segments;
    cindex->epsilon=clhash->epsilon_recursive;
    cindex->num_leaf_segment=clhash->num_leaf_segment;
    cindex->htl_first_key=clhash->htl_first_key;
    cindex->htl_intercept=clhash->htl_intercept;
    cindex->data=clhash->data;
    cindex->global_intercept=clhash->global_intercept;
}


void test_pthash_build(LVA* lvas, size_t n){
    pthash::build_configuration config;
    config.c = 3.0;
    config.alpha = 0.99f;
    config.minimal_output = true;
    config.verbose_output = false;
    config.seed = 0x123456789;
    config.num_threads = 1;
    config.dynamic_alpha = false;

    size_t in=100;
    while(true){
        
        if(in>n){
            break;
        }
        // collect time used to build
        struct timespec s, e;
        clock_gettime(CLOCK_REALTIME, &s);
        compact_pthash pthash;

        auto ret = pthash.build_in_internal_memory(std::make_move_iterator(lvas), in, config);
        
        clock_gettime(CLOCK_REALTIME, &e);
        unsigned long long used_ns=(e.tv_sec-s.tv_sec)*1000000000+(e.tv_nsec-s.tv_nsec);
        size_t table_size=pthash.table_size();

        printf("in_number: %lld, used time: %f us, table_size: %lld\n", in,(double)used_ns/1000, table_size);


        in*=2;
    }
}

size_t rebuild_pthash(void* index, LVA2PA* lva2pas, size_t num, int data_table_idx){
    clhash_v5 *clhash = (clhash_v5 *)index;
    compact_pthash pthash;
    pthash::build_configuration config;
    config.c = 3.0;
    config.alpha = 0.99f;
    config.minimal_output = true;
    config.verbose_output = false;
    config.seed = 0x123456789;
    config.num_threads = 1;
    config.dynamic_alpha = true;

    std::vector<LVA> lvas;
    for(size_t i=0;i<num;++i){
        lvas.push_back(lva2pas[i].lva);
    }
    
    while(1){
        auto ret = pthash.build_in_internal_memory(std::make_move_iterator(lvas.begin()), num, config);
        if(ret.encoding_seconds == -1){
            config.alpha *= config.alpha;
        }else{
            break;
        }
    }

    clhash->pthash_map[data_table_idx]=pthash;

    return pthash.table_size();
}

void rebuild_appseg_table(void* index, void* table, int data_table_idx, LVA2PA* lva2pas, size_t num){
    clhash_v5 *clhash = (clhash_v5 *)index;
    PhysicalAddr* table_ptr=(PhysicalAddr*)table;

    compact_pthash pthash=clhash->pthash_map[data_table_idx];
    for(size_t i=0;i<num;++i){
        LVA lva=lva2pas[i].lva;
        uint64_t pos=pthash(lva);
        memcpy(table_ptr+pos, lva2pas[i].pa.data, sizeof(PhysicalAddress));
    }
}