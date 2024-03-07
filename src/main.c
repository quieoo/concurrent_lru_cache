#include "lruhash.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "rustqc_dtable.h"

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

    void* index=lru_hash_build(cache_size, ht_len);
    uint64_t las[]={0x12345678, 0x22345678, 0x32345678, 0x42345678, 0x52345678, 0x62345678, 0x72345678, 0x82345678, 0x92345678, 0xa2345678, 0xb2345678, 0xc2345678, 0xd2345678, 0xe2345678, 0xf2345678};
    int num=sizeof(las)/sizeof(las[0]);
    
    PhysicalAddress pas[num];
    for(int i=0;i<num;i++){
        memset(pas[i].data,0,20);
        pas[i].data[0]=i;
    }
    lru_hash_bulk_load(index, las, pas,num);

    printf("----------------------\n");
    PhysicalAddress pa;

    u64 s=get_nanotime();
    for(int i=0;i<num*1000;i++){
        lru_hash_get_pa(index, las[i%num], &pa);
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

    void* index=lru_hash_build(10000, 64-8);
    lru_hash_bulk_load(index, lpn, ppn, num);

    u64 s=get_nanotime();
    PhysicalAddress pa;
    for(uint64_t i=0;i<num;i++){
        if(i%1000==0)   printf("i: %lu\n", i);
        lru_hash_get_pa(index, lpn[i], &pa);
    }
    
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/num/1000);

    free(lpn);
    clean_local_files();
}

// extern void* build_quick_cache(__uint64_t entry_num);
// extern void quick_cache_insert(void* cache, __uint64_t key, __uint64_t value);
// extern __uint64_t quick_cache_query(void* cache, __uint64_t key);

// void quick_cache_test(){
//     void* quick_cache= build_quick_cache(3);
//     quick_cache_insert(quick_cache, 1, 1);
//     // printf("Result from quick_cache_query: %ld\n", quick_cache_query(quick_cache, 1));
//     quick_cache_insert(quick_cache, 2, 2);
//     quick_cache_insert(quick_cache, 3, 3);
//     quick_cache_query(quick_cache, 3);
//     quick_cache_insert(quick_cache, 4, 4);
    
//     for(int i=1;i<=5;i++){
//         printf("Result from quick_cache_query %d: %ld\n", i, quick_cache_query(quick_cache, i));
//     }
// }

int cmp(const void *a, const void *b){return *(uint64_t*)a-*(uint64_t*)b;}

// void quick_cache_trace_test(char* file){
//     uint64_t* lpn;
//     uint64_t num=0;
//     get_traces(file, &lpn, &num);
//     printf("got all trace lpn, num: %lu\n", num);

//     //sort lpn
//     qsort(lpn, num, sizeof(uint64_t), cmp);

//     //remove redundant lpn
//     uint64_t* lpn2=(uint64_t*)malloc(num*sizeof(uint64_t));
//     uint64_t key_num=0;
//     for(uint64_t i=0;i<num;i++){
//         if(i==0 || lpn[i]!=lpn[i-1]){
//             lpn2[key_num++]=lpn[i];
//         }
//     }
//     printf("get unique lpn, num: %lu\n", key_num);




//     // for(int i=0;i<num;i++){
//     //     printf("lpn: %lu\n", lpn[i]);
//     // }
//     uint64_t* ppn=(uint64_t*)malloc(key_num*sizeof(uint64_t));
//     for(uint64_t i=0;i<key_num;i++){
//         ppn[i]=lpn2[i]%4096;
//     }

//     void* quick_cache=build_quick_cache(key_num);


//     for(uint64_t i=0;i<key_num;i++){
//         quick_cache_insert(quick_cache, lpn2[i], ppn[i]);
//     }

//     u64 s=get_nanotime();
//     uint64_t pa;
//     for(uint64_t i=0;i<num;i++){
//         // if(i%1000==0)   printf("i: %lu\n", i);
//         pa=quick_cache_query(quick_cache, lpn[i]);
//         if(pa!=lpn[i]%4096){
//             printf("error. expected: %lu, actual: %lu\n", lpn[i]%4096, pa);
//             break;
//         }
//     }
    
//     u64 e=get_nanotime();
//     printf("time: %f us\n", (double)(e-s)/num/1000);

//     free(lpn);
// }

void rustqc_dtable_test(char* trace_file){
    uint64_t* lpn;
    uint64_t num=0;
    get_traces(trace_file, &lpn, &num);
    printf("got all trace lpn, num: %lu\n", num);

    uint64_t* origin_lpn=(uint64_t*)malloc(num*sizeof(uint64_t));
    for(uint64_t i=0;i<num;i++){
        origin_lpn[i]=lpn[i];
    }

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
    // cached_directory_num=2160*1024*1024/8 =  283,115,520
    int cached_directory_num=283115520;
    void* index=rustqc_build_index(cached_directory_num, 22, lpn2, ppn, key_num);
    if(index==NULL){
        printf("rustqc_build_index error\n");
        return;
    }


    u64 s=get_nanotime();
    uint64_t pa;
    for(uint64_t i=0;i<1000000;i++){
        if(i%1000==0)   printf("i: %lu\n", i);
        if(rustqc_get_pa(index, origin_lpn[i], &pa)){
            printf("rustqc_get_pa error\n");
            break;
        }
        if(pa!=origin_lpn[i]%4096){
            printf("error. expected: %lu, actual: %lu\n", origin_lpn[i]%4096, pa);
            break;
        }
    }
    
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/num/1000);

    get_status();

    free(lpn);
    free(lpn2);
    free(ppn);
}

