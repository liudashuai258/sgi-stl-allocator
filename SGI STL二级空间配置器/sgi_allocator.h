#pragma once
#ifndef __SGI_STL_ALLOCATOR_H
#define __SGI_STL_ALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

// 内存对齐大小（字节）
const size_t __ALIGN = 8;
// 二级配置器处理的最大内存块大小（字节）
const size_t __MAX_BYTES = 128;
// 自由链表的个数（128/8=16）
const size_t __NFREELISTS = __MAX_BYTES / __ALIGN;

// 一级空间配置器：直接使用malloc/free处理大块内存
template <int inst>
class __malloc_alloc_template {
private:
    // 处理内存分配失败的函数指针
    static void* oom_malloc(size_t n);
    static void* oom_realloc(void* p, size_t n);
    static void (*__malloc_alloc_oom_handler)();

public:
    static void* allocate(size_t n) {
        void* result = malloc(n);
        // 内存分配失败，调用oom处理函数
        if (result == nullptr) result = oom_malloc(n);
        return result;
    }

    static void deallocate(void* p, size_t /* n */) {
        free(p);
    }

    static void* reallocate(void* p, size_t /* old_sz */, size_t new_sz) {
        void* result = realloc(p, new_sz);
        // 内存重分配失败，调用oom处理函数
        if (result == nullptr) result = oom_realloc(p, new_sz);
        return result;
    }

    // 设置自定义的内存不足处理函数
    static void (*set_malloc_handler(void (*f)()))() {
        void (*old)() = __malloc_alloc_oom_handler;
        __malloc_alloc_oom_handler = f;
        return old;
    }
};

// 初始化静态成员变量
template <int inst>
void (*__malloc_alloc_template<inst>::__malloc_alloc_oom_handler)() = nullptr;

template <int inst>
void* __malloc_alloc_template<inst>::oom_malloc(size_t n) {
    void (*my_malloc_handler)();
    void* result;

    for (;;) {
        my_malloc_handler = __malloc_alloc_oom_handler;
        // 如果没有设置处理函数，抛出bad_alloc异常
        if (my_malloc_handler == nullptr) {
            throw std::bad_alloc();
        }
        // 调用用户设置的处理函数
        (*my_malloc_handler)();
        // 再次尝试分配内存
        result = malloc(n);
        if (result) return result;
    }
}

template <int inst>
void* __malloc_alloc_template<inst>::oom_realloc(void* p, size_t n) {
    void (*my_malloc_handler)();
    void* result;

    for (;;) {
        my_malloc_handler = __malloc_alloc_oom_handler;
        if (my_malloc_handler == nullptr) {
            throw std::bad_alloc();
        }
        (*my_malloc_handler)();
        result = realloc(p, n);
        if (result) return result;
    }
}

// 定义一级配置器类型
typedef __malloc_alloc_template<0> malloc_alloc;

// 二级空间配置器：使用内存池技术处理小块内存
template <bool threads, int inst>
class __default_alloc_template {
private:
    // 自由链表节点结构
    union obj {
        union obj* free_list_link; // 指向下一个空闲块
        char client_data[1];       // 用户数据区
    };

    // 自由链表数组（16个链表，分别管理8~128字节的内存块）
    static obj* volatile free_list[__NFREELISTS];
    // 内存池起始地址
    static char* start_free;
    // 内存池结束地址
    static char* end_free;
    // 已分配的内存总量
    static size_t heap_size;

    // 将内存大小向上调整到8的倍数
    static size_t ROUND_UP(size_t bytes) {
        return ((bytes + __ALIGN - 1) & ~(__ALIGN - 1));
    }

    // 根据内存大小找到对应的自由链表索引
    static size_t FREELIST_INDEX(size_t bytes) {
        return ((bytes + __ALIGN - 1) / __ALIGN - 1);
    }

    // 从内存池中分配n个大小为size的内存块，返回第一个块的地址
    // n是引用传递，实际分配的块数可能小于请求的数量
    static char* chunk_alloc(size_t size, int& nobjs);

    // 当自由链表为空时，补充内存块
    static void* refill(size_t size);

public:
    static void* allocate(size_t n) {
        obj* volatile* my_free_list;
        obj* result;

        // 大于128字节，使用一级配置器
        if (n > (size_t)__MAX_BYTES) {
            return malloc_alloc::allocate(n);
        }

        // 找到对应的自由链表
        my_free_list = free_list + FREELIST_INDEX(n);
        result = *my_free_list;

        // 自由链表为空，需要补充内存
        if (result == nullptr) {
            void* r = refill(ROUND_UP(n));
            return r;
        }

        // 从自由链表中取出第一个块
        *my_free_list = result->free_list_link;
        return result;
    }

    static void deallocate(void* p, size_t n) {
        obj* q = (obj*)p;
        obj* volatile* my_free_list;

        // 大于128字节，使用一级配置器释放
        if (n > (size_t)__MAX_BYTES) {
            malloc_alloc::deallocate(p, n);
            return;
        }

        // 找到对应的自由链表
        my_free_list = free_list + FREELIST_INDEX(n);
        // 将释放的块插入到自由链表头部
        q->free_list_link = *my_free_list;
        *my_free_list = q;
    }

    static void* reallocate(void* p, size_t old_sz, size_t new_sz);
};

// 初始化静态成员变量
template <bool threads, int inst>
char* __default_alloc_template<threads, inst>::start_free = nullptr;

template <bool threads, int inst>
char* __default_alloc_template<threads, inst>::end_free = nullptr;

template <bool threads, int inst>
size_t __default_alloc_template<threads, inst>::heap_size = 0;

