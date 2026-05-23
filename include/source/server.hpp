#ifndef __M_SERVER_H__
#define __M_SERVER_H__
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <ctime>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <typeinfo>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h> 

#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL DBG

#define LOG(level, format, ...) do{\
        if (level < LOG_LEVEL) break;\
        time_t t = time(NULL);\
        struct tm *ltm = localtime(&t);\
        char tmp[32] = {0};\
        strftime(tmp, 31, "%H:%M:%S", ltm);\
        fprintf(stdout, "[%p %s %s:%d] " format "\n", (void*)pthread_self(), tmp, __FILE__, __LINE__, ##__VA_ARGS__);\
    }while(0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)
#define BUFFER_DEFAULT_SIZE 1024
class Buffer {
    private:
        std::vector<char> _buffer; //使用vector进行内存空间管理
        uint64_t _reader_idx; //读偏移
        uint64_t _writer_idx; //写偏移
    public:
        Buffer():_reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE){}
        char *Begin() { return &*_buffer.begin(); }
        //获取当前写入起始地址, _buffer的空间起始地址，加上写偏移量
        char *WritePosition() { return Begin() + _writer_idx; }
        //获取当前读取起始地址
        char *ReadPosition() { return Begin() + _reader_idx; }
        //获取缓冲区末尾空闲空间大小--写偏移之后的空闲空间, 总体空间大小减去写偏移
        uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }
        //获取缓冲区起始空闲空间大小--读偏移之前的空闲空间
        uint64_t HeadIdleSize() { return _reader_idx; }
        //获取可读数据大小 = 写偏移 - 读偏移
        uint64_t ReadAbleSize() { return _writer_idx - _reader_idx; }
        //将读偏移向后移动
        void MoveReadOffset(uint64_t len) { 
            if (len == 0) return; 
            //向后移动的大小，必须小于可读数据大小
            assert(len <= ReadAbleSize());
            _reader_idx += len;
        }
        //将写偏移向后移动 
        void MoveWriteOffset(uint64_t len) {
            //向后移动的大小，必须小于当前后边的空闲空间大小
            assert(len <= TailIdleSize());
            _writer_idx += len;
        }
        //确保可写空间足够（整体空闲空间够了就移动数据，否则就扩容）
        void EnsureWriteSpace(uint64_t len) {
            //如果末尾空闲空间大小足够，直接返回
            if (TailIdleSize() >= len) { return; }
            //末尾空闲空间不够，则判断加上起始位置的空闲空间大小是否足够, 够了就将数据移动到起始位置
            if (len <= TailIdleSize() + HeadIdleSize()) {
                //将数据移动到起始位置
                uint64_t rsz = ReadAbleSize();//把当前数据大小先保存起来
                std::copy(ReadPosition(), ReadPosition() + rsz, Begin());//把可读数据拷贝到起始位置
                _reader_idx = 0;    //将读偏移归0
                _writer_idx = rsz;  //将写位置置为可读数据大小， 因为当前的可读数据大小就是写偏移量
            }else {
                //总体空间不够，则需要扩容，不移动数据，直接给写偏移之后扩容足够空间即可
                DBG_LOG("RESIZE %ld", _writer_idx + len);
                _buffer.resize(_writer_idx + len);
            }
        } 
        //写入数据
        void Write(const void *data, uint64_t len) {
            //1. 保证有足够空间，2. 拷贝数据进去
            if (len == 0) return;
            EnsureWriteSpace(len);
            const char *d = (const char *)data;
            std::copy(d, d + len, WritePosition());
        }
        void WriteAndPush(const void *data, uint64_t len) {
            Write(data, len);
            MoveWriteOffset(len);
        }
        void WriteString(const std::string &data) {
            return Write(data.c_str(), data.size());
        }
        void WriteStringAndPush(const std::string &data) {
            WriteString(data);
            MoveWriteOffset(data.size());
        }
        void WriteBuffer(Buffer &data) {
            return Write(data.ReadPosition(), data.ReadAbleSize());
        }
        void WriteBufferAndPush(Buffer &data) { 
            WriteBuffer(data);
            MoveWriteOffset(data.ReadAbleSize());
        }
        //读取数据
        void Read(void *buf, uint64_t len) {
            //要求要获取的数据大小必须小于可读数据大小
            assert(len <= ReadAbleSize());
            std::copy(ReadPosition(), ReadPosition() + len, (char*)buf);
        }
        void ReadAndPop(void *buf, uint64_t len) {
            Read(buf, len);
            MoveReadOffset(len);
        }
        std::string ReadAsString(uint64_t len) {
            //要求要获取的数据大小必须小于可读数据大小
            assert(len <= ReadAbleSize());
            std::string str;
            str.resize(len);
            Read(&str[0], len);
            return str;
        }
        std::string ReadAsStringAndPop(uint64_t len) {
            assert(len <= ReadAbleSize());
            std::string str = ReadAsString(len);
            MoveReadOffset(len);
            return str;
        }
        char *FindCRLF() {
            char *res = (char*)memchr(ReadPosition(), '\n', ReadAbleSize());
            return res;
        }
        /*通常获取一行数据，这种情况针对是*/
        std::string GetLine() {
            char *pos = FindCRLF();
            if (pos == NULL) {
                return "";
            }
            // +1是为了把换行字符也取出来。
            return ReadAsString(pos - ReadPosition() + 1);
        }
        std::string GetLineAndPop() {
            std::string str = GetLine();
            MoveReadOffset(str.size());
            return str;
        }
        //清空缓冲区
        void Clear() {
            //只需要将偏移量归0即可
            _reader_idx = 0;
            _writer_idx = 0;
        }
};

