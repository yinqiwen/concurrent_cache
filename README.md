# concurrent_cache: 单机实现1~2亿ops吞吐的线程安全Hashmap&LRU/LFU/FIFO Cache

## 背景
本文主要用于探讨在单机情况下，读写安全的Hashmap&LRU/LFU/FIFO Cache实现性能极限，对于某些需要极高hashmap/cache读写性能的业务场景提供参考；类似的场景包括不限与：
- 机器学习的大规模特征存储
- 超大规模的图存储
- 搜广推系统的正排缓存

传统基于hashmap+list的cache解决方案面临三大核心挑战：
- 内存墙：常规哈希表内存访问模式导致40%以上CPU周期浪费在等待数据加载
- 锁竞争：全局锁架构下单核吞吐量不超过1~2M QPS
- 淘汰效率：LRU链表维护时延占据30%以上请求处理时间

## 核心设计原理
主要原理是基于[swisstables](https://abseil.io/about/design/swisstables)原理, 同时结合传统开链式寻址哈希表以及[hazard pointer](https://en.wikipedia.org/wiki/Hazard_pointer)，实现高性能的线程安全哈希表，同时支持近似LRU/LFU/FIFO淘汰策略。

### CPU Cache友好寻址结构
采用SwissTable的SIMD指令优化元数据布局，结合链式桶管理解决哈希冲突：
```cpp
template <typename KeyType, typename ValueType, typename Allocator>
struct Bucket {
  using Node = Node<KeyType, ValueType, Allocator>;
  using Slot = std::atomic<Node*>;
  static constexpr size_t kBucketSlotSize = 64; 
  std::atomic<uint8_t> tags[kBucketSlotSize]; //使用0xFE标记空node 
  Slot slots[kBucketSlotSize]; //实际数据指针
  std::atomic<Bucket*> overflow{nullptr};
  Bucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      tags[i] = kEmptyCtrl;
      slots[i] = nullptr;
    }
  }
};

// 节点为hazard pinter
template <typename KeyType, typename ValueType, typename Allocator>
class Node:public hazard_pointer{
  //...
};

```
![bucket](https://git.woa.com/qiyingwang/docs/raw/master/img/bucket.png "bucket")

`tags`为一个cacheline大小（64字节）的连续内存区域，用于标记对应slots里实际对象的uint8指纹，`slots`存放实际数据对象指针，`overflow`指向溢出下一个Bucket; 在支持avx2的机器上，`tags`的比较可以用两个avx2指令完成；在avx512机器上则可以一个比较指令完成，可以大大降低锁竞争，提高吞吐量。


对于`LRU`cache场景，bucket设计增加一个时间戳记录数组，也可以在几个指令内迅速定位到最早被访问的节点；
```cpp
template <typename KeyType, typename ValueType, typename Allocator>
struct LRUBucket:public Bucket<KeyType,ValueType,Allocator> {
  uint32_t access_ts[kBucketSlotSize]; //记录访问时间戳
  LRUBucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      access_ts[i] = std::numeric_limits<uint32_t>::max(); //保存为最大值
    }
  }
};
```
对于`LFU`cache场景，bucket除增加一个时间戳记录数组还需要一个lfu counter数组, 针对lfu counter数组也可以在1~2个指令定位到最小lfu的节点;
```cpp
template <typename KeyType, typename ValueType, typename Allocator>
struct LFUBucket:public Bucket<KeyType,ValueType,Allocator> {
  uint32_t access_ts[kBucketSlotSize]; //记录访问时间戳
  uint8_t lfu_counter[kBucketSlotSize];//记录近似LFU值
  LFUBucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      access_ts[i] = std::numeric_limits<uint32_t>::max(); //保存为最大值
      lfu_counter[i] = kLFUInitValue; // 5
    }
  }
};
```

`FIFO`cache场景则更简单，只需要一个`std::atomic<uint32_t>`的下标记录当前需要淘汰的节点下标；
```cpp
template <typename KeyType, typename ValueType, typename Allocator>
struct FIFOBucket:public Bucket<KeyType,ValueType,Allocator> {
   uint32_t create_ts[kBucketSlotSize]; //记录创建时间戳
};
```
### Hazard Pointer内存安全
采用无锁内存回收机制解决"ABA问题"：

- 读保护：线程通过Hazard Pointer注册正在访问的内存地址
- 延迟删除：删除操作仅标记引用计数，确保无活跃引用时回收
- 批量回收：每个线程维护私有回收队列，定期批量释放内存

### 查询（无锁Wait-free）

```cpp
iterator Find(Key k) {
  auto [bucket,tag] = GetBucket(k);
  SIMD扫描匹配tag的槽位
  遍历匹配的槽位
    Hazard Pointer保护slot指针
    if（匹配槽位key）
       返回查询成功
    else
       继续
}
```
- SIMD过滤：AVX2指令可以一次比较32个槽位的tag
- 无锁遍历：Hazard Pointer保证遍历过程中节点不会被释放
- 对于LRU Cache，查询后还需要一个额外的更新时间戳操作，并不需要如传统LRU实现更新链表顺序，因此查询操作是wait free的；    
- 对于LFU Cache，查询后需要更新时间戳和lfu counter，也是wait free的；     
- 对于FIFO Cache，则不需要任何其它操作（创建时间在写入时写入）；

本质上，以上Cache的实现都是局部Bucket内的LRU/LFU/FIFO，并非如传统实现一般是全局LRU/LFU/FIFO，可以避免并发读写过程对全局资源竞争操作；


### 写入（细粒度锁优化）

```cpp
void Insert(Key k, Value v) {
  auto [bucket,tag] = GetBucket(k);
  ScopedLock lock(&bucket->mutex); // 桶级自旋锁，仅锁写/删除操作，不阻塞读
  if (FindExistingSlot(k,tag)) {
    CAS更新现有节点; // Compare-and-Swap无锁更新
  } else {
    SIMD扫描空槽;
    if (找到空槽) 插入新节点;
    else 创建Overflow Bucket;
  }
  
  触发淘汰策略; // 按需淘汰旧数据
}
```

对于LRU/LFU/FIFO Cache场景，还可以进一步将实现改成lock free，仅需要将操作改成冲突导致的失败重试即可；

### 删除
删除操作比较简单，相比查找操作，增加的操作：
- 锁住bucket，不阻塞读操作
- 查到节点后，尝试将节点指针替换为null；替换成功则释放删除节点指针，并将tag改为kEmptyCtrl
```cpp
void Delete(Key k) {
  auto [bucket,tag] = GetBucket(k);
  ScopedLock lock(&bucket->mutex); // 桶级自旋锁，仅锁写/删除操作，不阻塞读
  if (FindExistingSlot(k,tag)) {
    CAS更新现有节点为null; // Compare-and-Swap无锁更新
    更新slot的tag为kEmptyCtrl;
    提前释放lock
    hazard pointer淘汰删除指针
  }
}
```
对于LRU/LFU/FIFO Cache场景，也可以将实现改成lock free，仅需要将操作改成冲突导致的失败重试即可；

### 高效淘汰策略

#### LRU淘汰
```cpp
void TryEvict(Bucket* bucket) {
  while（bucket当前size过大）{
     SIMD获取最小访问时间的slot
     尝试删除相应slot
  }
}
```
淘汰行为限制在一个bucket内选择而非全局，大量避免了并发冲突;

#### LFU淘汰
```cpp
void TryEvict(Bucket* bucket) {
  while（bucket当前size过大）{
     SIMD计算所有slot的lfu couter
     SIMD获取最小lfu counter的slot
     尝试删除相应slot
  }
}
```
淘汰行为限制在一个bucket内选择而非全局，大量避免了并发冲突;

#### FIFO淘汰
```cpp
void TryEvict(Bucket* bucket) {
  while（bucket当前size过大）{
     SIMD获取最小创建时间的slot
     尝试删除相应slot
  }
}
```
淘汰行为限制在一个bucket内选择而非全局，大量避免了并发冲突;


### 对比传统实现的限制和取舍
- HashMap
  - 初始bucket个数固定, 不能运行期安全的rehash增加/减少
  - rehash/resize需要在停止写的情况下执行
- LRU/LFU/FIFO Cache
  - 没有overflow bucket
    - 可能有hash冲突率高的情况下, 对应bucket淘汰过多数据
    - 没有overflow bucket, 也使得每个bucket数据大致相同, 读性能更为均衡无长尾访问现象
  - 淘汰实现基于局部bucket, 本质上是一种近似LRU/LFU/FIFO Cache实现
- Swiss Table
  - 采用8位int，范围0-253（0xFE，0xFF为控制值），相比swiss table的7bit范围更大，冲突概率低约1倍

## 其它优化

### 近似计算
- 近似lfu counter计算
  - 参考redis的LFU实现， 低计算成本的近似LRU计算
  - tiny-LFU中使用的[Count–min sketch](https://en.wikipedia.org/wiki/Count%E2%80%93min_sketch)也是一种近似计算方法
- 近似时间戳
  - 当ops达到1kw/s以上时，即便获取时间戳，开销也不小，采用1ms定时更新全局计时器，避免每次访问都获取时间戳

### TLS避免并发竞争
- Thread Local 计数
  - 如cache/map size/capacity等需要实时更新写的，单不需要实时读的，用TLS计数代替，避免竞争
- Thread Local 队列
  - Hazard Pointer淘汰时放入TLS队列，避免全局竞争

## 性能指标
### CPU
```shell
Architecture:        x86_64
CPU op-mode(s):      32-bit, 64-bit
Byte Order:          Little Endian
CPU(s):              56
On-line CPU(s) list: 0-55
Thread(s) per core:  2
Core(s) per socket:  28
Socket(s):           1
NUMA node(s):        1
Vendor ID:           AuthenticAMD
CPU family:          25
Model:               1
Model name:          AMD EPYC 7K83 64-Core Processor
Stepping:            0
CPU MHz:             2545.216
BogoMIPS:            5090.43
Hypervisor vendor:   KVM
Virtualization type: full
L1d cache:           32K
L1i cache:           32K
L2 cache:            512K
L3 cache:            32768K
NUMA node0 CPU(s):   0-55
Flags:               fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx mmxext fxsr_opt pdpe1gb rdtscp lm rep_good nopl cpuid extd_apicid amd_dcm tsc_known_freq pni pclmulqdq ssse3 fma cx16 sse4_1 sse4_2 x2apic movbe popcnt aes xsave avx f16c rdrand hypervisor lahf_lm cmp_legacy cr8_legacy abm sse4a misalignsse 3dnowprefetch osvw topoext ibpb vmmcall fsgsbase bmi1 avx2 smep bmi2 erms rdseed adx smap clflushopt sha_ni xsaveopt xsavec xgetbv1 arat fsrm
```
### 
### 只读性能（GET）
1000w keys， key为uint64， value为16长度字符串
![随机读](https://git.woa.com/qiyingwang/docs/raw/master/img/rand_get.png "随机读")
这里选择了几种hashmap实现和LRU cache对比， LFU/FIFO性能接近LRU实现；

由于每次读至少需要随机读64+8字节，最高ops内存访问速率相当于 2亿/s*(64+8) = 14GB/s

### 只写性能（SET）
1000w keys， key为uint64， value为16长度字符串
![随机写](https://git.woa.com/qiyingwang/docs/raw/master/img/rand_set.png "随机写")

写性能难以突破1000w ops/s，原因在于额外的复杂操作：
- 一次覆盖写相当于删除旧的节点对象（hazard pointer淘汰），插入新的节点对象
- hazard pointer为读优化设计，删除时需要hazard pointer需要延迟放置到队列中
- 批量淘汰时又需要从队列中pop删除；


### 读写混合性能（GETSET）
1000w keys， key为uint64， value为16长度字符串
这里的读写混合模式一个op相当于如下操作：
```cpp
   if(!cache.get(k)){
       cache.set(k, v);
   }
```
常规hashmap实现不会自动淘汰，这里只测试了两种LRU cache实现，读写混合性能受制与写的性能，如果读写混合中写部分操作达到1000w ops/s，那么读写混合性能也会达到瓶颈；
![随机写](https://git.woa.com/qiyingwang/docs/raw/master/img/rand_cache_getset.png "随机写")


## 与其它实现对比
这里仅记录技术上的思考对比，无实际benchmark
- [FasterKV](https://microsoft.github.io/FASTER/docs/fasterkv-basics/)
  - 针对写优化，每次读需要拷贝value才安全；号称1.6亿ops性能是只写场景
  - 原则上只能实现类似FIFO的cahce
- [CacheLib](https://github.com/facebook/CacheLib)
  - 完整的cache实现，实现上类似基于分段锁+引用计数，读写均有
- [swisstables](https://abseil.io/about/design/swisstables)的问题
  - 原则上如果限制行为为读和写，无实际删除(标记删除)，那么可以实现读写无锁的实现，类似[Folly's AtomicHashMap](https://github.com/facebook/folly/blob/main/folly/docs/AtomicHashMap.md)
  - swisstables查询性能严重依赖空槽与初始槽位距离，当一个实例频繁读写后，空槽与初始槽位距离会越来越大，查询性能会越来越差；[robin-hood-hashing](https://github.com/martinus/robin-hood-hashing)用于优化此问题，但引入交换操作很难无锁并行；
  - Cache场景则是大多数持续满载状态，频繁新增删除，如果使用swisstables作为cache实现，需要频繁的rehash
  
## 总结
以上的优化设计基本上从如下几个方面进行，大多数基本概念也可应用到其它类似需要更高性能场景；
- 针对内存访问局部性进行优化，尽量一次访问在连续内存上进行
- 并发竞争，尽量避免任何全局并发竞争； 如果无法避免，尽量减少并发竞争粒度
  - 全局std::atomic也是全局竞争资源，高并发情况下性能较差
- 尽可能采用SIMD指令，充分利用CPU新指令能力
- 近似计算代替精确计算节省大量计算资源
