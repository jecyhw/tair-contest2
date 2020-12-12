## 1 赛题介绍

### 1.1 赛题

Tair是阿里云自研的云原生内存数据库，接口兼容开源Redis/Memcache。在阿里巴巴集团内和阿里云上提供了缓存服务和高性能存储服务，追求极致的性能和稳定性。全新英特尔® 傲腾™ 数据中心级持久内存重新定义了传统的架构在内存密集型工作模式，具有突破性的性能水平，同时具有持久化能力，Aliyun弹性计算服务首次（全球首家）在神龙裸金属服务器上引入傲腾持久内存，撘配阿里云官方提供的Linux操作系统镜像Aliyun Linux，深度优化完善支持，为客户提供安全、稳定、高性能的体验。本题结合Tair基于神龙非易失性内存增强型裸金属实例和Aliyun Linux操作系统，探索新介质和新软件系统上极致的持久化和性能。参赛者在充分认知Aep硬件特质特性的情况下设计最优的数据结构。

### 1.2 赛题描述

热点是阿里巴巴双十一洪峰流量下最重要的一个问题，双十一淘宝首页上的一件热门商品的访问TPS在千万级别。这样的热点Key，在分布式的场景下也只会路由到其中的一台服务器上。解决这个问题的方法有Tair目前提供的热点散列机制，同时在单节点能力上需要做更多的增强。

本题设计一个基于傲腾持久化内存(Aep)的KeyValue单机引擎，支持Set和Get的数据接口，同时对于热点访问具有良好的性能。

**语言限定**：C/C++

**初赛资源情况**：内存4G、持久化内存(Aep)74G 、cpu16核

**复赛资源情况**：内存8G（再加320M用户空间）、持久化内存(Aep)74G 、cpu16核

> 赛题详细见：https://code.aliyun.com/db_contest_2nd/tair-contest/tree/master </br>
> 比赛链接：https://tianchi.aliyun.com/competition/entrance/531820/information </br>
> 英特尔傲腾持久内存综述：https://tianchi.aliyun.com/course/video?liveId=41202

## 2 初赛

### 2.1 评测逻辑

引擎使用的内存和持久化内存限制在 4G Dram和 74G Aep，无持久化要求。

**评测分为两个阶段**
* 正确性评测（不计入耗时）：开启16个线程并发写入一定量KV对象，并验证读取和更新后的正确性
* 性能评测：16个线程并发调用48M个Key大小为16Bytes，Value大小为80Bytes的KV对象，接着以95：5的读写比例访问调用48M次。其中读访问具有热点的特征，大部分的读访问集中在少量的Key上面

### 2.2 赛题分析

* Set阶段
    * 只有少量update操作
    * Aep（74G）和内存（4G）大于总写入kv数据量（72G）
    * 无持久化要求

* Get阶段：
    * 读具有热点key特征
    * Get阶段的Set都是update操作
    * Get阶段计分是取的10轮中最慢的一次


### 2.3 方案

#### 2.3.1 架构

整个方案的架构如下图所示。

<img src="https://i.postimg.cc/zGwZQPnX/1.png" width="600px"/>

* Buffer池：Set阶段每个线程先写4M块buffer，buffer写满之后，再提交给异步线程落盘
* hash索引：对key进行hash分桶
* 缓存：Get阶段对读进行缓存


#### 2.3.2 数据结构

##### 1 索引和KV的数据结构

<img src="https://i.postimg.cc/VLnkYgj2/2.png" width="800px"/>

* 同一个hash桶内kv数据之间使用链表存储，insert使用头插法，时间复杂度为O(1)
* key和value都是定长，pre_ptr记录逻辑个数，4个字节就可表示
* 由于key是均匀分布的，所以使用的是key的前8个字节的低28位作为hash值

##### 2 缓存数据结构

<img src="https://i.postimg.cc/FzkwVPrv/3.jpg" width="800px"/>

bucket内slot使用完之后使用公共缓存池