template <bool threads, int inst>
typename __default_alloc_template<threads, inst>::obj* volatile
__default_alloc_template<threads, inst>::free_list[__NFREELISTS] = {
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr
};

// 从内存池中分配内存块
template <bool threads, int inst>
char* __default_alloc_template<threads, inst>::chunk_alloc(size_t size, int& nobjs) {
    char* result;
    size_t total_bytes = size * nobjs;
    size_t bytes_left = end_free - start_free;

    // 内存池有足够空间满足全部请求
    if (bytes_left >= total_bytes) {
        result = start_free;
        start_free += total_bytes;
        return result;
    }
    // 内存池空间不足，但至少能分配一个块
    else if (bytes_left >= size) {
        nobjs = bytes_left / size;
        total_bytes = size * nobjs;
        result = start_free;
        start_free += total_bytes;
        return result;
    }
    // 内存池连一个块都分配不了，需要向系统申请内存
    else {
        // 申请的内存大小 = 2*总请求量 + 已分配内存总量/8
        size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);

        // 将内存池中剩余的小块内存分配给对应的自由链表
        if (bytes_left > 0) {
            obj* volatile* my_free_list = free_list + FREELIST_INDEX(bytes_left);
            ((obj*)start_free)->free_list_link = *my_free_list;
            *my_free_list = (obj*)start_free;
        }

        // 向系统申请内存
        start_free = (char*)malloc(bytes_to_get);
        // 系统内存不足，尝试从更大的自由链表中获取内存
        if (start_free == nullptr) {
            size_t i;
            obj* volatile* my_free_list;
            obj* p;

            // 从比当前size大的自由链表中查找
            for (i = size; i <= __MAX_BYTES; i += __ALIGN) {
                my_free_list = free_list + FREELIST_INDEX(i);
                p = *my_free_list;
                if (p != nullptr) {
                    // 释放一个块到内存池
                    *my_free_list = p->free_list_link;
                    start_free = (char*)p;
                    end_free = start_free + i;
                    // 递归调用，重新尝试分配
                    return chunk_alloc(size, nobjs);
                }
            }

            // 所有自由链表都没有内存，只能调用一级配置器
            end_free = nullptr;
            start_free = (char*)malloc_alloc::allocate(bytes_to_get);
        }

        heap_size += bytes_to_get;
        end_free = start_free + bytes_to_get;
        // 递归调用，重新尝试分配
        return chunk_alloc(size, nobjs);
    }
}

// 补充自由链表的内存块
template <bool threads, int inst>
void* __default_alloc_template<threads, inst>::refill(size_t size) {
    int nobjs = 20; // 默认申请20个块
    char* chunk = chunk_alloc(size, nobjs);
    obj* volatile* my_free_list;
    obj* result;
    obj* current_obj;
    obj* next_obj;
    int i;

    // 如果只分配到一个块，直接返回给用户
    if (nobjs == 1) return chunk;

    // 找到对应的自由链表
    my_free_list = free_list + FREELIST_INDEX(size);
    // 第一个块返回给用户
    result = (obj*)chunk;
    // 剩下的块加入自由链表
    *my_free_list = next_obj = (obj*)(chunk + size);

    // 将分配到的内存块链接成链表
    for (i = 1; ; i++) {
        current_obj = next_obj;
        next_obj = (obj*)((char*)next_obj + size);
        if (i == nobjs - 1) {
            current_obj->free_list_link = nullptr;
            break;
        }
        else {
            current_obj->free_list_link = next_obj;
        }
    }

    return result;
}

// 内存重分配
template <bool threads, int inst>
void* __default_alloc_template<threads, inst>::reallocate(void* p, size_t old_sz, size_t new_sz) {
    void* result;
    size_t copy_sz;

    // 新旧大小都大于128字节，直接使用一级配置器
    if (old_sz > __MAX_BYTES && new_sz > __MAX_BYTES) {
        return malloc_alloc::reallocate(p, old_sz, new_sz);
    }

    // 新旧大小调整后相同，直接返回原指针
    if (ROUND_UP(old_sz) == ROUND_UP(new_sz)) return p;

    // 否则，重新分配内存并拷贝数据
    result = allocate(new_sz);
    copy_sz = new_sz > old_sz ? old_sz : new_sz;
    memcpy(result, p, copy_sz);
    deallocate(p, old_sz);
    return result;
}

// 定义默认的二级配置器类型（非线程安全版本）
typedef __default_alloc_template<false, 0> alloc;

// 包装成标准STL风格的allocator接口
template <class T, class Alloc = alloc>
class simple_alloc {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;

    template <class U>
    struct rebind {
        typedef simple_alloc<U, Alloc> other;
    };

    static pointer allocate(size_type n) {
        return n == 0 ? nullptr : (pointer)Alloc::allocate(n * sizeof(T));
    }

    static pointer allocate() {
        return (pointer)Alloc::allocate(sizeof(T));
    }

    static void deallocate(pointer p, size_type n) {
        if (n != 0) Alloc::deallocate(p, n * sizeof(T));
    }

    static void deallocate(pointer p) {
        Alloc::deallocate(p, sizeof(T));
    }

    static void construct(pointer p, const T& val) {
        new (p) T(val);
    }

    static void destroy(pointer p) {
        p->~T();
    }

    static pointer address(reference x) {
        return &x;
    }

    static const_pointer const_address(const_reference x) {
        return &x;
    }

    static size_type max_size() {
        return size_type(-1) / sizeof(T);
    }
};

#endif // __SGI_STL_ALLOCATOR_H