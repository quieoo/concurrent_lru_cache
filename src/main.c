#include "lruhash.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

typedef unsigned long long u64;

unsigned long long get_nanotime(){
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec * 1000000000 + now.tv_nsec;
}

void simple_test(){
    int ht_len=8;
    int ht_size=1<<(sizeof(uint32_t)*8-ht_len);
    int cache_size=ht_size*16; // 2 hash table allowd in cache

    void* index=build_lru_hash(cache_size, ht_len);
    uint64_t las[]={0x12345678, 0x22345678, 0x32345678, 0x42345678, 0x52345678, 0x62345678, 0x72345678, 0x82345678, 0x92345678, 0xa2345678, 0xb2345678, 0xc2345678, 0xd2345678, 0xe2345678, 0xf2345678};
    int num=sizeof(las)/sizeof(las[0]);
    
    PhysicalAddress pas[num];
    for(int i=0;i<num;i++){
        memset(pas[i].data,0,20);
        pas[i].data[0]=i;
    }
    bulk_load(index, las, pas,num);

    printf("----------------------\n");
    PhysicalAddress pa;

    u64 s=get_nanotime();
    for(int i=0;i<num*1000;i++){
        get_pa(index, las[i%num], &pa);
        // sleep(1);
        // printf("key: %x, pa: %d\n", las[i], pa.data[0]);
    }
    
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/num/1000/1000);
}

void get_traces(char* file, uint64_t** lpn, uint64_t* num){
    FILE *f;
    char line[1024];
    f=fopen(file, "r");
    if(f==NULL){
        printf("can't open file %s\n", file);
        return;
    }
    uint64_t timestamp, offset, size, t0;
    char trace_name[100];
    char op[100];
    int trace_id;
    *num=0;
    while(fgets(line, 1024, f)!=NULL){
        sscanf(line, "%lu,%100[^,],%d,%100[^,],%lu,%lu,%lu\n", &timestamp,trace_name,&trace_id,op,&offset,&size,&t0);
        *num+=size/4096;
    }
    fclose(f);
    printf("num: %lu\n", *num);
    *lpn=(uint64_t*)malloc((*num)*sizeof(uint64_t));
    f=fopen(file, "r");
    if(f==NULL){
        printf("can't open file %s\n", file);
        return;
    }
    uint64_t ln=0;
    while(fgets(line, 1024, f)!=NULL){
        sscanf(line, "%lu,%100[^,],%d,%100[^,],%lu,%lu,%lu\n", &timestamp,trace_name,&trace_id,op,&offset,&size,&t0);
        // printf("timestamp: %lu, offset: %lu, size: %lu\n", timestamp, offset, size);
        for(int i=0;i<(size/4096);i++){
            (*lpn)[ln++]=offset/4096+i;
        }
    }
    fclose(f);
}

void clean_local_files(){
    char cmd[1024];
    sprintf(cmd, "rm -rf hash_tables/*");
    system(cmd);
}

void trace_test(char* file){
    uint64_t* lpn;
    uint64_t num=0;
    get_traces(file, &lpn, &num);
    printf("num: %lu\n", num);
    // for(int i=0;i<num;i++){
    //     printf("lpn: %lu\n", lpn[i]);
    // }
    PhysicalAddress* ppn=(PhysicalAddress*)malloc(num*sizeof(PhysicalAddress));
    for(uint64_t i=0;i<num;i++){
        ppn[i].data[0]=i;
    }

    void* index=build_lru_hash(10000, 64-8);
    bulk_load(index, lpn, ppn, num);

    u64 s=get_nanotime();
    PhysicalAddress pa;
    for(uint64_t i=0;i<num;i++){
        if(i%1000==0)   printf("i: %lu\n", i);
        get_pa(index, lpn[i], &pa);
    }
    
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/num/1000);

    free(lpn);
    clean_local_files();
}

extern void* build_quick_cache(__uint64_t entry_num);
extern void quick_cache_insert(void* cache, __uint64_t key, __uint64_t value);
extern __uint64_t quick_cache_query(void* cache, __uint64_t key);

void quick_cache_test(){
    void* quick_cache= build_quick_cache(3);
    quick_cache_insert(quick_cache, 1, 1);
    // printf("Result from quick_cache_query: %ld\n", quick_cache_query(quick_cache, 1));
    quick_cache_insert(quick_cache, 2, 2);
    quick_cache_insert(quick_cache, 3, 3);
    quick_cache_query(quick_cache, 3);
    quick_cache_insert(quick_cache, 4, 4);
    
    for(int i=1;i<=5;i++){
        printf("Result from quick_cache_query %d: %ld\n", i, quick_cache_query(quick_cache, i));
    }
}

int cmp(const void *a, const void *b){return *(uint64_t*)a-*(uint64_t*)b;}

void quick_cache_trace_test(char* file){
    uint64_t* lpn;
    uint64_t num=0;
    get_traces(file, &lpn, &num);
    printf("got all trace lpn, num: %lu\n", num);

    //sort lpn
    qsort(lpn, num, sizeof(uint64_t), cmp);

    //remove redundant lpn
    uint64_t* lpn2=(uint64_t*)malloc(num*sizeof(uint64_t));
    uint64_t key_num=0;
    for(uint64_t i=0;i<num;i++){
        if(i==0 || lpn[i]!=lpn[i-1]){
            lpn2[key_num++]=lpn[i];
        }
    }
    printf("get unique lpn, num: %lu\n", key_num);




    // for(int i=0;i<num;i++){
    //     printf("lpn: %lu\n", lpn[i]);
    // }
    uint64_t* ppn=(uint64_t*)malloc(key_num*sizeof(uint64_t));
    for(uint64_t i=0;i<key_num;i++){
        ppn[i]=lpn2[i]%4096;
    }

    void* quick_cache=build_quick_cache(key_num);


    for(uint64_t i=0;i<key_num;i++){
        quick_cache_insert(quick_cache, lpn2[i], ppn[i]);
    }

    u64 s=get_nanotime();
    uint64_t pa;
    for(uint64_t i=0;i<num;i++){
        // if(i%1000==0)   printf("i: %lu\n", i);
        pa=quick_cache_query(quick_cache, lpn[i]);
        if(pa!=lpn[i]%4096){
            printf("error. expected: %lu, actual: %lu\n", lpn[i]%4096, pa);
            break;
        }
    }
    
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/num/1000);

    free(lpn);
}

int main(int argc, char** argv){
    // // 从参数中获得file
    // char* file=argv[1];
    // // trace_test(file);
    // quick_cache_trace_test(argv[1]);
    quick_cache_test();
    return 0;

}