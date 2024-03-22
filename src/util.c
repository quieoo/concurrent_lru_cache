#include "clhash.h"

typedef struct lib_ftl_config{
    int left_epsilon;
    int right_epsilon;
    int sml_size_limit;
    int dma_size_limit;
    int min_accurate_th;
}lib_ftl_config;




// parse lib_ftl_config from a json file
void parse_lib_ftl_config(char* file, lib_ftl_config* config){
    FILE *f;
    char line[1024];
    f=fopen(file, "r");
    if(f==NULL){
        printf("can't open file %s\n", file);
        return;
    }
    while(fgets(line, 1024, f)!=NULL){
        sscanf(line, "left_epsilon: %d\n", &config->left_epsilon);
        sscanf(line, "right_epsilon: %d\n", &config->right_epsilon);
        sscanf(line, "sml_size_limit: %d\n", &config->sml_size_limit);
        sscanf(line, "dma_size_limit: %d\n", &config->dma_size_limit);
        sscanf(line, "min_accurate_th: %d\n", &config->min_accurate_th);
    }
    fclose(f);
    printf("============parse lib_ftl_config============\n");
    printf("left_epsilon: %d\n", config->left_epsilon);
    printf("right_epsilon: %d\n", config->right_epsilon);
    printf("sml_size_limit: %d\n", config->sml_size_limit);
    printf("dma_size_limit: %d\n", config->dma_size_limit);
    printf("min_accurate_th: %d\n", config->min_accurate_th);
    printf("=============================================\n");
}