### 2.4 方案效果
* 异步落盘线程数：5个
* 内存使用情况
    * hash索引：1G（ hash长度256M，key的数量768M，平均一个桶内key的数量为3）
    * 读缓存：16M
    * buffer池：128M，每个buffer大小4M
    * kv写缓存：3.12G
* 最优耗时：38.627s（Init阶段90ms，Set阶段24.5s，Get阶段不到14.1s）
* 最终排名：第1名
    <img src="https://i.postimg.cc/VNYQPBR9/4.png" width="600px"/>


### 2.5 其它优化

* Init阶段优化：
    * 多线程分段初始化hash索引
        ```cpp
        for (int i = 0, len = MEMSET_THREAD_NUM; i < len; i++) {
            threads[i] = std::thread(&NvmEngine::_thread_task, this, i, _data);
        }
        for (auto &t: threads) {
            t.join();
        }
        ```
        ```cpp
        void NvmEngine::_thread_task(int idx, const int *data) {
            for (int s = idx * BUCKET_BLOCK_SIZE, e = s + BUCKET_BLOCK_SIZE; s < e; s += INT_MEM_CPY_LEN) {
                memcpy(bucket_bases + s, data, INT_MEM_CPY_LEN * 4);
            }
        }
        ```
* Set阶段优化
    * update采用追加插入方式，Get阶段的Set使用更新方式
    * hash索引更新使用cas无锁操作
        ```cpp
        int old_first_node;
        do {
            old_first_node = _getFirstNode(hash);
        } while (!CAS(bucket_bases + hash, old_first_node, write_pos));
        
        *(int *) new_node_ptr = old_first_node;
        ```
    * 利用多余内存(3G+)来缓存头部写入KV
    
* Get阶段优化
    * Get操作val结果对象复用
    * 初赛没有持久化要求，所以该阶段的update操作更新缓存成功就不更新aep

## 3 复赛

### 3.1 评测逻辑

**评测分为三个阶段：**
* 正确性评测：16个线程并发写入KV对象（Key固定大小16bytes，Value大小范围在80-1024bytes），然后验证读取和更新后的正确性
* 持久化评测：模拟断电场景，验证写入数据断电恢复后不受影响。该阶段不提供日志
* 性能评测（计入成绩）：
    * Set阶段：16个线程并发调用24M次Set操作，并选择性读取验证
    * Get阶段：16个线程以75%：25%的读写比例调用24M次，其中读符合热点Key特征。该阶段有10轮测试，取最慢一次的结果作为成绩

**性能阶段的数据特点：**
* 会保证任意时刻数据的value部分长度和不超过50G
* 每个线程纯写入的24M次操作中，val分布如下
    |  写入val长度   | 占比  | 
    |  ----  | ----  |
    | 80-128bytes  | 55% |
    | 129-256bytes  | 25% |
    | 257-512bytes  | 15% |
    | 513-1024bytes  | 5% |
* 总数据量：75G左右
* Get阶段中的所有Set操作的Value长度均不超过128bytes并且全部是更新操作

**要求：**
* 对于Insert
    * 已经返回的key，要求断电后一定可以读到数据。
    * 尚未返回的key，要求数据写入做到原子性，在恢复后要么返回NotFound，要么返回正确的value。

* 对于Update
    * 已经返回的key，要求断电后一定可以读到新数据。
    * 尚未返回的key，要求更新做到原子性，在恢复后要么返回旧的value，要么返回新的value。

### 3.2 赛题分析

* Set阶段：
    * 要保证数据断电后不丢失，无法使用buffer来批量落盘，每次Set都需要刷盘才能保证断电后数据不丢失
    * Aep（64G）和内存（8G）的容量小于总写入KV数据量（75G），所有kv数据想要全部存储到Aep，需要有一定的回收机制
    * key的数量大概有220M
* Get阶段：
    * 读具有热点Key特征
    * Get阶段计分是是取的10轮中最慢的一次
* Aep保证8字节的原子性写
    * Aep以及内存保证按8字节对齐后小于等于8字节的写入是原子性的

### 3.3 初版方案

#### 3.3.1 架构

