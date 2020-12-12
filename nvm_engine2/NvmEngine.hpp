#ifndef TAIR_CONTEST_KV_CONTEST_NVM_ENGINE_H_
#define TAIR_CONTEST_KV_CONTEST_NVM_ENGINE_H_

#include "include/db.hpp"
#include <mutex>
#include "util.h"
#include "Config.hpp"
#include "Recycle.h"

class NvmEngine : DB {
public:
    static Status CreateOrOpen(const std::string &name, DB **dbptr);
    NvmEngine(const std::string &name);
    Status Get(const Slice &key, std::string *value);
    Status Set(const Slice &key, const Slice &value);
    void get_file_write_meta(long &local_file_write_offset, long &local_file_write_limit);
    int _hash(char *str);
    void _buildIndexAndGc();
    void _preHeat();
    ~NvmEngine();

private:
    char *pmem_base;
    size_t _mapped_len;
    int _is_pmem;

    char *file_data;
    long *file_write_offset;

    spin_mutex mut_locks[LOCK_SIZE];
    spin_mutex offset_lock;

    Recycle recycles[16];
    int *hash_table;

    // | key:16 | file_pos:4 | pre_ptr:4 | block_size:1 version:1 val_len:2 |
    char *key_node; //一个key node占28个字节
    int key_counter;
    Counter key_counter_cnts[16];

    char *t_cache;
};

#endif