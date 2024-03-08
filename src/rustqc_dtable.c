#include "rustqc_dtable.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

int pa_not_found(RC_PhysicalAddr pa){
    // if all bytes in pa are 0xff, return 1
    for(int i=0;i<20;i++){
        if(pa.data[i]!=0xff) return 0;
    }
    return 1;
}
extern void* build_quick_cache(uint64_t entry_num);
extern void quick_cache_insert(void* cache, uint64_t key, RC_PhysicalAddr value);
extern RC_PhysicalAddr quick_cache_query(void* cache, uint64_t key);

uint64_t cache_hit=0;
uint64_t cache_miss=0;
void* rustqc_build_index(uint64_t max_cache_entry, int ht_len, uint64_t* lvas, RC_PhysicalAddr* pas, uint64_t key_num){
    rustqc_dtable* dtable=(rustqc_dtable*)malloc(sizeof(rustqc_dtable));

    //build quick cache
    void* quick_cache= build_quick_cache(max_cache_entry);
    dtable->quick_cache=quick_cache;
    dtable->directory_suffix=ht_len;
    uint64_t table_data_len=1<<ht_len;
    
    // iterate lvas accroading to each directory suffix and create directory
    char file_name[50];
    uint64_t l=0;
    uint64_t r;
    int table_num=0;
    while(l<key_num){
        // get lvas for each directory
        int table_id=lvas[l]>>ht_len;
        r=l+1;
        while(r<key_num && (lvas[r]>>ht_len)==table_id){
            r++;
        }
        printf("table-%lu, key number: %lu\n", table_id, r-l);

        RC_PhysicalAddr* table_data=(RC_PhysicalAddr*)malloc(sizeof(RC_PhysicalAddr)*table_data_len);
        for(uint64_t i=l;i<r;i++){
            uint64_t table_offset=lvas[i]%table_data_len;
            table_data[table_offset]=pas[i];
            quick_cache_insert(quick_cache, lvas[i], pas[i]);
        }

        // if "directories" not exist, create it
        if(access("tables", 0)==-1){
            mkdir("tables", 0777);
        }

        // get directory file name
        sprintf(file_name, "tables/tbl_%d", table_id);
        FILE* fp = fopen(file_name, "w");
        if(fp!=NULL){
            fwrite(table_data, sizeof(RC_PhysicalAddr), table_data_len, fp);
            fclose(fp);
        }else{
            printf("fopen error\n");
            return NULL;
        }

        // // insert table data into quick cache
        // for(uint64_t i=0;i<table_data_len;i++){
        //     uint64_t key=table_id*table_data_len+i;
        //     quick_cache_insert(quick_cache, key, table_data[i]);
        // }

        l=r;
        table_num++;
    }
    printf("created table_num num: %d\n", table_num);
    return dtable;
}
int rustqc_get_pa(void* index, uint64_t lva, RC_PhysicalAddr* pa){
    rustqc_dtable* dtable=(rustqc_dtable*)index;

    RC_PhysicalAddr ret = quick_cache_query(dtable->quick_cache, lva);
    // check quick cache
    if(pa_not_found(ret)){
        uint64_t table_id=lva>>(dtable->directory_suffix);
        uint64_t table_offset=lva%(1<<(dtable->directory_suffix));
    
        ++cache_miss;
        char file_name[50];
        sprintf(file_name, "tables/tbl_%lu", table_id);
        FILE* fp = fopen(file_name, "r");
        if(fp!=NULL){
            // uint64_t table_data_len=1<<(dtable->directory_suffix);
            // RC_PhysicalAddr* table_data=(RC_PhysicalAddr*)malloc(sizeof(RC_PhysicalAddr)*table_data_len);
            // fread(table_data, sizeof(RC_PhysicalAddr), table_data_len, fp);
            // fclose(fp);
            // *pa=table_data[table_offset];
            // seek to offset
            fseek(fp, table_offset*sizeof(RC_PhysicalAddr), SEEK_SET);
            fread(pa, sizeof(RC_PhysicalAddr), 1, fp);
            fclose(fp);
            
            //insert to cache
            quick_cache_insert(dtable->quick_cache, lva, *pa);

            // // miss happens insert all table data to quick cache
            // for(uint64_t i=0;i<table_data_len;i++){
            //     uint64_t key=table_id*table_data_len+i;
            //     quick_cache_insert(dtable->quick_cache, key, table_data[i]);
            // }
            return 0;
        }else{
            printf("fopen error\n");
            return -1;
        }
    }

    *pa=ret;
    ++cache_hit;
    return 0;
}

void get_status(){
    printf("cache_hit: %lu, cache_miss: %lu\n", cache_hit, cache_miss);
}


