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
class TimerTask {
    private:
        uint64_t id_; //定时器任务对象id
        uint32_t timeout_; 
        bool cancelled_;
        TaskFunc task_cb_;
        ReleaseFunc release_;

        public:
        TimerTask(uint64_t id, uint32_t delay,const TaskFunc& cb)
        :id_(id),
        timeout_(delay),
        task_cb_(cb),
        cancelled_(false) {}
        ~TimerTask() 
        {
            if(cancelled_ == false) task_cb_();
            release_();
        }
        void Cancel() {cancelled_ = true;}
        void SetRelease(const ReleaseFunc& cb) {release_ = cb;}
        uint32_t DelayTime() { return timeout_; }
};

class TimerWheel
{
    private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int tick_;
    int wheel_size_;
    std::vector<std::vector<PtrTask>> wheel_;
    std::unordered_map<uint64_t,WeakTask> timers_;
    private:
    void RemoveTimer(uint64_t id)
    {
        auto it = timers_.find(id);
        if(it != timers_.end())
        {
            timers_.erase(it);
        }
    }
    public:
        TimerWheel()
        :tick_(0)
        ,wheel_size_(60)
        ,wheel_(wheel_size_)
        {}

        void TimerAdd(uint64_t id,uint32_t delay,const TaskFunc& cb)
        {
            PtrTask pt(new TimerTask(id,delay,cb));
            pt->SetRelease(std::bind(&TimerWheel::RemoveTimer,this,id));
            int pos = (tick_ +delay) % wheel_size_;
            wheel_[pos].push_back(pt);
            timers_[id] = WeakTask(pt);
        }
        //刷新/延迟定时服务
        void TimerRefresh(uint64_t id)
        {
            //通过保存的定时器对象的weak_ptr构造一个shared_ptr出来，添加到轮子中
            auto it = timers_.find(id);
            if(it == timers_.end()) return;
            //lock获取weak_ptr管理的对象对应的shared_ptr
            PtrTask pt = it->second.lock();
            int delay = pt->DelayTime();
            int pos = (tick_ + delay) % wheel_size_;
            wheel_[pos].push_back(pt);
        }
        void TimerCancel(uint64_t id)
        {
            auto it = timers_.find(id);
            if(it == timers_.end()) return;
            PtrTask pt = it->second.lock();
            if(pt) pt->Cancel();
        }
        void RunTimerTask()
        {
            tick_ = (tick_ + 1) % wheel_size_;
            //清空指定位置的数组，就会把数组中保存的所有管理定时器对象的shared_ptr释放掉
            wheel_[tick_].clear();
        }
};

class TimerWheelTest
{
    public:
    TimerWheelTest() { std::cout << "TimerWheelTest constructor" << std::endl; }
    ~TimerWheelTest() { std::cout << "TimerWheelTest destructor" << std::endl; }
};

void DelTest(TimerWheelTest* t)
{
    delete t;
}
int main()
{
    TimerWheel timer_wheel;
    TimerWheelTest* t = new TimerWheelTest();
    timer_wheel.TimerAdd(666,5,std::bind(DelTest,t));
    for(int i = 0; i < 5;i++)
    {
        sleep(1);
        timer_wheel.TimerRefresh(666);//刷新定时任务
        timer_wheel.RunTimerTask();//向后移动指针
        std::cout << "刷新了一下定时任务,重新需要5s中后才会销毁\n";

    }
    timer_wheel.TimerCancel(666);
    while(true)
    {
        sleep(1);
        std::cout <<"-------------------\n";
        timer_wheel.RunTimerTask();//向后移动秒针
    }

    return 0;
}