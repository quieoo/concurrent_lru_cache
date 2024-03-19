
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