<img src="https://i.postimg.cc/HxxqVYFV/5.png" width="500px"/>

* Aep hash索引：用来对Key进行分桶，以及满足持久化要求
* hash索引缓存：对Aep hash索引进行缓存，读内存是快于读Aep
* 缓存：Get阶段对读进行缓存

#### 3.3.2 数据结构

##### 1 索引和KV的数据结构

<img src="https://i.postimg.cc/v8pmFk6W/6.png" width="1000px"/>

* 每个kv数据按照16字节对齐， Aep有64G，这样只要4个字节就能表示数据块地址
* 同一个hash桶内kv数据之间使用链表存储，insert使用头插法，时间复杂度就是O(1)
* 由于key是均匀分布的，所以使用的是key的前8个字节的低28位作为hash值
* val_ptr、block_size和val_len一起是8字节并且起始地址是按照8字节对齐，可以保证val更新符合持久化要求

Aep的分配策略是按照4M块进行分配的，每个线程写完一个4M之后再申请下一个4M块，如下图所示。

<img src="https://i.postimg.cc/QNBMzhV8/7.png" width="900px"/>

这样带来的好处有：
* 减小获取锁的次数：无需每次写都去获取锁
* 减少随机写：线程内基本顺序写，线程间相对顺序写

##### 2 回收池数据结构

<img src="https://i.postimg.cc/7bvdsgh2/8.png" width="800px"/>
对每种数据块大小维护一个栈。
* 数据块回收：找数据块大小对应的栈，入栈即可，时间复杂度为O(1)
* 数据块复用：从数据块大小对应的栈开始按照一定步长找不为空栈，然后出栈即可，时间复杂度为O(1)

##### 3 缓存数据结构

<img src="https://i.postimg.cc/ydbw9qzn/9.png" width="700px"/>

* 使用覆盖的方式解决hash冲突
* 读缓存需要先加分段锁，保证缓存并发读写安全

#### 3.3.3 set操作

set操作流程图如下所示。

<img src="https://i.postimg.cc/NjxGX0C4/10.png" width="900px"/>

> 每次Set操作先写数据再写索引：索引写入是8字节并且对齐，Aep可以保证原子写

**1、置换更新**

置换更新val数据结构如下图所示。

<img src="https://i.postimg.cc/Xv7jk0T0/11.png" width="900px"/>

> 每个线程对每种val数据块大小预留一块空间作为交换区（更新val小于等于更新前val数据块则可以使用置换更新）

置换更新示例图。

<img src="https://i.postimg.cc/hGySC5BQ/12.png" width="1000px"/>

> Set阶段置换更新接住了接近2/3的update操作，接住了基本所有Get阶段的Set操作。

**2、key插入操作**

key insert写入数据后需要更新文件索引以及文件索引缓存，如下图所示。

<img src="https://i.postimg.cc/50XfDS47/13.png" width="1200px"/>

**3、key更新操作**

key更新val写入数据后，需要同时更新val_ptr、block_size和val_len，具体如下图所示。

<img src="https://i.postimg.cc/90TCQXqy/14.png" width="1200px"/>

#### 3.3.4 get操作

get操作流程图如下所示。

<img src="https://i.postimg.cc/V62zw05Y/15.png" width="800px"/>

#### 3.3.5 方案效果

* 内存使用量：2371M
    * hash索引缓存：2G（hash长度256M>key的数量220M）
    * 缓存：260M
    * 回收池：64M
* 缓存命中率：98%
* hash效果
    * 第1层：67.2%
    * 第2层：24.6%
    * 第3层：6.6%
    * 第4层以上：1.6%

* 每次set需要写2次Aep：1次写Aep数据，1次写Aep索引 
* 最优耗时：55s（set阶段41s，get阶段14s）

### 3.4 优化版方案

<img src="https://i.postimg.cc/jdHRV5Tb/16.png" width="700px"/>

#### 3.4.1 架构

<img src="https://i.postimg.cc/GmRh9DKt/17.png" width="500px"/>

> 和初版方案不同点在于索引和KV数据存储

