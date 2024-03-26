

#define ANSI_CURSOR_UP(n) "\033[" #n "A"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t LVA;
typedef struct PhysicalAddr{
    uint8_t data[20];
}PhysicalAddr;

typedef struct LVA2PA{
    LVA lva;
    PhysicalAddr pa;
}LVA2PA;

typedef struct clpam{
    LVA first_key;
    LVA last_key;
    uint32_t num_levels;
    uint32_t *level_offsets;
    uint32_t num_segments;
    cSegmentv2 *segments;
    uint32_t epsilon;

    uint32_t num_leaf_segment;
    LVA* htl_first_key;
    uint64_t *htl_intercept;
    uint64_t global_intercept;
    PhysicalAddr *data;

    // offload index
    void** pthash_addrs;
    void** data_tables;
    size_t* data_table_size;
    void* htl_data_addr;
    
}clpam;



uint32_t get_htl_idx(void *map, LVA la);
uint64_t get_pa_pos_v5(void* index, LVA lva);
void *build_index(LVA *lvas, PhysicalAddr *pas, size_t number, int left_epsilon, int right_epsilon, int SM_capacity, int DMA_capacity, int min_accurate_th);
size_t dlpam_Lindex_bytes(void* map);
void get_clpam(void*index, clpam* clpam);



#ifdef __cplusplus
}
#endif