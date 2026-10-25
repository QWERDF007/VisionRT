#include <future>
#include <iostream>
#include <chrono>
int main() {
 std::promise<void> notification, allowReturn;
 auto permit=allowReturn.get_future();
 auto work=std::async(std::launch::async,[&]{ notification.set_value(); permit.wait(); return 42; });
 notification.get_future().wait();
 bool pending=work.wait_for(std::chrono::seconds(0))!=std::future_status::ready;
 allowReturn.set_value();
 if (!pending || work.get()!=42) return 1;
 std::cout << "PASS: notification can be consumed before async future is ready; interleaving model only, not Qt integration test\n";
}