extern void* build_quick_table_cache(uint64_t entry_num);
extern void quick_table_cache_insert(void* cache, uint64_t table_id, RC_PhysicalAddr* table_data, uint64_t table_len);
extern void quick_table_cache_get(void* cache, uint64_t table_id, uint64_t table_offset, RC_PhysicalAddr* ret);
extern void set_panic_handler(void (*panic_handler)(const uint8_t*));
extern void func2(void);
// Rust-compatible panic handler function
extern void panic_handler(const uint8_t *info) {
    printf("C Panic Handler: %s\n", info);
    // Optionally perform C-specific cleanup or logging operations here
}

void* rustqc_dtable_build_index(int max_cache_entry, int ht_len, uint64_t* lvas, RC_PhysicalAddr* pas, uint64_t key_num){
    rustqc_dtable* dtable=(rustqc_dtable*)malloc(sizeof(rustqc_dtable));
    dtable->quick_cache=build_quick_table_cache(max_cache_entry);
    dtable->directory_suffix=ht_len;
    uint64_t table_data_len=1<<ht_len;

    uint64_t l=0;
    uint64_t r;
    char file_name[50];
    int table_num=0;
    while(l<key_num){
        uint64_t table_id=lvas[l]>>ht_len;
        r=l+1;
        while(r<key_num && (lvas[r]>>ht_len)==table_id){
            r++;
        }
        printf("table-%lu, key number: %lu\n", table_id, r-l);
        RC_PhysicalAddr* table_data=(RC_PhysicalAddr*)malloc(sizeof(RC_PhysicalAddr)*table_data_len);
        for(uint64_t i=l;i<r;i++){
            uint64_t table_offset=lvas[i]%table_data_len;
            table_data[table_offset]=pas[i];
        }
        if(access("tables", 0)==-1){
            mkdir("tables", 0777);
        }
        sprintf(file_name, "tables/tbl_%lu", table_id);
        FILE* fp = fopen(file_name, "w");
        if(fp!=NULL){
            fwrite(table_data, sizeof(RC_PhysicalAddr), table_data_len, fp);
            fclose(fp);
        }else{
            printf("fopen error\n");
            return NULL;
        }
        quick_table_cache_insert(dtable->quick_cache, table_id, table_data, table_data_len);
        table_num++;
        l=r;
    }
    printf("table number: %d\n", table_num);
    return dtable;
}



int rustqc_dtable_get_pa(void* index, uint64_t lva, RC_PhysicalAddr* pa){
    rustqc_dtable* dtable=(rustqc_dtable*)index;
    uint64_t table_id=lva>>(dtable->directory_suffix);
    uint64_t table_offset=lva%(1<<(dtable->directory_suffix));
    RC_PhysicalAddr ret;
    
    quick_table_cache_get(dtable->quick_cache, table_id, table_offset,&ret);
    if(pa_not_found(ret)){
        // printf("table-%lu not found\n", table_id);
        cache_miss++;
        char file_name[50];
        sprintf(file_name, "tables/tbl_%lu", table_id);
        FILE* fp = fopen(file_name, "r");
        if(fp!=NULL){
            uint64_t table_data_len=1<<(dtable->directory_suffix);
            RC_PhysicalAddr* table_data=(RC_PhysicalAddr*)malloc(sizeof(RC_PhysicalAddr)*table_data_len);
            fread(table_data, sizeof(RC_PhysicalAddr), table_data_len, fp);
            fclose(fp);
            *pa=table_data[table_offset];
            quick_table_cache_insert(dtable->quick_cache, table_id, table_data, table_data_len);
            return 0;
        }else{
            printf("fopen error\n");
            return -1;
        }
    }
    cache_hit++;
    *pa=ret;
    return 0;
}

void deleteFilesInDir(const char *dirname) {
    DIR *dir;
    struct dirent *entry;
    char path[1000];

    dir = opendir(dirname);
    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name);
            if (entry->d_type == DT_DIR) {
                deleteFilesInDir(path); // Recursively delete subdirectories
                rmdir(path); // Remove the subdirectory
            } else {
                remove(path); // Delete the file
            }
        }
    }

    closedir(dir);
}

void rustqc_dtable_clean_local_files(){
    // delete all files under the "tables" directory
    char dirname[] = "tables";
    deleteFilesInDir(dirname);    
    rmdir(dirname);
}

void test_rustqc(){
    rustqc_dtable* dtable=build_quick_table_cache(10);
    uint64_t lva=1;
    RC_PhysicalAddr* pas=(RC_PhysicalAddr*)malloc(sizeof(RC_PhysicalAddr)*10);
    for(int i=0;i<10;i++){
        pas[i].data[0]=i;
    }

    quick_table_cache_insert(dtable, lva, pas, 10);

    RC_PhysicalAddr pa;
    for(int i=0;i<10;i++){
        quick_table_cache_get(dtable, lva, i, &pa);
        printf("pa: %u\n", pa.data[0]);
    }
    free(pas);
    free(dtable);
}