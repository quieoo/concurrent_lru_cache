#include "rustqc_dtable.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>


extern void* build_quick_cache(uint64_t entry_num);
extern void quick_cache_insert(void* cache, uint64_t key, uint64_t value);
extern uint64_t quick_cache_query(void* cache, uint64_t key);

uint64_t cache_hit=0;
uint64_t cache_miss=0;
void* rustqc_build_index(int max_cache_entry, int directory_suffix, uint64_t* lvas, uint64_t* pas, uint64_t key_num){
    rustqc_dtable* dtable=(rustqc_dtable*)malloc(sizeof(rustqc_dtable));

    //build quick cache
    void* quick_cache= build_quick_cache(max_cache_entry);
    dtable->quick_cache=quick_cache;

    // //warm up cache
    // for(uint64_t i=0;i<key_num;i++){
    //     quick_cache_insert(quick_cache, lvas[i], pas[i]);
    // }

    dtable->directory_suffix=directory_suffix;
    
    // iterate lvas accroading to each directory suffix and create directory
    char file_name[50];
    uint64_t l=0;
    uint64_t r;
    int created_directory_num=0;
    while(l<=key_num){
        // get lvas for each directory
        int directory_id=lvas[l]>>directory_suffix;
        r=l+1;
        while(r<key_num && (lvas[r]>>directory_suffix)==directory_id){
            r++;
        }
        printf("directory_id: %d, l: %lu, r: %lu\n", directory_id, l, r);
        // if "directories" not exist, create it
        if(access("directories", 0)==-1){
            mkdir("directories", 0777);
        }

        // get directory file name
        sprintf(file_name, "directories/dir_%d", directory_id);
        // if file not exist, create it and pre-allocate
        if(access(file_name, 0)){
            int file_des = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            if(file_des!=-1){
                if(ftruncate(file_des, sizeof(uint64_t)*(1<<directory_suffix))){
                    printf("ftruncate error\n");
                    return NULL;
                }
            }else{
                printf("open error\n");
                return NULL;
            }
            close(file_des);
        }
        FILE* fp = fopen(file_name, "r+");
        if(fp!=NULL){
            for(uint64_t i=l;i<r;i++){
                // get file offset according to lva
                uint64_t offset=lvas[i]%(1<<directory_suffix);
                // write pa to the specific offset of directory file
                if(fseek(fp, offset*sizeof(uint64_t), SEEK_SET)==0){
                    size_t writen=fwrite(&(pas[i]), sizeof(uint64_t), 1, fp);
                }else{
                    printf("fseek error\n");
                    fclose(fp);
                    return NULL;
                }
            }
            fclose(fp);
        }else{
            printf("fopen error\n");
            return NULL;
        }
        l=r;
        created_directory_num++;
    }
    printf("created directory num: %d\n", created_directory_num);
    return dtable;
}
int rustqc_get_pa(void* index, uint64_t lva, uint64_t* pa){
    rustqc_dtable* dtable=(rustqc_dtable*)index;
    
    // check quick cache
    uint64_t value=quick_cache_query(dtable->quick_cache, lva);
    if(value!=0){
        *pa=value;
        ++cache_hit;
        return 0;
    }else{
        ++cache_miss;
        // get pa from directory
        int directory_id=lva>>(dtable->directory_suffix);
        char file_name[50];
        sprintf(file_name, "directories/dir_%d", directory_id);
        FILE* fp = fopen(file_name, "r");
        if(fp!=NULL){
            uint64_t offset=lva%(1<<(dtable->directory_suffix));
            if(fseek(fp, offset*sizeof(uint64_t), SEEK_SET)!=0){
                printf("fseek error\n");
                fclose(fp);
                return -1;
            }
            fread(pa, sizeof(uint64_t), 1, fp);
            fclose(fp);
            quick_cache_insert(dtable->quick_cache, lva, *pa);
            return 0;
        }else{
            printf("directory open error\n");
            return -1;
        }
    }
}

void get_status(){
    printf("cache_hit: %lu, cache_miss: %lu\n", cache_hit, cache_miss);
}


extern void* build_quick_table_cache(uint64_t entry_num);
extern void quick_table_cache_insert(void* cache, uint64_t table_id, RC_PhysicalAddr* table_data, uint64_t table_len);
extern void quick_table_cache_get(void* cache, uint64_t table_id, uint64_t table_offset, RC_PhysicalAddr* ret);

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

int pa_not_found(RC_PhysicalAddr pa){
    // if all bytes in pa are 0xff, return 1
    for(int i=0;i<20;i++){
        if(pa.data[i]!=0xff) return 0;
    }
    return 1;
}

int rustqc_dtable_get_pa(void* index, uint64_t lva, RC_PhysicalAddr* pa){
    rustqc_dtable* dtable=(rustqc_dtable*)index;
    uint64_t table_id=lva>>(dtable->directory_suffix);
    uint64_t table_offset=lva%(1<<(dtable->directory_suffix));
    RC_PhysicalAddr ret;
    quick_table_cache_get(dtable->quick_cache, table_id, table_offset,&ret);
    if(pa_not_found(ret)){
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