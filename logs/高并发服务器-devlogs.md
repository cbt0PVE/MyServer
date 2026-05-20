# 【仿 muduo 服务器】开发日志 - Day X

### 1. 今日开发模块
* 模块名称：[例如：EventLoop / EpollPoller / Channel / Buffer]
* 核心目标：实现 Non-blocking IO + IO Multiplexing 的核心事件分发循环。

### 2. 架构设计与逻辑思索
* （用文字或字符画简述今天模块的关系。例如：Channel 是 EventLoop 和 Poller 的桥梁，封装了 fd 和感兴趣的 events...）

### 3. 【核心】踩坑与性能调优（Bug & Debug）
* **现象/问题：** 压测时程序偶尔崩溃，报 `Segmentation fault`，或者发现 CPU 飙升到 100% 但不处理事件。
* **原因排查：** 
  * 通过 GDB 调试 / valgrind 内存检测，发现 `Channel` 在析构时，其注册在 `Poller` 中的原生指针变成了野指针。
  * 或者是：在多线程下，由子线程去唤醒主线程的 `EventLoop` 时，由于未加锁导致 `vector` 扩容时引发线程不安全。
* **解决方案：** 
  * 引入 `std::shared_ptr` 和 `std::weak_ptr` 进行生命周期管理，确保 `Channel` 析构时安全注销。
  * 关键代码改进：
  ```cpp
  // 优化后的唤醒/事件注册逻辑