#### 3.4.2 索引和KV的数据结构

<img src="https://i.postimg.cc/mgVRD5hp/18.png" width="900px"/>

* Key的数量有220M，每个Key16字节，总共有不到3.5G的Key，内存有8个G，所以可以对所有Key进行缓存并且对建立内存key索引
* 内存key索引各数据项按照访问先后顺序组织，充分利用缓存行
* version从1开始，每次更新version+1
* 数据块每4个字节累加作为crc值

内存key索引分配策略和Aep分配策略类似，如下图所示。

<img src="https://i.postimg.cc/m2fT690j/19.png" width="900px"/>

每个线程增加Key索引之前获取长度为1024\*128的空间，写完之后再申请下一个1024\*128，减小锁的粒度。

##### 1 索引重建流程

程序初始化的时候，根据Aep分配策略以及存储KV的数据结构，重建索引，具体流程如下图所示。

<img src="https://i.postimg.cc/jSRtRbqJ/20.png" width="900px"/>

> 在索引重建过程中，把crc不相等以及版本号低的数据块进行回收

#### 3.4.3 优化效果

* 每次set只要写1次Aep，没有任何读Aep操作
* 每次Get读，没有命中缓存才有1次读val的Aep操作（key在内存都有，只需要读1次val）
* 内存使用量：7696M
    * hash索引：1G
    * key索引：6.2G
    * 缓存：260M
    * 回收池：64M
* 缓存命中率和hash效果同初版方案
* 最优耗时：35.8s（set阶段27.1s，get阶段8.7s）
* 最终排名：第3名
    <img src="https://i.postimg.cc/v86QzhSD/21.png" width="600px"/>

#### 3.4.5 其他优化

* 初始化时对Aep预写：耗时节省了7.8s左右
    ```cpp
    void NvmEngine::_preHeat() {
        long interval_len = 2 * 1024 * 1024L;
        long buffer_size = 64L;
        long pre_write_size = 64 * 1024 * 1024 * 1024L - buffer_size;
        char *buffer = (char *) malloc(buffer_size);
        for (long i = 0; i < pre_write_size; i += interval_len) {
            pmem_memcpy_nodrain(buffer, file_data + i, buffer_size);
            pmem_drain();
            pmem_memcpy_nodrain(file_data + i, buffer, buffer_size);
            pmem_drain();
        }
        free(buffer);
    }
    ```
* Get操作val结果对象复用：耗时节省了2s左右
    ```cpp
    if (value->length() == 0) {
        *value = std::string(value_offset, value_len);
    } else {
        if (is_first) {
            *value = std::string(value_offset, 1024);
            is_first = false;
        }
        *((long *) ((char *) value + 8)) = value_len;
        memcpy(&((*value)[0]), value_offset, value_len);
    }
    ```
* 全局回收池换成thread local回收池：耗时节省了1s左右
* Set和Get的mutex互斥锁换成spin自旋锁：耗时节省了100多ms
* 一维数组模拟二维数组，减少对象产生
* 控制Get阶段Set的稳定性：Get阶段的Set是有热点Key更新的特征并且基本全部满足置换更新的条件，所以可以让每次Set更新前后的偏移差保证在一定范围内，从而大大减少更新落盘的随机性


## 4 总结和感想

初赛没有持久化要求，我们的核心思路就是使用hash对key分桶建立索引之后，Set线程先批量写内存buffer再由异步线程负责落盘，Get阶段使用读缓存，然后对每个阶段各个细节进行优化，包括：充分利用内存和cpu缓存行优势(除了读缓存，将剩余的内存用来缓存kv)；锁优化(cas、spin自旋锁、单次批量分配)、测Aep的读写性能等。整个初赛还是比较顺利的。

