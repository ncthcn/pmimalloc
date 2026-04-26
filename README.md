# PMimalloc — Memory Allocator with Pluggable Memory Resources and Optional Registration, Pinning, and Mirroring

PMimalloc is a C++ library that composes memory resources out of orthogonal layers:
- Base memory (host, host+device, user memory, etc.)
- Optional pinning (none, mlock, CUDA-pinned)
- Optional registration backend (libfabric, UCX, or none)
- Optional “mirroring” behavior (host/device pointer translation)
- Allocator backends (mimalloc arena-backed or std::pmr pool-backed)

It provides:
- A resource builder that produces nested, policy-based resources for allocation
- A handler for interacting with registered memory (RMA-style offsets/keys)
- Optional NUMA-aware allocation/utilities
- Optional logging
- Tests to exercise host and mirrored allocations

This README explains how to build, configure, and use the library and its tests.

## Contents

- Features
- Architecture at a Glance
- Build and Dependencies
- Configuration Options
- Quick Start
  - Minimal build
  - With mimalloc
  - With libfabric
  - With UCX
  - With CUDA pinning
  - With NUMA tools and logging
- Usage Examples
  - Resource builder (host memory)
  - Mirrored allocations (host+device)
  - Handlers (registered memory)
  - Using std::pmr with resources
- Tests
- Notes on UCX integration
- Project Structure

## Features

- Composable memory resource chain with layers: Mirroring → Resource → Context → Pinning → Memory → Base
- Pluggable allocator backend: mimalloc (arena) or C++ std::pmr pools
- Optional memory pinning (mlock, CUDA)
- Optional memory registration backends (libfabric now; UCX scaffolding present)
- NUMA tools for querying nodes, allocating/freeing pages on specific nodes
- Logging facility with per-thread buffering

## Architecture at a Glance

The core idea is to nest types to form a single “resource”:

```
Mirrored<Resource<Context<Pinned<Memory<Base>>,[Backend]>, Allocator>>
```

Default templates (see include/PMimalloc/builders.hpp) define sensible layers that you can override fluently with a builder.

- Base: include/PMimalloc/base.hpp — holds address/size/numa node
- Memory: include/PMimalloc/memory.hpp (host, host+device, user memory, etc.)
- Pinning: include/PMimalloc/pinning.hpp (not_pinned, pinned, cuda_pinned)
- Context: include/PMimalloc/context.hpp (ties memory to registration backend)
- Resource: include/PMimalloc/resource.hpp (wraps allocator, inherits Context)
- Mirroring: include/PMimalloc/mirroring.hpp (maps host allocations to device range)
- Allocators: include/PMimalloc/ext_mimalloc.hpp or include/PMimalloc/ext_stdmalloc.hpp
- NUMA utilities: include/PMimalloc/numa.hpp, src/numa.cpp
- Logging: include/PMimalloc/log.hpp, src/log.cpp

## Build and Dependencies

The project uses CMake. At a minimum you need:
- A C++17 compiler
- fmt (headers and library)
- For optional features:
  - mimalloc: libmimalloc and headers
  - libfabric: libfabric and headers
  - UCX: UCX (ucx/ucp headers and libs)
  - CUDA: CUDA toolkit for cuda_runtime.h (if using CUDA pinning)
  - libnuma (Linux): for NUMA features

Example system packages (Linux):
- fmt: libfmt-dev (or build from source)
- libfabric: libfabric-dev
- UCX: libucx-dev
- mimalloc: mimalloc-dev
- NUMA: libnuma-dev
- CUDA (optional): CUDA toolkit

## Configuration Options

CMake options (toggle ON/OFF as needed):
- ENABLE_LOGGING: Enable internal logging macros (default OFF)
- PMimALLOC_WITH_MIMALLOC: Enable mimalloc backend (default OFF)
- WITH_LIBFABRIC: Enable libfabric backend and registration (default OFF)
- WITH_UCX: Enable UCX backend stubs (default OFF)
- WITH_CUDA: Enable CUDA pinning (default OFF)
- NUMA_THROWS: Make NUMA tools throw on errors (default OFF)

These options control preprocessor guards used across the code.

## Quick Start

### Minimal Build (std::pmr backend, no registration)
- Dependencies: fmt
- Configure/build:

```
cmake -S . -B build -DENABLE_LOGGING=ON -DPMimALLOC_WITH_MIMALLOC=OFF -DWITH_LIBFABRIC=OFF -DWITH_UCX=OFF -DWITH_CUDA=OFF
cmake --build build -j
```

Run tests:
```
ctest --test-dir build
```

### Build with mimalloc
- Requires mimalloc library and headers.
- Configure:

```
cmake -S . -B build -DPMimALLOC_WITH_MIMALLOC=ON
cmake --build build -j
```

### Build with libfabric registration
- Requires libfabric.
- Configure:

```
cmake -S . -B build -DWITH_LIBFABRIC=ON
cmake --build build -j
```

