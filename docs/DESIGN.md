# pmimalloc — Design Notes

## Problem

HPC applications that communicate over RDMA networks (libfabric, UCX) or with
GPUs need memory with special properties: pinned so the OS cannot page it out,
registered with the NIC so remote peers can address it, or mirrored so a host
allocation has a matching device-side buffer. These properties are expensive to
establish (`mlock`, `fi_mr_reg`, `cudaMalloc` are all orders of magnitude slower
than a malloc fast path) and are usually managed by ad-hoc code paths separate
from the application's allocator.

pmimalloc's goal is to pay these costs **once per arena, not once per
allocation**: preallocate a large slab, pin/register/mirror the whole slab up
front, then serve individual allocations out of it at general-purpose-allocator
speed. The benchmark in the README (allocation throughput and peak-RSS overhead
vs. mimalloc, jemalloc, tcmalloc across 1–36 threads) checks that the layering
does not compromise the fast path.

## Design

A resource is a compile-time nest of policy classes, each inheriting from the
next (see `include/pmimalloc/builders.hpp`):

```
Mirrored -- Resource -- Context -- Pinned -- Memory -- Base
                \           \
             Allocator     Backend
```

- **Base / Memory** acquire the slab (host `mmap`, host+device, or user-provided
  memory) and record address, size, and NUMA node.
- **Pinned** optionally `mlock`s or CUDA-pins the slab at construction.
- **Context** optionally registers the slab with a network backend and exposes
  `get_key(ptr)` — the remote key and offset a peer needs for RMA.
- **Resource** attaches the allocator that carves the slab into allocations.
- **Mirrored** maps each host allocation to a device pointer at the same offset
  in an equally-sized device arena — pointer translation is a constant offset,
  with no per-allocation device bookkeeping.

`resource_builder` (in `builders.hpp`) makes composition ergonomic: each fluent
call (`pin()`, `register_memory()`, `on_host_and_device()`, ...) is `constexpr`
and returns a builder for a new nested type, using the `replace_resource_t`
metafunction to splice a layer into the nest by position. `build(size)`
constructs the whole chain; `pmimallocator<T, Resource>` wraps the resource in
an STL-compatible allocator holding a `shared_ptr`, so containers can copy it
cheaply.

## Allocator core

The default backend (`ext_mimalloc.hpp`) delegates to
[mimalloc](https://github.com/microsoft/mimalloc): the preallocated slab is
handed to mimalloc with `mi_manage_os_memory_ex(..., exclusive = true)`, and
each thread lazily gets its own heap inside that arena via
`mi_heap_new_in_arena` (managed by `indexed_tl_ptr`, a thread-local pointer
indexed per arena instance). Per-thread heaps avoid lock contention on the
allocation path; cross-thread frees are handled by mimalloc itself. All
allocation algorithms (size classes, page management, free-list sharding) are
mimalloc's — pmimalloc contributes the arena provisioning and the layers around
it.

An alternative backend (`ext_stdmalloc.hpp`) serves allocations from
`std::pmr` pools over the same slab, useful as a portable baseline.

## Key decisions and trade-offs

- **Compile-time composition, no virtual dispatch.** Every layer combination is
  a distinct concrete type, so the hot path is fully inlinable. The costs are
  template-heavy code, longer compile times, and resource types that cannot be
  chosen at runtime.
- **Fixed-size preallocated arenas.** Pinning and registration are amortized
  and allocations never surprise the NIC with unregistered memory. The
  trade-off: arenas cannot grow (mimalloc manages growth internally for its own
  arenas, but a user-provided arena is fixed), and `mmap`/`mlock` limits cap
  the size. Callers must size arenas up front or create more of them.
- **Mirroring by fixed offset.** Host and device arenas have equal size, so
  device pointers are `host_ptr + constant`. This wastes device memory if the
  workload is asymmetric, but makes translation free and lock-less.
- **Reuse mimalloc rather than write an allocator.** The benchmark exists to
  validate this choice: pmimalloc should track mimalloc closely, and any gap is
  layering overhead.

## What I would do next

(From `include/pmimalloc/allocator.hpp` TODOs and the README status section.)

- Growable arenas: hook into mimalloc's arena growth instead of fixed slabs.
- Wire the UCX backend (`src/ucx/`) into the builders; MPI backend.
- An LRU page cache so user memory registered "on the fly" is amortized too.
- Huge-page support to lift `mmap`/`mlock` size limits.
- Rerun the benchmark with recorded methodology (machine, versions, workload)
  and add latency percentiles alongside wall-clock totals.
