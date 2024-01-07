# zzy-thread-pool

本仓库包含了一个线程池的C++11实现，可伸缩。

## 使用方法

### 创建线程池

```cpp
#include <chrono>
#include "thread_pool.h"
using std::operator""ms;

int main()
{
    ztools::ThreadPool pool;  // 默认线程数量的区间为[硬件线程数量 / 2,硬件线程数量]，默认空闲销毁时限为1s
    ztools::ThreadPool pool(1, 4);  // 创建了一个线程数量区间为[1,4]的线程池，默认空闲销毁时限为1s
    ztools::ThreadPool pool(1, 4, 500ms);  // 创建了一个线程数量区间为[1,4]的线程池，指定空闲销毁时限为500ms
    ztools::ThreadPool pool(500ms);  // 创建了一个线程数量区间为[硬件线程数量 / 2,硬件线程数量]的线程池，指定空闲销毁时限为500ms
}
```

### 销毁线程池

析构函数即销毁线程池，暂无销毁接口。

```cpp
#include "thread_pool.h"

int main()
{
    {
        ztools::ThreadPool pool;  // 创建线程池
    }  // 销毁线程池
}
```

### 添加任务

通过`add_task`方法添加任务，共有两种重载。

1. ```cpp
   template <typename Func, typename... Args>
   auto add_task(unsigned priority, Func&& func, Args&&... args) -> decltype(std::forward<Func>(func)(std::forward<Args>(args)...));
   ```

2. ```cpp
   template <typename Func, typename... Args>
   auto add_task(Func&& func, Args&&... args) -> decltype(std::forward<Func>(func)(std::forward<Args>(args)...));
   ```

解释：

1号重载代表创建一个优先级为`priority`的任务，任务内容为func，参数为args。返回值为一个包含func函数返回值的future对象。

2号重载代表创建一个默认优先级（默认优先级为5）的任务，任务内容为func，参数为args。返回值为一个包含func函数返回值的future对象。等价于`add_task(5, func, args...)`。