#define MAX_LISTEN 1024
class Socket {
    private:
        int _sockfd;
    public:
        Socket():_sockfd(-1) {}
        Socket(int fd): _sockfd(fd) {}
        ~Socket() { Close(); }
        int Fd() { return _sockfd; }
        //创建套接字
        bool Create() {
            _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (_sockfd < 0) {
                ERR_LOG("CREATE SOCKET FAILED!!");
                return false;
            }
            return true;
        }
        //绑定地址信息
        bool Bind(const std::string &ip, uint16_t port) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
            socklen_t len = sizeof(struct sockaddr_in);
            int ret = bind(_sockfd, (struct sockaddr*)&addr, len);
            if (ret < 0) {
                ERR_LOG("BIND ADDRESS FAILED!");
                return false;
            }
            return true;
        }
        //开始监听
        bool Listen(int backlog = MAX_LISTEN) {
            int ret = listen(_sockfd, backlog);
            if (ret < 0) {
                ERR_LOG("SOCKET LISTEN FAILED!");
                return false;
            }
            return true;
        }
        //向服务器发起连接
        bool Connect(const std::string &ip, uint16_t port) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
            socklen_t len = sizeof(struct sockaddr_in);
            int ret = connect(_sockfd, (struct sockaddr*)&addr, len);
            if (ret < 0) {
                ERR_LOG("CONNECT SERVER FAILED!");
                return false;
            }
            return true;
        }
        //获取新连接
        int Accept() {
            int newfd = accept(_sockfd, NULL, NULL);
            if (newfd < 0) {
                // Mac 上非阻塞时如果没连上来会报 EAGAIN，属于正常现象
                if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
                ERR_LOG("SOCKET ACCEPT FAILED!");
                return -1;
            }
            return newfd;
        }
        //接收数据
        ssize_t Recv(void *buf, size_t len, int flag = 0) {
            ssize_t ret = recv(_sockfd, buf, len, flag);
            if (ret <= 0) {
                if (ret == 0) return -1; // Mac上对端关闭显式返回-1代表断开
                if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) {
                    return 0;
                }
                ERR_LOG("SOCKET RECV FAILED!!");
                return -1;
            }
            return ret; 
        }
        ssize_t NonBlockRecv(void *buf, size_t len) {
            return Recv(buf, len, MSG_DONTWAIT); 
        }
        //发送数据
        ssize_t Send(const void *buf, size_t len, int flag = 0) {
            ssize_t ret = send(_sockfd, buf, len, flag);
            if (ret < 0) {
                if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) {
                    return 0;
                }
                ERR_LOG("SOCKET SEND FAILED!!");
                return -1;
            }
            return ret;
        }
        ssize_t NonBlockSend(void *buf, size_t len) {
            if (len == 0) return 0;
            return Send(buf, len, MSG_DONTWAIT); 
        }
        //关闭套接字
        void Close() {
            if (_sockfd != -1) {
                close(_sockfd);
                _sockfd = -1;
            }
        }
        //创建一个服务端连接
        bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = true) {
            if (Create() == false) return false;
            ReuseAddress(); // Mac 上优先启动地址复用
            if (block_flag) NonBlock();
            if (Bind(ip, port) == false) return false;
            if (Listen() == false) return false;
            return true;
        }
        //创建一个客户端连接
        bool CreateClient(uint16_t port, const std::string &ip) {
            if (Create() == false) return false;
            if (Connect(ip, port) == false) return false;
            return true;
        }
        //设置套接字选项---开启地址端口重用
        void ReuseAddress() {
            int val = 1;
            setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&val, sizeof(int));
