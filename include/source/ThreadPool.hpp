#ifndef MY_SERVER_THREAD_POOL_HPP
#define MY_SERVER_THREAD_POOL_HPP





#include <condition_variable> 
#include <functional>         
#include <mutex>              
#include <queue>              
#include <thread>             
#include <vector>             
class ThreadPool {
 public:
  
  explicit ThreadPool(int threads_count);

  
  ~ThreadPool();

  
  void SubmitTask(std::function<void()> task);

 private:
 
  void WorkerLoop();

  
  std::vector<std::thread> workers_;

  
  std::queue<std::function<void()>> tasks_;

  
  std::mutex queue_mutex_;

  
  std::condition_variable condition_;

  
  bool stop_;
};

#endif  
