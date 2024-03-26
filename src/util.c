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

// set the last 8 bytes of pa to la
void fill_pa(LVA* lvas, PhysicalAddr* pas, size_t number){
    for(size_t i=0;i<number;++i){
        memcpy(pas[i].data+8, &(lvas[i]), 8);
    }
}



size_t get_lva2pa_from_table(void* table_data, size_t table_data_size, LVA2PA* ret){
    PhysicalAddr* table=(PhysicalAddr*)table_data;
    size_t len=table_data_size/sizeof(PhysicalAddr);
    size_t lva_len=0;
    for(size_t i=0;i<len;++i){
        int valid=0;
        // check if all 20 bytes in data fields are 0
        for(int j=0;j<20;++j){
            if(table[i].data[j]!=0){
                valid=1;
                break;
            }
        }
        if(valid){
            // get lva2pa from the last 8 bytes
            memcpy(&(ret[lva_len].lva), table[i].data+8, 8);
            memcpy(&(ret[lva_len].pa), table[i].data, 20);
            ++lva_len;
        }
    }

    return lva_len;
}

int compare_lva2pa(const void* a, const void* b){
    LVA2PA* pa1=(LVA2PA*)a;
    LVA2PA* pa2=(LVA2PA*)b;
    return pa1->lva - pa2->lva;
}

/*
    handle new inseartion
    new_lvas: sorted new lvas. Make sure they are not in current clpam
*/
void dlpam_partial_reconstruct(void* index, clpam* cindex, LVA* new_lvas, PhysicalAddr* new_pas, size_t number, int dma_capacity){
    uint64_t l=0;
    uint64_t r=0;

    int htl_seg_id=-1;
    LVA htl_seg_first_key=cindex->first_key;
    LVA2PA* new_la2pas=(LVA2PA*)malloc(dma_capacity);
    while(l<number){
        // divide lvas to htl segments
        while(l<number && new_lvas[l]>htl_seg_first_key){
            ++htl_seg_id;
            if(htl_seg_id < cindex->num_leaf_segment){
                htl_seg_first_key=cindex->htl_first_key[htl_seg_id];
            }else{
                htl_seg_first_key=cindex->last_key;
            }
        }
        r=l+1;
        while(r<number && new_lvas[r]<=htl_seg_first_key){
            ++r;
        }
        
        printf("insert keys with lva range [%ld, %ld), keys[%llx, %llx), to htl segment %d, with first_key %llx\n", l, r, new_lvas[l], new_lvas[r-1], htl_seg_id, htl_seg_first_key);
        // insert lva pas from [l,r) to htl segment 'htl_seg_id': 
        // (0) get original table data, merge it with new lvas
        size_t data_table_idx;
        if(htl_seg_id==-1){
            data_table_idx=0;
        }else if(htl_seg_id >= cindex->num_leaf_segment){
            data_table_idx=1;
        }else{
            data_table_idx=2+htl_seg_id;
        }
        size_t new_la2pa_num=get_lva2pa_from_table(cindex->data_tables[data_table_idx], cindex->data_table_size[data_table_idx], new_la2pas);
        if(new_la2pa_num + r-l > (dma_capacity)/sizeof(PhysicalAddr)){
            printf("error: new_la2pa_num + r-l > (dma_capacity)/sizeof(PhysicalAddr)\n");
            return;
        }
        for(int i=0; i<r-l; ++i){
            LVA2PA inst;
            inst.lva=new_lvas[l+i];
            inst.pa=new_pas[l+i];
            new_la2pas[new_la2pa_num+i]=inst;
        }
        new_la2pa_num+=r-l;
        qsort(new_la2pas, new_la2pa_num, sizeof(LVA2PA), compare_lva2pa);

        //  (2) rebuild pthash
        size_t new_table_size=rebuild_pthash(index, new_la2pas, new_la2pa_num, data_table_idx)*sizeof(PhysicalAddr);
        if(new_table_size > dma_capacity){
            printf("error: new_table_size > dma_capacity\n");
            return;
        }
        hash_table_structures_v3 hts;
        get_pthash_data_v5(&hts, index, data_table_idx);

        // (3) offload new table and pthash
        PhysicalAddr* new_table=(PhysicalAddr*)malloc(new_table_size);
        rebuild_appseg_table(index, new_table, new_table_size, data_table_idx, new_la2pas, new_la2pa_num);
        uint64_t pthash_bytes=hts.compact_pilots_len*sizeof(uint64_t)+16;
        uint64_t* new_pthash=(uint64_t*)malloc(pthash_bytes);
        new_pthash[0]=(uint64_t)new_table;
        new_pthash[1]=0;
        memcpy(new_pthash+2, hts.compact_pilots, hts.compact_pilots_len*sizeof(uint64_t));
        uint64_t new_pthash_meta=0;
        new_pthash_meta |= ((hts.m_bucket_size<<40) & 0xffffff0000000000);
        new_pthash_meta |= ((hts.m_compact_pilot_width<<32) & 0x000000ff00000000);
        new_pthash_meta |= ((hts.m_table_size) & 0x00000000ffffffff);


        //  (4) lock htl segment by set the first bit of corresponding pthash
        uint64_t* pthash_data=(uint64_t*)(cindex.pthash_addrs[data_table_idx]);
        *pthash_data = *pthash_data | (1ULL<<63);

        //  (5) update htl segment
        uint64_t* htl_data=(uint64_t*)(cindex->htl_data_addr);
        htl_data[data_table_idx*2+1]=new_pthash_meta;
        htl_data[data_table_idx*2]=(uint64_t)new_pthash;

        //  (6) clean and update cindex        
        free(cindex->data_tables[data_table_idx]);
        free(cindex->pthash_addrs[data_table_idx]);
        cindex->data_tables[data_table_idx]=new_table;
        cindex->data_table_size[data_table_idx]=new_table_size;
        cindex->pthash_addrs[data_table_idx]=new_pthash;

        // move to next htl segment
        l=r;
        ++htl_seg_id;
        if(htl_seg_id < cindex->num_leaf_segment){
            htl_seg_first_key=cindex->htl_first_key[htl_seg_id];
        }else{
            htl_seg_first_key=cindex->last_key;
        }

    }
}