#ifdef SO_REUSEPORT
            val = 1;
            setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void*)&val, sizeof(int));
#endif
        }
        //设置套接字阻塞属性-- 设置为非阻塞
        void NonBlock() {
            int flag = fcntl(_sockfd, F_GETFL, 0);
            fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
        }
};

class Poller;
class EventLoop;
class Channel {
    private:
        int _fd;
        EventLoop *_loop;
        uint32_t _events;  // 当前需要监控的事件
        uint32_t _revents; // 当前连接触发的事件
        using EventCallback = std::function<void()>;
        EventCallback _read_callback;   //可读事件被触发的回调函数
        EventCallback _write_callback;  //可写事件被触发的回调函数
        EventCallback _error_callback;  //错误事件被触发的回调函数
        EventCallback _close_callback;  //连接断开事件被触发的回调函数
        EventCallback _event_callback;  //任意事件被触发的回调函数
    public:
        Channel(EventLoop *loop, int fd):_fd(fd), _events(0), _revents(0), _loop(loop) {}
        int Fd() { return _fd; }
        uint32_t Events() { return _events; }//获取想要监控的事件
        void SetREvents(uint32_t events) { _revents = events; }//设置实际就绪的事件
        void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
        void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
        void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
        void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
        void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
        //当前是否监控了可读（Mac kqueue下对齐行为）
        bool ReadAble() { return (_events & EVFILT_READ); } 
        //当前是否监控了可写
        bool WriteAble() { return (_events & EVFILT_WRITE); }
        //启动读事件监控
        void EnableRead() { _events |= EVFILT_READ; Update(); }
        //启动写事件监控
        void EnableWrite() { _events |= EVFILT_WRITE; Update(); }
        //关闭读事件监控
        void DisableRead() { _events &= ~EVFILT_READ; Update(); }
        //关闭写事件监控
        void DisableWrite() { _events &= ~EVFILT_WRITE; Update(); }
        //关闭所有事件监控
        void DisableAll() { _events = 0; Update(); }
        //移除监控
        void Remove();
        void Update();
        //事件处理
        void HandleEvent() {
            if (_revents & EVFILT_READ) {
                if (_read_callback) _read_callback();
            }
            if (_revents & EVFILT_WRITE) {
                if (_write_callback) _write_callback();
            }
            // Mac kqueue 异常通过 flags 携带，这里做大类兼顾
            if (_revents & EV_ERROR) {
                if (_error_callback) _error_callback();
            }
            if (_event_callback) _event_callback();
        }
};

