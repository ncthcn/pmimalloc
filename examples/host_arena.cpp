#include <cstdlib>
#include <iostream>
//
#include <pmimalloc/allocator.hpp>

/* Same example as the README: an mlock-pinned host arena with a mimalloc heap
   on top, used through the STL-compatible allocator. */
int main()
{
    resource_builder RB;
    auto rb = RB.pin().on_host();
    using resource_t = decltype(rb.build());

    /* STL-compatible allocator backed by a 128 MiB arena */
    pmimallocator<int, resource_t> alloc(rb, 1ull << 27);

    int* p = alloc.allocate(1);
    *p = 42;
    bool ok = (*p == 42);
    alloc.deallocate(p);

    std::cout << "host arena allocation " << (ok ? "succeeded" : "failed") << "\n";
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
