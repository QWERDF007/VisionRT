#include "visionrt/contracts/DataRef.h"
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>
using namespace visionrt;
void require(bool v) {if(!v) throw std::runtime_error("check failed");}
int main(){
 int destroyed=0;
 auto dependency=std::shared_ptr<int>(new int(7),[&](int*p){++destroyed;delete p;});
 std::weak_ptr<int> weak=dependency;
 std::optional<DataRef<std::vector<int>>> retained;
 {
  DataRef<std::vector<int>> data({1,2},{dependency});
  auto copy=data;
  copy.get()[0]=9;
  require(data.get()[0]==9 && &data.get()==&copy.get());
  retained=data.retainedBy(std::make_shared<int>(3));
  dependency.reset();
  require(!weak.expired());
 }
 require(!weak.expired() && retained->get()[0]==9);
 retained.reset();
 require(weak.expired() && destroyed==1);
 std::cout<<"PASS: shared writable payload; retained dependency lifetime; last-owner release once\n";
}