//  Mac kqueue 高性能 Poller 实现
#define MAX_EPOLLEVENTS 1024
class Poller {
    private:
        int _kqfd;
        struct kevent _evs[MAX_EPOLLEVENTS];
        std::unordered_map<int, Channel *> _channels;
    private:
        void Update(Channel *channel, int filter, int op) {
            struct kevent ev;
            // 针对每个事件属性（读或写）向 kqueue 注册
            EV_SET(&ev, channel->Fd(), filter, op, 0, 0, channel);
            kevent(_kqfd, &ev, 1, NULL, 0, NULL);
        }
    public:
        Poller() {
            _kqfd = kqueue();
            if (_kqfd < 0) {
                ERR_LOG("KQUEUE CREATE FAILED!!");
                abort();
            }
        }
        void UpdateEvent(Channel *channel) {
            // 对齐 Linux epoll_ctl，分别处理读写事件标志
            if (channel->Events() & EVFILT_READ) {
                Update(channel, EVFILT_READ, EV_ADD | EV_ENABLE);
            } else {
                Update(channel, EVFILT_READ, EV_DELETE);
            }
            
            if (channel->Events() & EVFILT_WRITE) {
                Update(channel, EVFILT_WRITE, EV_ADD | EV_ENABLE);
            } else {
                Update(channel, EVFILT_WRITE, EV_DELETE);
            }
            _channels[channel->Fd()] = channel;
        }
        void RemoveEvent(Channel *channel) {
            Update(channel, EVFILT_READ, EV_DELETE);
            Update(channel, EVFILT_WRITE, EV_DELETE);
            _channels.erase(channel->Fd());
        }
        void Poll(std::vector<Channel*> *active) {
            int nfds = kevent(_kqfd, NULL, 0, _evs, MAX_EPOLLEVENTS, NULL);
            if (nfds < 0) {
                if (errno == EINTR) return;
                ERR_LOG("KQUEUE WAIT ERROR:%s\n", strerror(errno));
                abort();
            }
            for (int i = 0; i < nfds; i++) {
                Channel* chan = (Channel*)_evs[i].udata;
                if (chan) {
                    chan->SetREvents(_evs[i].filter); // 传递就绪的 filter
                    active->push_back(chan);
                }
            }
        }
};

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask{
    private:
        uint64_t _id;       // 定时器任务对象ID
        uint32_t _timeout;  //定时任务的超时时间
        bool _canceled;     // false-表示没有被取消， true-表示被取消
        TaskFunc _task_cb;  //定时器对象要执行的定时任务
        ReleaseFunc _release; //用于删除TimerWheel中保存的定时器对象信息
    public:
        TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb): 
            _id(id), _timeout(delay), _task_cb(cb), _canceled(false) {}
        ~TimerTask() { 
            if (_canceled == false) _task_cb(); 
            _release(); 
        }
        void Cancel() { _canceled = true; }
        void SetRelease(const ReleaseFunc &cb) { _release = cb; }
        uint32_t DelayTime() { return _timeout; }
};

class TimerWheel {
    private:
        using WeakTask = std::weak_ptr<TimerTask>;
        using PtrTask = std::shared_ptr<TimerTask>;
        int _tick;      //当前的秒针
        int _capacity;  //表盘最大数量
        std::vector<std::vector<PtrTask>> _wheel;
        std::unordered_map<uint64_t, WeakTask> _timers;

        EventLoop *_loop;
        int _timerfd; // Mac 下作为虚拟描述符标志
        std::unique_ptr<Channel> _timer_channel;
    private:
        void RemoveTimer(uint64_t id) {
            auto it = _timers.find(id);
            if (it != _timers.end()) {
                _timers.erase(it);
            }
        }
        // 🌟 Mac 下利用 kqueue 的原生事件进行定时器驱动
        void InitMacTimer() {
            struct kevent change;
            EV_SET(&change, _timerfd, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_SECONDS, 1, 0);
            int kq = my_kqueue_fd(); 
            kevent(kq, &change, 1, NULL, 0, NULL);
        }
        int ReadTimefd() {
            return 1; // 触发即为 1 次超时跳格
        }
        void RunTimerTask() {
            _tick = (_tick + 1) % _capacity;
            _wheel[_tick].clear();
        }
        void OnTime() {
            RunTimerTask();
        }
        void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb) {
            PtrTask pt(new TimerTask(id, delay, cb));
            pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
            int pos = (_tick + delay) % _capacity;
            _wheel[pos].push_back(pt);
            _timers[id] = WeakTask(pt);
        }
        void TimerRefreshInLoop(uint64_t id) {
            auto it = _timers.find(id);
            if (it == _timers.end()) return;
            PtrTask pt = it->second.lock();
            if(!pt) return;
            int delay = pt->DelayTime();
            int pos = (_tick + delay) % _capacity;
            _wheel[pos].push_back(pt);
        }
        void TimerCancelInLoop(uint64_t id) {
            auto it = _timers.find(id);
            if (it == _timers.end()) return;
            PtrTask pt = it->second.lock();
            if (pt) pt->Cancel();
        }
        int my_kqueue_fd();
    public:
        TimerWheel(EventLoop *loop);
        void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);
        void TimerRefresh(uint64_t id);
        void TimerCancel(uint64_t id);
        bool HasTimer(uint64_t id) {
            auto it = _timers.find(id);
            return it != _timers.end();
        }
};