void test_dlpam_on_host(LVA* lvas, PhysicalAddr* pas, int number){
    
    int build_number=number*9/10;
    int update_number=number - build_number;
    fill_pa(lvas, pas, number);

    // --- build index ---
    struct lib_ftl_config config;
    parse_lib_ftl_config("lib_ftl_config.json", &config);
    void* index=build_index(lvas, pas, build_number, config.left_epsilon, config.right_epsilon, config.sml_size_limit, (config.dma_size_limit)/2, config.min_accurate_th);

    // --- offload index ---
    clpam cindex;
    get_clpam(index, &cindex);
    cindex.pthash_addrs=(void**)malloc((cindex.num_leaf_segment+2)*sizeof(void*));
    memset(cindex.pthash_addrs, 0, (cindex.num_leaf_segment+2)*sizeof(void*));
    cindex.data_tables=(void**)malloc((cindex.num_leaf_segment+2)*sizeof(void*));
    memset(cindex.data_tables, 0, (cindex.num_leaf_segment+2)*sizeof(void*));
    cindex.data_table_size=(size_t*)malloc((cindex.num_leaf_segment+2)*sizeof(size_t));
    memset(cindex.data_table_size, 0, (cindex.num_leaf_segment+2)*sizeof(size_t));

    uint64_t* ptr;

    // - offload Lindex -
    size_t Lindex_bytes=16;
    Lindex_bytes+=((cindex.num_levels * sizeof(uint32_t))+15)/16*16;
    Lindex_bytes+=cindex.num_segments * sizeof(cSegmentv2);
    uint8_t* Lindex=(uint8_t*)malloc(Lindex_bytes);
    ptr=(uint64_t*)Lindex;
    ptr[0]=cindex.first_key;
    uint64_t meta=0;    // the first 2 bytes stores num_level, second 2 bytes stores epsilon, the last 4 bytes stores num_segments
    meta|=((cindex.num_levels<<48)&(0xffff000000000000));
    meta|=(cindex.epsilon_recursive<<32)&(0x0000ffff00000000);
    meta|=(cindex.num_leaf_segment&(0x00000000ffffffff));
    ptr[1]=meta;
    // fit level_offsets, aligned to 16B
    ptr+=2;
    memcpy(ptr, cindex.level_offsets, (cindex.num_levels)*sizeof(uint32_t));
    // fit segments
    ptr+=(((cindex.num_levels)*sizeof(uint32_t)+15)/16)*2;
    memcpy(ptr, cindex.segments, (cindex.num_segments)*sizeof(cSegmentv2));


    // - offload htl_first_key -
    uint8_t* htl_first_key=(uint8_t*)malloc((cindex.num_leaf_segment) * sizeof(LVA));
    memcpy(htl_first_key, cindex.htl_first_key, (cindex.num_leaf_segment) * sizeof(LVA));

    // - htl_data -
    uint8_t* htl_data=(uint8_t*)malloc(cindex.num_leaf_segment * 16);
    cindex.htl_data_addr=htl_data;
    uint64_t* ptr_htl_data=(uint64_t*)htl_data;
    ptr_htl_data+=4;

    // -- iterate and process each htl segment -- 
    for(int i=0;i<cindex.num_leaf_segment;i++){
        // get table data
        uint64_t intercept=cindex.htl_intercept[i];
        uint8_t accurate=intercept>>63;
        if(accurate)    intercept=intercept&(~(1ULL<<63));
        uint64_t next_intercept;
        if(i!=cindex.num_leaf_segment-1){
            next_intercept=cindex.htl_intercept[i+1];
            if(next_intercept>>63){
                next_intercept=next_intercept&(~(1ULL<<63));
            }
        }else{
            next_intercept=cindex.global_intercept;
        }
        uint64_t table_key_num=next_intercept-intercept;
        void* data_address=malloc(table_size);
        memcpy(data_address, (uint8_t*)(cindex.data+intercept), table_key_num*sizeof(PhysicalAddr));
        cindex.data_tables[i+2]=data_address;
        cindex.data_table_size[i+2]=table_key_num*sizeof(PhysicalAddr);

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
            get_pthash_data_v5(&hts, index, i+2);
            uint64_t pthash_bytes=hts.compact_pilots_len*sizeof(uint64_t)+16;
            void* pthash_address=malloc(pthash_bytes);
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

            cindex.pthash_addrs[i+2]=pthash_address;
        }
    }


    // --- query ---
    for(uint64_t i=0;i<build_number;i++){
        float percent=(float)i/(float)build_number*100;
        printf("\r%d%%", (int)percent);
        

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
        PhysicalAddr pa_o=pas[i];
        // get_pa_v5(index, lva, &pa_o);
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


    printf(" test partial update\n");
    dlpam_partial_reconstruct(index, &cindex, lvas+build_number, pas+build_number, update_number, dma_capacity);
    for(uint64_t i=0;i<update_number;i++){
        LVA lva=lvas[build_number+i];
        // query updated lva2pas
    }

    printf(" clean\n");
    for(int i=0;i<cindex.num_leaf_segment+2;i++){
        if(!(cindex.data_tables[i]==NULL))
            free(cindex.data_tables[i]);
        if(!(cindex.pthash_addrs[i]==NULL)){
            free(cindex.pthash_addrs[i]);
        }
    }
    free(cindex.data_tables);
    free(cindex.pthash_addrs);
}