复赛有持久化要求，没法对写入进行缓冲了，并且持久化阶段没有日志，写入的kv数据也远大于Aep的容量，使得复赛难度明显上升了一个层次。我们先在初赛的基础上简单先把所有阶段跑通，跑通之后在进行优化。
复赛初版我们采用了文件hash索引，导致一次Set必须要2次写入，不管我们怎么优化，耗时都在50s+，后面看到有队伍进了50s以内，我们开始对方案重新思考。
有次在测单线程Aep一次写入160B，和160B分拆成152B和8B2次写的耗时，发现拆成2次写的耗时是1次写的耗时的3倍，使得我们相信瓶颈就是1次Set需要2次写入。
所以我们就开始思考1次Set是不是1次写入就好了，最终有了我们优化版的方案：就是去掉文件hash索引，只使用内存索引并且缓存所有key，在初始化时根据数据重建索引，然后使用version和crc解决多版本和部分写问题。

本次赛题是一道和存储相关的题目，涉及CPU、内存、Aep（本质上还是文件IO操作），存储题的瓶颈通常是文件IO，这次赛题也不例外。

下面总结了一些关于CUP、内存、文件IO相关的常见优化手段。
* 算法要尽量无锁化：thread local变量、cas、分段锁、自旋锁、单次批量分配
* 充分使用内存当缓存：内存读写速度远远大于文件读写
* 利用CPU缓存行，避免伪共享
* 对象复用，减少对象产生，从而避免gc
* 对于文件读写：要避免随机读写、利用内存当缓冲区达到批量写、单次操作尽量少的IOPS、异步读写、使用Direct IO以及mmap

另外，结合答辩的情况，方案还有以下的优化空间。
* 64B对齐写入（特别遗憾的点）
* 对内存预写
* mm_prefetch优化

最后感谢阿里云及天池平台提供的比赛，感谢官方人员在比赛期间的答疑和帮助，祝阿里云及天池平台的发展越来越好。


## 5 附录

* 复赛代码：https://github.com/jecyhw/tair-contest2
> 平时不咋用c++，代码写的烂还请轻喷

#### 5.1 通用代码
1. 自旋锁
    ```cpp
    class spin_mutex {
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    public:
        spin_mutex() = default;
    
        spin_mutex(const spin_mutex &) = delete;
    
        spin_mutex &operator=(const spin_mutex &) = delete;
    
        void lock() {
            while (flag.test_and_set(std::memory_order_acquire));
        }
    
        void unlock() {
            flag.clear(std::memory_order_release);
        }
    };
    ```

#### 5.2 初赛部分代码
1. 初赛异步批量写入
    ```cpp
    int NvmEngine::_write(const long *key, const Slice &val, int hash, int tid, bool flush) {
        static thread_local char *buffer = nullptr;
        static thread_local char buffer_pos = flush_service->GetBufPos();
        static thread_local int buffer_write_key_cnt = 0;
        static thread_local int end_off = 0;
        static thread_local int count_down = 0;
        static thread_local int write_buf_cnt = 0;
    
        if (flush) {
            flush_service->AddFlushItem(
                    std::move(std::make_tuple(buffer_pos, end_off - MEM_KEYS_NUM - buffer_write_key_cnt)));
            buffer_write_key_cnt = 0;
    
            write_thread_done_time[tid] = getCurrentTime();
            return -1;
        }
    
        if (buffer_write_key_cnt == BUF_SEG_NODE_NUM) {
            flush_service->AddFlushItem(
                    std::move(std::make_tuple(buffer_pos, end_off - MEM_KEYS_NUM - buffer_write_key_cnt)));
            buffer_write_key_cnt = 0;
            buffer_pos = flush_service->GetBufPos();
        }
    
        if (count_down == 0) {
            end_off = tid * BUF_SEG_NODE_NUM + 16 * BUF_SEG_NODE_NUM * (write_buf_cnt++);
            count_down = BUF_SEG_NODE_NUM;
        }
    
        int write_pos = end_off++;
        count_down--;
    
        char *new_node_ptr;
        if (write_pos >= MEM_KEYS_NUM) {
            buffer = flush_service->GetBuf(buffer_pos);
            new_node_ptr = buffer + (buffer_write_key_cnt++) * NODE_SIZE;
        } else {
            new_node_ptr = data_mem_bases + write_pos * NODE_SIZE;
        }
    
        memcpy(new_node_ptr + 4, key, KEY_SIZE);
        memcpy(new_node_ptr + 20, val.data(), VAL_SIZE);
    
        int old_first_node;
        do {
            old_first_node = _getFirstNode(hash);
        } while (!CAS(bucket_bases + hash, old_first_node, write_pos));
    
        // int lock_idx = hash & MUT_SIZE_MASK;
    
        // muts[lock_idx].lock();
        // int old_first_node = _getFirstNode(hash);
        // _updateFirstNode(hash, write_pos);
        // muts[lock_idx].unlock();
    
        *(int *) new_node_ptr = old_first_node;
    
        return write_pos;
    }
    ```