class EventLoop {
    private:
        using Functor = std::function<void()>;
        std::thread::id _thread_id;//线程ID
        int _pipe_fds[2]; // 🌟 用 Mac 管道平替 eventfd：[0]读，[1]写
        std::unique_ptr<Channel> _event_channel;
        Poller _poller;//进行所有描述符的事件监控
        std::vector<Functor> _tasks;//任务池
        std::mutex _mutex;//实现任务池操作的线程安全
        TimerWheel _timer_wheel;//定时器模块
    public:
        void RunAllTask() {
            std::vector<Functor> functor;
            {
                std::unique_lock<std::mutex> _lock(_mutex);
                _tasks.swap(functor);
            }
            for (auto &f : functor) {
                f();
            }
        }
        void ReadEventfd() {
            uint64_t res = 0;
            read(_pipe_fds[0], &res, sizeof(res)); // 从管道读端唤醒
        }
        void WeakUpEventFd() {
            uint64_t val = 1;
            write(_pipe_fds[1], &val, sizeof(val)); // 往管道写端灌入事件
        }
        int GetKqFd() { return _poller.Poll(nullptr), 0; } // 内部通路占位
    public:
        EventLoop():_thread_id(std::this_thread::get_id()), _timer_wheel(this) {
            if (pipe(_pipe_fds) < 0) abort();
            fcntl(_pipe_fds[0], F_SETFL, O_NONBLOCK);
            fcntl(_pipe_fds[1], F_SETFL, O_NONBLOCK);
            _event_channel.reset(new Channel(this, _pipe_fds[0]));
            _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventfd, this));
            _event_channel->EnableRead();
        }
        void Start() {
            // 🌟 启动时将时间轮驱动也绑定至监控内核
            struct kevent change;
            EV_SET(&change, 999, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_SECONDS, 1, 0);
            
            while(1) {
                std::vector<Channel *> actives;
                _poller.Poll(&actives);
                for (auto &channel : actives) {
                    channel->HandleEvent();
                }
                RunAllTask();
            }
        }
        bool IsInLoop() { return (_thread_id == std::this_thread::get_id()); }
        void AssertInLoop() { assert(_thread_id == std::this_thread::get_id()); }
        void RunInLoop(const Functor &cb) {
            if (IsInLoop()) return cb();
            return QueueInLoop(cb);
        }
        void QueueInLoop(const Functor &cb) {
            {
                std::unique_lock<std::mutex> _lock(_mutex);
                _tasks.push_back(cb);
            }
            WeakUpEventFd();
        }
        void UpdateEvent(Channel *channel) { _poller.UpdateEvent(channel); }
        void RemoveEvent(Channel *channel) { _poller.RemoveEvent(channel); }
        void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb) { _timer_wheel.TimerAdd(id, delay, cb); }
        void TimerRefresh(uint64_t id) { _timer_wheel.TimerRefresh(id); }
        void TimerCancel(uint64_t id) { _timer_wheel.TimerCancel(id); }
        bool HasTimer(uint64_t id) { return _timer_wheel.HasTimer(id); }
};

// 🌟 实现延迟绑定的 TimerWheel 构造函数
inline TimerWheel::TimerWheel(EventLoop *loop):_capacity(60), _tick(0), _wheel(_capacity), _loop(loop), 
    _timerfd(999), _timer_channel(new Channel(_loop, _timerfd)) {
    _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
    _timer_channel->EnableRead();
}

class LoopThread {
    private:
        std::mutex _mutex;          
        std::condition_variable _cond;   
        EventLoop *_loop;       
        std::thread _thread;    
    private:
        void ThreadEntry() {
            EventLoop loop;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _loop = &loop;
                _cond.notify_all();
            }
            loop.Start();
        }
    public:
        LoopThread():_loop(NULL), _thread(std::thread(&LoopThread::ThreadEntry, this)) {}
        EventLoop *GetLoop() {
            EventLoop *loop = NULL;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cond.wait(lock, [&](){ return _loop != NULL; });
                loop = _loop;
            }
            return loop;
        }
};

