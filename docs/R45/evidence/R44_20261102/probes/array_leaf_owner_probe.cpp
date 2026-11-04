#include <visionrt/contracts/Array.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
using namespace visionrt;
template<class T> concept CanReadElements = requires(Array a) { a.template elements<T>(); };
static_assert(CanReadElements<std::int64_t>);
static_assert(CanReadElements<double>);
static_assert(!CanReadElements<bool>);
static_assert(!CanReadElements<std::vector<std::int64_t>>);
static_assert(!CanReadElements<int*>);
int main() {
    bool dependencyReleased=false;
    std::unique_ptr<Array> keeper;
    {
        Owner dependency(new int(1), [&](void* p) { dependencyReleased=true;delete static_cast<int*>(p); });
        auto source=Array::dense(DType::Int64,{2},std::vector<std::int64_t>{17,23});
        auto retained=source.retainedBy(dependency);
        keeper=std::make_unique<Array>(retained);
    }
    assert(!dependencyReleased);
    keeper->elements<std::int64_t>()[1]=42;
    assert(keeper->elements<std::int64_t>()[1]==42);
    keeper.reset();assert(dependencyReleased);
    std::cout << "Current Contracts: scalar constraints and complete Array lifetime passed\n";
}