2. 异步线程写入Aep
    ```cpp
    void Consume(){
        std::tuple<int, int> &&t = GetFlushItem();
        
        int buf_pos = std::get<0>(t);
        char *t_buf = GetBuf(buf_pos);
        int dest_pos = std::get<1>(t);
        // memcpy(dest_mem + dest_pos * 100L, t_buf, BUF_SEG_BYTE_SIZE);
        pmem_memcpy(dest_mem + dest_pos * 100L, t_buf, BUF_SEG_BYTE_SIZE, PMEM_F_MEM_NODRAIN);
        AddBufPos(buf_pos);
    }
    ```

#### 5.3 复赛部分代码
1. 16字节对齐
    ```cpp
    inline int align16(int size) {
        int t = size & 15;
        if (t == 0 ) { 
            return size;
        }
    
        return size - t + 16;
    }
    ```

2. 回收池
    ```cpp
    long Get(int pos) {
        if (recycle_pos[pos] == 0) {
            return 0;
        }
        return real_offset(recycles[pos][--recycle_pos[pos]]);
    }
    
    bool Set(int pos, int node_offset) {
        if (recycle_pos[pos] < recycles_len[pos]) {
            recycles[pos][recycle_pos[pos]++] = node_offset;
            count++;
            return true;
        }
        return false;
    }
    
    int recycle_pos[BIT_INTERVAL];
    int recycles_len[BIT_INTERVAL];
    int *recycles[BIT_INTERVAL];
    int count = 0;
    ```

3. 置换更新
    ```cpp
    //初始化临时缓冲记录，用于置换更新
    for (int i = 0; i < BIT_INTERVAL; i += 16) {
        tmp_store[i] = relative_offset(local_file_write_offset);
        *version = 0;
        *block_size = relative_offset_char(80 + i + 32);
        pmem_memcpy_nodrain(file_data + local_file_write_offset, write_buffer, 8);
        pmem_drain();
    
        local_file_write_offset += (80 + i + 32);//16字节对齐
    }
    ```
    ```cpp
    if (old_block_size >= align16_block_size) { //等块置换更新
    
        //利用临时缓冲记录进行置换更新
        *version = *(key_node_addr + MEM_NODE_VERSION_OFFSET) + 1;
        *block_size = relative_block_size;
        *crc = crc16(crc, write_value_size);

        pmem_memcpy_nodrain(file_data + real_offset(tmp_file_addr), write_buffer, write_value_size);
        pmem_drain();

        //更新内存
        *tmp_store_ptr = *key_file_addr; //回收老的位置
        *key_file_addr = tmp_file_addr; //更新file_addr
        *(key_node_addr + MEM_NODE_VERSION_OFFSET) = (char)(*(key_node_addr + MEM_NODE_VERSION_OFFSET) + 1);  //更新version
        *((short *)(key_node_addr + MEM_NODE_VAL_LEN_OFFSET)) = (short)value_size; //更新value_size
        
        return Ok;
    }
    ```