class LoopThreadPool {
    private:
        int _thread_count;
        int _next_idx;
        EventLoop *_baseloop;
        std::vector<LoopThread*> _threads;
        std::vector<EventLoop *> _loops;
    public:
        LoopThreadPool(EventLoop *baseloop):_thread_count(0), _next_idx(0), _baseloop(baseloop) {}
        void SetThreadCount(int count) { _thread_count = count; }
        void Create() {
            if (_thread_count > 0) {
                _threads.resize(_thread_count);
                _loops.resize(_thread_count);
                for (int i = 0; i < _thread_count; i++) {
                    _threads[i] = new LoopThread();
                    _loops[i] = _threads[i]->GetLoop();
                }
            }
        }
        EventLoop *NextLoop() {
            if (_thread_count == 0) return _baseloop;
            _next_idx = (_next_idx + 1) % _thread_count;
            return _loops[_next_idx];
        }
};

// 🌟 手手写的 Any 类保持原汁原味
class Any{
    private:
        class holder {
            public:
                virtual ~holder() {}
                virtual const std::type_info& type() = 0;
                virtual holder *clone() = 0;
        };
        template<class T>
        class placeholder: public holder {
            public:
                placeholder(const T &val): _val(val) {}
                virtual const std::type_info& type() { return typeid(T); }
                virtual holder *clone() { return new placeholder(_val); }
            public:
                T _val;
        };
        holder *_content;
    public:
        Any():_content(NULL) {}
        template<class T>
        Any(const T &val):_content(new placeholder<T>(val)) {}
        Any(const Any &other):_content(other._content ? other._content->clone() : NULL) {}
        ~Any() { delete _content; }

        Any &swap(Any &other) {
            std::swap(_content, other._content);
            return *this;
        }
        template<class T>
        T *get() {
            assert(typeid(T) == _content->type());
            return &((placeholder<T>*)_content)->_val;
        }
        template<class T>
        Any& operator=(const T &val) {
            Any(val).swap(*this);
            return *this;
        }
        Any& operator=(const Any &other) {
            Any(other).swap(*this);
            return *this;
        }
};

