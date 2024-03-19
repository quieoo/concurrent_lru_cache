//mock a pthash, implement all methods used in utilcc.cc

#include <cstdint>
#include <vector>

namespace pthash{
    struct build_timings{
        double encoding_seconds;
    };

    template<typename Hash, typename Table, bool Use_Single_Pthread>
    struct single_phf{
        template<typename Iterator>
        build_timings build_in_internal_memory(Iterator begin, size_t num_keys, build_configuration config){
            return {0.0};
        }
    };

    struct build_configuration{
        double alpha;
    };

    struct compact{
        size_t table_size;
    };

    struct murmurhash2_hash{
        uint64_t operator()(uint64_t key){
            return 0;
        }
    };
}