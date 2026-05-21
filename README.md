# MyServer - High-Performance Concurrent Network Server (高性能并发网络服务器)

[English] A high-performance, asynchronous concurrent network server framework built from scratch using **Modern C++ (C++11)**. The core architecture is inspired by the well-known **Muduo** network library, operating on the **Multi-Reactor** event-driven model.

[中文] 一个基于 **现代 C++ (C++11)** 从零实现的高性能、异步并发网络服务器框架。核心架构受著名网络库 **Muduo** 启发，采用 **Multi-Reactor**（多事件循环）事件驱动模型。

> **Environment / 开发环境**: Developed and compiled entirely via macOS Terminal toolchain (`clang++`, `CMake`, `lldb`). 全程基于 macOS 终端工具链开发与编译。

---

## 🚀 Core Features / 核心特性

### 1. High-Performance ThreadPool / 高性能线程池
* **EN**: A robust C++11 task-dispatching center utilizing `std::condition_variable`, movable task queues, and perfect forwarding to achieve zero-copy task submission.
* **ZH**: 基于 `std::condition_variable` 实现的 C++11 线程池，任务队列支持移动语义，利用完美转发实现任务的零拷贝提交。

### 2. Asynchronous Multi-Reactor / 异步多事件循环
* **EN**: Driven by macOS native `kqueue` multiplexing, adopting the "One Loop Per Thread" architecture to maximize multi-core CPU utilization.
* **ZH**: 采用 macOS 原生的 `kqueue` 多路复用驱动，贯彻 "One Loop Per Thread" 架构，榨干多核 CPU 性能。

### 3. Non-blocking I/O & Buffers / 非阻塞 I/O 与缓冲区
* **EN**: Designed with thread-safe ring buffers to handle non-blocking I/O read/write, elegantly solving TCP stick and half package problems.
* **ZH**: 配合非阻塞 I/O 设计了线程安全的环形缓冲区，优雅解决 TCP 粘包、半包问题。

### 4. Async Logger / 零拷贝异步日志
* **EN**: A dual-buffer asynchronous logging system utilizing C++11 move semantics to ensure minimal disk I/O blocking on working threads.
* **ZH**: 采用双缓冲区（Dual-Buffer）异步日志系统，利用 C++11 移动语义，确保业务线程几乎不受磁盘 I/O 阻塞。

---

## 📁 Directory Tree / 项目目录树
```text
MyServer/
├── CMakeLists.txt      # CMake 自动化构建脚本
├── .gitignore          # Git 忽略文件规则
├── README.md           # Project documentation (中英文文档)
├── include/            # Header files / 头文件 (.hpp)
│   └── ThreadPool.hpp
├── src/                # Source files / 源文件 (.cpp)
└── logs/               # Server runtime logs / 日志存放目录