4. 索引和内存池重建
    ```cpp
    void NvmEngine::_buildIndexAndGc() {
    // |crc:2|version:1|block_size:1|val_len:2|key:16|value|
        char *buffer = (char *) malloc(24 + 1024);
        short *crc = (short *)buffer;// 16位crc校验值
        char *version = buffer + 2; //1字节版本号
        char *block_size = buffer + 3; //1字节块大小
        short *val_len = (short *)(buffer + 4); //2字节val长度
        char *key_store = (char *) (buffer + 6); //16字节key
        char *suffix_key = key_store + 3;
    
        long file_write_pos = *file_write_offset;
    
        int bit_cnts[BIT_INTERVAL];
        memset(bit_cnts, 0, sizeof(int) * BIT_INTERVAL);
    
        for (long i = 16; i < file_write_pos; i += AEP_WRITE_BUF_SIZE) {
            long start = i, end = i + AEP_WRITE_BUF_SIZE;
            while(start < end) {
                char *addr = file_data + start;
                memcpy(buffer, addr, 22);
                if (*version <= 0) {//版本号不大于0跳出循环
                    if (*block_size <= 0) {
    
                        break;
                    } else {
                        //未使用的临时缓冲记录，可以回收
                        int pos = *block_size - 80 - 32;
                        int tid = bit_cnts[pos]++;
                        recycles[(tid & 15)].Set(pos, relative_offset(start));
    
                        start += real_offset_char(*block_size);
    
                        continue;
                    }
                }
    
                memcpy(buffer + 22, addr + 22, *val_len);
                if (crc16(crc, 22 + *val_len) == *crc) {
                    int hash = _hash(key_store);
                    bool is_new_key = true;
                    int key_node_pos = hash_table[hash];
                    while (key_node_pos != 0) {
                        // | key:16 | file_addr:4 | pre_ptr:4 | version:1 block_size:1  val_len:2 |
                        char *key_node_addr = key_node + key_node_pos * MEM_NODE_SIZE;
                        if(memcmp(key_node_addr, suffix_key, 13) == 0) {
                            // |crc:2|version:1|block_size:1|val_len:2|key:16|value|
                            if (*version >= *(key_node_addr + MEM_NODE_VERSION_OFFSET)) {//更新
                                *((int *)(key_node_addr + MEM_NODE_FILE_ADDR_OFFSET)) = relative_offset(start); //file_addr
                                *(key_node_addr + MEM_NODE_VERSION_OFFSET) = *version;
                                *(key_node_addr + MEM_NODE_BLOCK_SIZE_OFFSET) = *block_size;
                                *((short *)(key_node_addr + MEM_NODE_VAL_LEN_OFFSET)) = *val_len;
                            }else{
                                //旧记录可以回收
                                int pos = *block_size - 80 - 32;
                                int tid = bit_cnts[pos]++;
                                recycles[(tid & 15)].Set(pos, relative_offset(start));
                            }
                            is_new_key = false;
                            break;
                        }
                        key_node_pos = *((int *)(key_node_addr + MEM_NODE_PRE_PTR_OFFSET));
                    }
    
                    //新增
                    if (is_new_key) {
                        key_node_pos = key_counter++;
                        // | key:16 | file_addr:4 | pre_ptr:4 | version:1 block_size:1 val_len:2 |
                        char *key_node_addr = key_node + key_node_pos * MEM_NODE_SIZE;
                        //更新内存
                        memcpy(key_node_addr, suffix_key, 13);//key
                        *((int *)(key_node_addr + MEM_NODE_FILE_ADDR_OFFSET)) = relative_offset(start); //file_addr
                        *((int *)(key_node_addr + MEM_NODE_PRE_PTR_OFFSET)) = hash_table[hash]; //pre_ptr
                        *(key_node_addr + MEM_NODE_VERSION_OFFSET) = *version;
                        *(key_node_addr + MEM_NODE_BLOCK_SIZE_OFFSET) = *block_size;
                        *((short *)(key_node_addr + MEM_NODE_VAL_LEN_OFFSET)) = *val_len; //block_size、version、val_len
    
                        hash_table[hash] = key_node_pos;
                    }
                }else{
                    //损坏记录可以回收
                    int pos = *block_size - 80 - 32;
                    int tid = bit_cnts[pos]++;
                    recycles[(tid & 15)].Set(pos, relative_offset(start));
                }
                start += real_offset_char(*block_size);
            }
    
        }
    }
    ```