Note: The default builder uses a “backend_none”. To actually register memory, use the builder method register_memory() (see examples) or set WITH_LIBFABRIC and switch builder context.

### Build with UCX (experimental scaffolding)
- Requires UCX. Current UCX integration includes region/handle definitions and an rma_context scaffold. It is not yet wired into the default builders.
- Configure:

```
cmake -S . -B build -DWITH_UCX=ON
cmake --build build -j
```

### Build with CUDA pinning
- Requires CUDA toolkit present.
- Configure:

```
cmake -S . -B build -DWITH_CUDA=ON
cmake --build build -j
```

Then use builder.cuda_pin() to get CUDA-pinned host memory.

### Use NUMA tools and logging
- Logging:

```
cmake -S . -B build -DENABLE_LOGGING=ON
```

- NUMA tools are compiled by default on Linux. To make failures throw:

```
cmake -S . -B build -DNUMA_THROWS=ON
```

## Usage Examples

Note: These examples assume you include the appropriate public headers used in your build (builders, resource, mirroring, pinning, etc.).

### Resource Builder: Host Memory

Allocate a host-only arena and perform allocations with a chosen backend.

```cpp
#include <pmimalloc/builders.hpp>

int main() {
    // Default resource: simple<Resource<Context<not_pinned<host_memory<base>>, backend_none>, ext_mimalloc>>
    // Replace allocator with std::pmr pool backend:
    auto builder = resource_builder<>().use_stdmalloc().on_host();

    // Build a resource with a given arena memory (addr, size)
    // Typically you pass a host arena ptr and size to the Resource constructor
    // If your resource allocates its own arena internally, pass the size instead.
    std::size_t arena_size = 1ull << 26; // 64 MiB
    auto res = builder.build(arena_size);

    // Allocate some memory
    void* p = res.allocate(4096);
    // ...
    res.deallocate(p, 4096); // some allocators require the size
}
```

### Mirrored Allocations: Host + Device

Map host allocations to a device-mirrored range. You allocate on the host side; the mirroring layer provides an equivalent device pointer via a fixed offset into the device arena.

```cpp
#include <pmimalloc/builders.hpp>

int main() {
    auto builder = resource_builder<>()
        .on_host_and_device(); // mirrored layer on top of host+device memory

    // Build with total arena size
    std::size_t arena_size = 1ull << 27; // 128 MiB
    auto res = builder.build(arena_size);

    // Allocate on host, get device-mirrored pointer
    void* dev_ptr = res.allocate(1<<20); // 1 MiB
    // ...
    res.deallocate(dev_ptr, 1<<20);
}
```

Note: The mirroring layer expects the underlying memory to expose both host and device arena addresses of equal size. Ensure the underlying memory resource initializes both sides accordingly.

### Handlers: Registered Memory

For RMA backends (e.g., libfabric), a handler provides a consistent interface to query keys and offsets.

```cpp
#include <pmimalloc/builders.hpp>
#include <pmimalloc/handler.hpp>

int main() {
    // Build a context/handler chain with memory registration enabled (backend)
    auto h_builder = handler_builder<>()
        .register_memory()  // WITH_LIBFABRIC or other backend must be enabled at build time
        .pin();             // Optional: mlock pinning

    std::size_t arena_size = 1ull << 27;
    auto h = h_builder.build(arena_size);

    // Get remote key for a pointer within the arena
    int* arr = static_cast<int*>(h.get_address());
    auto key = h.get_key(arr + 128); // offset within arena

    // Use key.remote_key and key.offset for RMA operations in your transport.
    (void)key;
}
```

### Using std::pmr with Resources

You can wrap a resource’s arena into a C++ polymorphic allocator.

```cpp
#include <pmimalloc/builders.hpp>
#include <memory_resource>
#include <vector>

int main() {
    auto r = resource_builder<>().use_stdmalloc().on_host().build(1<<26);
    std::pmr::monotonic_buffer_resource mbuf(r.get_address(), r.get_size());
    std::pmr::polymorphic_allocator<int> pa(&mbuf);
    std::pmr::vector<int> v(pa);
    v.resize(1000);
}
```

## Tests

The repository provides several test executables (see test/test_host.cpp, test/test_mirror.cpp and test/CMakeLists.txt). Build and run:

```
cmake -S . -B build -DENABLE_LOGGING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- test_host: Exercises a host-only allocator across arenas/threads.
- test_mirror: Exercises mirrored allocations.

You can adjust arena sizes and number of allocations in the tests as needed for your system.

## Notes on UCX Integration

The UCX backend in `src/ucx/` provides region/handle scaffolding and an rma_region that uses `ucp_mem_map` to register memory, optionally for device memory. A minimal `rma_context` stub is provided for creating regions but is not yet integrated into the default builder flow. To use UCX with RMA, you will typically:
- Create and manage a `ucp_context_h` externally
- Use `rma_region` to map/unmap host/device regions
- Wire the registration stage into the builder (similar to libfabric’s backend) if desired

Until that wiring is complete, you can use `rma_region` directly in UCX-based applications.
