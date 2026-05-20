#include <iostream>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <functional>
#include <memory>
#include <unistd.h>

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimeTask {
    private:
        uint64_t id_; //定时器任务对象id
        uint32_t timeout_; 
        bool cancelled_;
};