class Connection;
typedef enum { DISCONNECTED, CONNECTING, CONNECTED, DISCONNECTING}ConnStatu;
using PtrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection> {
    private:
        uint64_t _conn_id;  
        int _sockfd;        
        bool _enable_inactive_release;  
        EventLoop *_loop;   
        ConnStatu _statu;   
        Socket _socket;     
        Channel _channel;   
        Buffer _in_buffer;  
        Buffer _out_buffer; 
        Any _context;       

        using ConnectedCallback = std::function<void(const PtrConnection&)>;
        using MessageCallback = std::function<void(const PtrConnection&, Buffer *)>;
        using ClosedCallback = std::function<void(const PtrConnection&)>;
        using AnyEventCallback = std::function<void(const PtrConnection&)>;
        ConnectedCallback _connected_callback;
        MessageCallback _message_callback;
        ClosedCallback _closed_callback;
        AnyEventCallback _event_callback;
        ClosedCallback _server_closed_callback;
    private:
        void HandleRead() {
            char buf[65536];
            ssize_t ret = _socket.NonBlockRecv(buf, 65535);
            if (ret < 0) {
                return ShutdownInLoop();
            }
            _in_buffer.WriteAndPush(buf, ret);
            if (_in_buffer.ReadAbleSize() > 0) {
                return _message_callback(shared_from_this(), &_in_buffer);
            }
        }
        void HandleWrite() {
            ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadAbleSize());
            if (ret < 0) {
                if (_in_buffer.ReadAbleSize() > 0) {
                    _message_callback(shared_from_this(), &_in_buffer);
                }
                return Release();
            }
            _out_buffer.MoveReadOffset(ret);
            if (_out_buffer.ReadAbleSize() == 0) {
                _channel.DisableWrite();
                if (_statu == DISCONNECTING) {
                    return Release();
                }
            }
        }
        void HandleClose() {
            if (_in_buffer.ReadAbleSize() > 0) {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            return Release();
        }
        void HandleError() { return HandleClose(); }
        void HandleEvent() {
            if (_enable_inactive_release == true)  {  _loop->TimerRefresh(_conn_id); }
            if (_event_callback)  {  _event_callback(shared_from_this()); }
        }
        void EstablishedInLoop() {
            assert(_statu == CONNECTING);
            _statu = CONNECTED;
            _channel.EnableRead();
            if (_connected_callback) _connected_callback(shared_from_this());
        }
        void ReleaseInLoop() {
            _statu = DISCONNECTED;
            _channel.Remove();
            _socket.Close();
            if (_loop->HasTimer(_conn_id)) CancelInactiveReleaseInLoop();
            if (_closed_callback) _closed_callback(shared_from_this());
            if (_server_closed_callback) _server_closed_callback(shared_from_this());
        }
        void SendInLoop(Buffer &buf) {
            if (_statu == DISCONNECTED) return ;
            _out_buffer.WriteBufferAndPush(buf);
            if (_channel.WriteAble() == false) {
                _channel.EnableWrite();
            }
        }
        void ShutdownInLoop() {
            _statu = DISCONNECTING;
            if (_in_buffer.ReadAbleSize() > 0) {
                if (_message_callback) _message_callback(shared_from_this(), &_in_buffer);
            }
            if (_out_buffer.ReadAbleSize() > 0) {
                if (_channel.WriteAble() == false) {
                    _channel.EnableWrite();
                }
            }
            if (_out_buffer.ReadAbleSize() == 0) {
                Release();
            }
        }
        void EnableInactiveReleaseInLoop(int sec) {
            _enable_inactive_release = true;
            if (_loop->HasTimer(_conn_id)) {
                return _loop->TimerRefresh(_conn_id);
            }
            _loop->TimerAdd(_conn_id, sec, std::bind(&Connection::Release, this));
        }
        void CancelInactiveReleaseInLoop() {
            _enable_inactive_release = false;
            if (_loop->HasTimer(_conn_id)) { 
                _loop->TimerCancel(_conn_id); 
            }
        }
        void UpgradeInLoop(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg, const ClosedCallback &closed, const AnyEventCallback &event) {
            _context = context; _connected_callback = conn; _message_callback = msg; _closed_callback = closed; _event_callback = event;
        }
    public:
        Connection(EventLoop *loop, uint64_t conn_id, int sockfd):_conn_id(conn_id), _sockfd(sockfd),
            _enable_inactive_release(false), _loop(loop), _statu(CONNECTING), _socket(_sockfd), _channel(loop, _sockfd) {
            _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this));
            _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
            _channel.SetReadCallback(std::bind(&Connection::HandleRead, this));
            _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this));
            _channel.SetErrorCallback(std::bind(&Connection::HandleError, this));
        }
        ~Connection() { DBG_LOG("RELEASE CONNECTION:%p", this); }
        int Fd() { return _sockfd; }
        int Id() { return _conn_id; }
        bool Connected() { return (_statu == CONNECTED); }
        void SetContext(const Any &context) { _context = context; }
        Any *GetContext() { return &_context; }
        void SetConnectedCallback(const ConnectedCallback&cb) { _connected_callback = cb; }
        void SetMessageCallback(const MessageCallback&cb) { _message_callback = cb; }
        void SetClosedCallback(const ClosedCallback&cb) { _closed_callback = cb; }
        void SetAnyEventCallback(const AnyEventCallback&cb) { _event_callback = cb; }
        void SetSrvClosedCallback(const ClosedCallback&cb) { _server_closed_callback = cb; }
        void Established() { _loop->RunInLoop(std::bind(&Connection::EstablishedInLoop, this)); }
        void Send(const char *data, size_t len) {
            Buffer buf; buf.WriteAndPush(data, len);
            _loop->RunInLoop(std::bind(&Connection::SendInLoop, this, std::move(buf)));
        }
        void Shutdown() { _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop, this)); }
        void Release() { _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop, this)); }
        void EnableInactiveRelease(int sec) { _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop, this, sec)); }
        void CancelInactiveRelease() { _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop, this)); }
        void Upgrade(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg, const ClosedCallback &closed, const AnyEventCallback &event) {
            _loop->AssertInLoop();
            _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop, this, context, conn, msg, closed, event));
        }
};

