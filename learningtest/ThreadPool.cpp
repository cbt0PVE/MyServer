#include "ThreadPool.hpp" 

ThreadPool::ThreadPool(int threads_count) : stop_(false) { 
  for (int i = 0; i < threads_count; ++i) {
    workers_.emplace_back([this]() { 
      this->WorkerLoop(); 
    });
  }
}


void ThreadPool::SubmitTask(std::function<void()> task) {
  
  {
    
    std::unique_lock<std::mutex> lock(queue_mutex_);

    
    if (stop_) return;

    
    tasks_.push(task);
  } 

  
  condition_.notify_one();
}


void ThreadPool::WorkerLoop() {
  while (true) { 
    std::function<void()> task;

    
    {
     
      std::unique_lock<std::mutex> lock(this->queue_mutex_);

      
      this->condition_.wait(lock, [this]() {
        return this->stop_ || !this->tasks_.empty();
      });

      
      if (this->stop_ && this->tasks_.empty()) {
        return; 

      
      task = std::move(this->tasks_.front());

     
      this->tasks_.pop();
    } 

    
    if (task) {
      task(); 
    }
  } 
}


ThreadPool::~ThreadPool();
 {
  {
    
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  } 
  condition_.notify_all();
  for (std::thread &worker : workers_) {
    if (worker.joinable()) {
      worker.join(); 
    }
  } 
}
