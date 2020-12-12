#ifndef TAIR_CONTEST_KV_METAINFO_H
#define TAIR_CONTEST_KV_METAINFO_H

//base
const long PMEM_SIZE = (1L << 30) * 64;
const int KEY_SIZE = 16;
const int MAX_VAL_SIZE = 1024;
const int AEP_WRITE_BUF_SIZE = 2 * 1024 * 1024;
const int MEM_KEY_ALLOCATOR_SIZE = 128 * 1024;

//mem_index
const long MEM_KEY_NUM = 1024 * 1024 * 225ll;   
const long MEM_NODE_SIZE = 25;   // key:13 | file_addr:4 | pre_ptr:4 | version:1 block_size:1 val_len:2 

const int MEM_NODE_KEY_OFFSET = 0;
const int MEM_NODE_FILE_ADDR_OFFSET = 13;
const int MEM_NODE_PRE_PTR_OFFSET = 17;
const int MEM_NODE_VERSION_OFFSET = 21;
const int MEM_NODE_BLOCK_SIZE_OFFSET = 22;
const int MEM_NODE_VAL_LEN_OFFSET = 23;

//bucket数组
const uint64_t INDEX_SIZE = 1 << 28;
const uint64_t INDEX_MASK = INDEX_SIZE - 1;

//分段锁
const int LOCK_SIZE = 1 << 18;
const int LOCK_MASK = LOCK_SIZE - 1;

//内存池
#define BIT_INTERVAL 945
const float GC_USAGE_FACTOR = 0.963;
const long GC_USAGE_THRESHOLD = (long)(PMEM_SIZE * GC_USAGE_FACTOR);
const int GC_JUMP_LEN = 64;
const int GC_JUMP_LIMIT = 336;

//cache相关
const long CACHE_BLOCK_LEN = 1042L;  //  1042 = 2(val_size) + 16(key) + 1024(value)
//other

#endif //TAIR_CONTEST_KV_METAINFO_H