void test_file_wr(){
    const char *filename = "data.bin";
    uint64_t data[] = {123456789, 987654321, 111222333, 444555666};
    size_t num_elements = sizeof(data) / sizeof(data[0]);

    // 打开文件用于二进制写入
    FILE *file = fopen(filename, "w");
    if(ftruncate(fileno(file), sizeof(uint64_t)*(1<<15))==-1){
        printf("ftruncate error\n");
        return;
    }

    if (file != NULL) {
        // 将数据写入文件
        fwrite(data, sizeof(uint64_t), num_elements, file);

        // 关闭文件
        fclose(file);
        printf("Data written to file successfully.\n");
    } else {
        perror("Error opening file");
    }

    uint64_t read_data;

    // 打开文件用于二进制读取
    file = fopen(filename, "r");

    if (file != NULL) {
        // 设置文件指针位置到第二个 uint64_t 数据的位置
        fseek(file, sizeof(uint64_t), SEEK_SET);

        // 从文件读取数据
        fread(&read_data, sizeof(uint64_t), 1, file);

        // 关闭文件
        fclose(file);

        // 输出读取的数据
        printf("Read data: %lu\n", read_data);
    } else {
        perror("Error opening file");
    }
}

RC_PhysicalAddr get_pa_from_u64(uint64_t val){
    RC_PhysicalAddr ret;
    // set the first 8 bytes of ret to val
    // from high-effective byte to low-effective byte
    for(int i=7;i>=0;i--){
        ret.data[7-i]=val>>i*8;
    }
    return ret;
}
uint64_t get_u64_from_pa(RC_PhysicalAddr pa){
    uint64_t ret=0;
    for(int i=0;i<8;i++){
        ret<<=8;
        ret+=pa.data[i];
    }
    return ret;
}


struct thread_arg{
    void* index;
    uint64_t* lvas;
    uint64_t num;
};

void* dtable_query_routine(void* arg){
    struct thread_arg* t_arg=(struct thread_arg*)arg;
    RC_PhysicalAddr pa;
    u64 s=get_nanotime();
    for(uint64_t i=0;i<t_arg->num;i++){
        if(rustqc_dtable_get_pa(t_arg->index, t_arg->lvas[i], &pa)){
            printf("rustqc_dtable_get_pa error\n");
            return NULL;
        }
        if(get_u64_from_pa(pa)!=t_arg->lvas[i]){
            printf("error. expected: %lu, actual: %lu\n", t_arg->lvas[i], get_u64_from_pa(pa));
            return NULL;
        }
    }
    u64 e=get_nanotime();
    printf("time: %f us\n", (double)(e-s)/t_arg->num/1000);
    return NULL;
}

void test_rustqu_dtable(char* trace_file){
    uint64_t* lpn;
    uint64_t num=0;
    get_traces(trace_file, &lpn, &num);
    printf("got all trace lpn, num: %lu\n", num);

    uint64_t* origin_lpn=(uint64_t*)malloc(num*sizeof(uint64_t));
    for(uint64_t i=0;i<num;i++){
        origin_lpn[i]=lpn[i];
    }

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
    RC_PhysicalAddr* ppn=(RC_PhysicalAddr*)malloc(key_num*sizeof(RC_PhysicalAddr));
    for(uint64_t i=0;i<key_num;i++){
        // set ppn to lpn, byte by byte
        ppn[i]=get_pa_from_u64(lpn2[i]);
    }
    int ht_len=20;
    // allowed_cache_table=2160*1024*1024/(1<<20 * sizeof(RC_PhysicalAddr))
    int allowd_cache_table=108;
    printf("key_num: %lu, ht_len: %d, allowed_cache_table: %d\n", key_num, ht_len, allowd_cache_table);
    rustqc_dtable* index=rustqc_dtable_build_index(allowd_cache_table, ht_len, lpn2, ppn, key_num);
    if(index==NULL){
        printf("rustqc_build_index error\n");
        return;
    }
    

    // create multiple threads to call rustqc_dtable_get_pa at the same time
    struct thread_arg arg;
    arg.index=index;
    arg.lvas=origin_lpn;
    arg.num=num;
    pthread_t threads[10];
    for(int i=0;i<10;i++){
        pthread_create(&threads[i], NULL, dtable_query_routine, &arg);
    }
    for(int i=0;i<10;i++){
        pthread_join(threads[i], NULL);
    }


    

    get_status();

    rustqc_dtable_clean_local_files();
}

int main(int argc, char** argv){
    // // 从参数中获得file
    char* file=argv[1];
    // // trace_test(file);
    // quick_cache_trace_test(argv[1]);
    // rustqc_dtable_test(file);
    test_rustqu_dtable(file);
    
    // quick_cache_test();

    return 0;

}