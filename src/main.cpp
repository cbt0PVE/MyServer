#include "../include/ThreadPool.hpp" 
#include <chrono>         
#include <iostream>       

// int main() { 
//   ThreadPool pool(3);

  
//   for (int i = 1; i <= 10; ++i) {
    
//     pool.SubmitTask([i]() {
      
//       std::cout << "任务 " << i 
//                 << " 正在被线程 " << std::this_thread::get_id() 
//                 << " 执行" << std::endl;
//     });
//   } 
//   std::this_thread::sleep_for(std::chrono::seconds(1));

  
//   std::cout << "主线程执行完毕，工厂准备下班析构！" << std::endl;
  
//   return 0; 
// }