class Acceptor {
    private:
        Socket _socket;
        EventLoop *_loop; 
        Channel _channel; 
        using AcceptCallback = std::function<void(int)>;
        AcceptCallback _accept_callback;
    private:
        void HandleRead() {
            int newfd = _socket.Accept();
            if (newfd < 0) return;
            if (_accept_callback) _accept_callback(newfd);
        }
        int CreateServer(int port) {
            bool ret = _socket.CreateServer(port);
            assert(ret == true);
            return _socket.Fd();
        }
    public:
        Acceptor(EventLoop *loop, int port): _socket(CreateServer(port)), _loop(loop), _channel(loop, _socket.Fd()) {
            _channel.SetReadCallback(std::bind(&Acceptor::HandleRead, this));
        }
        void SetAcceptCallback(const AcceptCallback &cb) { _accept_callback = cb; }
        void Listen() { _channel.EnableRead(); }
};

class TcpServer {
    private:
        uint64_t _next_id;      
        int _port;
        int _timeout;           
        bool _enable_inactive_release;
        EventLoop _baseloop;    
        Acceptor _acceptor;    
        LoopThreadPool _pool;   
        std::unordered_map<uint64_t, PtrConnection> _conns;

        using ConnectedCallback = std::function<void(const PtrConnection&)>;
        using MessageCallback = std::function<void(const PtrConnection&, Buffer *)>;
        using ClosedCallback = std::function<void(const PtrConnection&)>;
        using AnyEventCallback = std::function<void(const PtrConnection&)>;
        using Functor = std::function<void()>;
        ConnectedCallback _connected_callback;
        MessageCallback _message_callback;
        ClosedCallback _closed_callback;
        AnyEventCallback _event_callback;
    private:
        void RunAfterInLoop(const Functor &task, int delay) {
            _next_id++; _baseloop.TimerAdd(_next_id, delay, task);
        }
        void NewConnection(int fd) {
            _next_id++;
            PtrConnection conn(new Connection(_pool.NextLoop(), _next_id, fd));
            conn->SetMessageCallback(_message_callback);
            conn->SetClosedCallback(_closed_callback);
            conn->SetConnectedCallback(_connected_callback);
            conn->SetAnyEventCallback(_event_callback);
            conn->SetSrvClosedCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
            if (_enable_inactive_release) conn->EnableInactiveRelease(_timeout);
            conn->Established();
            _conns.insert(std::make_pair(_next_id, conn));
        }
        void RemoveConnectionInLoop(const PtrConnection &conn) {
            _conns.erase(conn->Id());
        }
        void RemoveConnection(const PtrConnection &conn) {
            _baseloop.RunInLoop(std::bind(&TcpServer::RemoveConnectionInLoop, this, conn));
        }
    public:
        TcpServer(int port): _port(port), _next_id(0), _enable_inactive_release(false), _acceptor(&_baseloop, port), _pool(&_baseloop) {
            _acceptor.SetAcceptCallback(std::bind(&TcpServer::NewConnection, this, std::placeholders::_1));
            _acceptor.Listen();
        }
        void SetThreadCount(int count) { return _pool.SetThreadCount(count); }
        void SetConnectedCallback(const ConnectedCallback&cb) { _connected_callback = cb; }
        void SetMessageCallback(const MessageCallback&cb) { _message_callback = cb; }
        void SetClosedCallback(const ClosedCallback&cb) { _closed_callback = cb; }
        void SetAnyEventCallback(const AnyEventCallback&cb) { _event_callback = cb; }
        void EnableInactiveRelease(int timeout) { _timeout = timeout; _enable_inactive_release = true; }
        void RunAfter(const Functor &task, int delay) {
            _baseloop.RunInLoop(std::bind(&TcpServer::RunAfterInLoop, this, task, delay));
        }
        void Start() { _pool.Create();  _baseloop.Start(); }
};

inline void Channel::Remove() { return _loop->RemoveEvent(this); }
inline void Channel::Update() { return _loop->UpdateEvent(this); }
inline void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb) {
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}
inline void TimerWheel::TimerRefresh(uint64_t id) {
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}
inline void TimerWheel::TimerCancel(uint64_t id) {
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

class NetWork {
    public:
        NetWork() {
            DBG_LOG("SIGPIPE INIT");
            signal(SIGPIPE, SIG_IGN);
        }
};
static NetWork nw;
#endif