void test_dlpam_on_host(LVA* lvas, PhysicalAddr* pas, int number){
    
    // --- build index ---
    struct lib_ftl_config config;
    parse_lib_ftl_config("lib_ftl_config.json", &config);
    void* index=build_index(lvas, pas, number, config.left_epsilon, config.right_epsilon, config.sml_size_limit, config.dma_size_limit, config.min_accurate_th);

    // --- offload index ---
    clpam cindex=get_clpam(index);
    uint64_t* ptr;

    // - offload Lindex -
    size_t Lindex_bytes=16;
    Lindex_bytes+=((clpam.num_levels * sizeof(uint32_t))+15)/16*16;
    Lindex_bytes+=clpam.num_segments * sizeof(cSegmentv2);
    uint8_t* Lindex=(uint8_t*)malloc(Lindex_bytes);
    ptr=(uint64_t*)Lindex;
    ptr[0]=clpam.first_key;
    uint64_t meta=0;    // the first 2 bytes stores num_level, second 2 bytes stores epsilon, the last 4 bytes stores num_segments
    meta|=((clpam.num_levels<<48)&(0xffff000000000000));
    meta|=(clpam.epsilon_recursive<<32)&(0x0000ffff00000000);
    meta|=(clpam.num_leaf_segment&(0x00000000ffffffff));
    ptr[1]=meta;
    // fit level_offsets, aligned to 16B
    ptr+=2;
    memcpy(ptr, clpam.level_offsets, (clpam.num_levels)*sizeof(uint32_t));
    // fit segments
    ptr+=(((clpam.num_levels)*sizeof(uint32_t)+15)/16)*2;
    memcpy(ptr, clpam.segments, (clpam.num_segments)*sizeof(cSegmentv2));


    // - offload htl_first_key -
    uint8_t* htl_first_key=(uint8_t*)malloc((clpam.num_leaf_segment) * sizeof(LVA));
    memcpy(htl_first_key, clpam.htl_first_key, (clpam.num_leaf_segment) * sizeof(LVA));

    // - htl_data -
    uint8_t* htl_data=(uint8_t*)malloc(clpam.num_leaf_segment * 16);
    uint64_t* ptr_htl_data=(uint64_t*)htl_data;

    // -- iterate and process each htl segment -- 
    for(int i=0;i<clpam.num_leaf_segment;i++){
        // get table data
        uint64_t intercept=clpam.htl_intercept[i];
        uint8_t accurate=intercept>>63;
        if(accurate)    intercept=intercept&(~(1ULL<<63));
        uint64_t next_intercept;
        if(i!=clpam.num_leaf_segment-1){
            next_intercept=clpam.htl_intercept[i+1];
            if(next_intercept>>63){
                next_intercept=next_intercept&(~(1ULL<<63));
            }
        }else{
            next_intercept=clpam.global_intercept;
        }
        uint64_t table_key_num=next_intercept-intercept;
        uint8_t* data_address=(uint8_t*)malloc(table_size);
        memcpy(data_address, (uint8_t*)(clpam.data+intercept), table_key_num*sizeof(PhysicalAddr));

        // construct htl data
        if(accurate){
            // accurate segment
            ptr_htl_data[2*i]=(uint64_t)data_address;
            ptr_htl_data[2*i+1]=intercept;
            // set the first bit of ptr_htl_data[2*i] to 1
            ptr_htl_data[2*i] |= (1ULL<<63);
        }else{
            // approximate segment

            // construct and offload pthash
            hash_table_structure_v3 hts;
            get_pthash_data_v5(&hts, index, i);
            uint64_t pthash_bytes=hts.compact_pilots_len*sizeof(uint64_t)+16;
            uint8_t* pthash_address=(uint8_t*)malloc(pthash_bytes);
            uint64_t* ptr_pthash=(uint64_t*)pthash_address;
            ptr_pthash[0]=(uint64_t)data_address;
            ptr_pthash[1]=intercept;
            memcpy(ptr_pthash+2, hts.compact_pilots, hts.compact_pilots_len*sizeof(uint64_t));

            uint64_t pthash_meta=0;
            pthash_meta|=((hts.m_bucket_size<<40)&0xffffff0000000000);
            pthash_meta|=((hts.m_table_size<<32)&0x000000ff00000000);
            pthash_meta|=((hts.m_table_size)&0x00000000ffffffff);

            ptr_htl_data[2*i]=(uint64_t)pthash_address;
            ptr_htl_data[2*i+1]=pthash_meta;
        }
    }


    // --- query ---
    for(uint64_t i=0;i<number;i++){
        LVA lva=lvas[i];

        // search Lindex
        uint64_t* ptr=(uint64_t*)Lindex;
        uint64_t first_key=ptr[0];
        uint32_t num_level=(uint32_t)(ptr[1]>>48);
        uint32_t ep=(uint32_t)(ptr[1]>>32)&(0x0000ffff);
        uint32_t num_htl_segment=(uint32_t)(ptr[1]);
        uint32_t *level_offsets=(uint32_t*)(ptr+2);
        cSegmentv2 *segments=(cSegmentv2*)(ptr+2+(((num_level)*sizeof(uint32_t)+15)/16)*2);

        LVA key=first_key<lva ? lva:first_key;
        uint32_t level_node_idx=level_offsets[num_level-1];
        while(num_level>0){
            int64_t pos = int64_t(segments[level_node_idx].slope) * (key - (segments[level_node_idx].key)) / UintFloat_Mask + segments[level_node_idx].intercept;
            if (pos < 0)
                pos = 0;
            if(pos > segments[level_node_idx + 1].intercept){
                pos = segments[level_node_idx + 1].intercept;
            }
            if(num_level<=1){
                level_node_idx = PGM_SUB_EPS(pos, ep+1);
                break;
            }
            uint32_t s_idx2=level_offsets[num_level - 2]+PGM_SUB_EPS(pos, ep+1);
            while(segments[s_idx2+1].key > key){
                ++s_idx2;
            }
            level_node_idx = s_idx2;
            --num_level;
        }
        // search on htl_first_key
        ptr=(uint64_t*)htl_first_key;
        while((level_node_idx+1)<num_htl_segment) && (ptr[level_node_idx+1]<=key){
            ++level_node_idx;
        }

        uint32_t htl_idx_o=get_htl_idx(index, lva);
        if(htl_idx_o != level_node_idx){
            printf(" error htl_idx_o: %d, level_node_idx: %d\n", htl_idx_o, level_node_idx);
            return;
        }

        uint64_t seg_first_key=ptr[level_node_idx];

        // htl process
        ptr=(uint64_t*)htl_data;
        uint64_t daddr, intercept;
        uint64_t in_seg_pos;
        if(ptr[level_node_idx*2]>>63){
            // accurate segment
            daddr=ptr[level_node_idx*2]&(~(1ULL<<63));
            intercept=ptr[level_node_idx*2+1];
            in_seg_pos=lva-seg_first_key+intercept;
        }else{
            // approximate segment
            paddr=ptr[level_node_idx*2];
            pmeta=ptr[level_node_idx*2+1];
            uint64_t* pdata=(uint64_t*)paddr;
            daddr=pdata[0];
            intercept=pdata[1];


            paddr+=2;
            uint64_t hash=c_hash64(lva, 0x123456789);
            uint64_t blk=hash%((pmeta>>40)&(0x0000000000ffffff));
            uint64_t width=(pmeta>>32)&(0x00000000000000ff);
            blk=blk*width;
            width=-(width==64) | ((((uint64_t)1)<<width)-1);
            paddr+=blk>>3;

            uint64_t *pilots_addr=(uint64_t*)paddr;
            uint64_t pos=*(pilots_addr);
            pos=pos>>(blk&7)&width;
            pos=c_hash(pos, 0x123456789);
            pos=(pos^hash)%(pmeta&(0x00000000ffffffff));

            in_seg_pos=pos;
        }

        uint64_t pa_addr=daddr+in_get_pos*sizeof(PhysicalAddr);

        PhysicalAddr pa=*((PhysicalAddr *)pa_addr);
        // verify
        int success;
        PhysicalAddr pa_o;
        get_pa_v5(index, lva, &pa_o);
        for(int i=0;i<sizeof(PhysicalAddr);i++){
            if(pa.data[i]!=pa_o.data[i]){
                success=0;
                break;
            }
        }
        if(!success){
            printf("verify failed\n");
            printf("expect: \n");
            for(int i=0;i<sizeof(PhysicalAddr);i++){
                printf("%02x ", pa_o.data[i]);
            }
            printf("\n");
            printf("actual: \n");
            for(int i=0;i<sizeof(PhysicalAddr);i++){
                printf("%02x ", pa.data[i]);
            }
            printf("\n");
            return;
        }
    }
}
