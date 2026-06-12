#include <vector>
#include <list>
#include "sgi_allocator.h"

int main() {
    // 使用自定义分配器的vector
    std::vector<int, simple_alloc<int>> vec;
    for (int i = 0; i < 1000; i++) {
        vec.push_back(i);
    }

    // 使用自定义分配器的list
    std::list<double, simple_alloc<double>> lst;
    for (int i = 0; i < 1000; i++) {
        lst.push_back(i * 0.5);
    }

    std::cout << "SGI STL二级空间配置器测试完成" << std::endl;
    return 0;
}