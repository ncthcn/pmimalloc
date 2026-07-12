#pragma once

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include <pmimalloc/builders.hpp>

/* TODO:
    - Fix segfault
    - how to extend the arena size ? Currently we cannot grow a custom arena, mimallloc handles this
      internally, why can't we hook into it for our arenas 

    - numa node stuff, steal it from Fabian and get how to use it

    - UCX
    - MPI

    - in ext_stdmalloc change std::malloc to a pmr::malloc on the context 
      (+ numa stuff ?)

    - Max arena sizes (s<<32), currently mmap fails if we ask for too much, does it change on daint,
      does it change if we use module load craype-hugepages1G
      Same problem for mlock, we can only pin so many pages, can we use huge pages to improve
      the problem ?
    
    - Run test on daint, more threads, more memory, more graphs, huge pages, what do we learn ?
            
    - LRU Cache, registration of user memory "on the fly" would benefit from an LRU memory page 
      cache. Steal code from MPI if time permits.
*/

/** @brief STL-compatible allocator serving allocations of T from a composed
 * resource (see builders.hpp). Holds the resource in a shared_ptr, so copies
 * are cheap and share the same arena.
 * @tparam T value type
 * @tparam Resource composed resource type, e.g. decltype(rb.build()) */
template <typename T, typename Resource>
class pmimallocator
{
public:
    /* Types */
    using this_type = pmimallocator<T, Resource>;
    using resource_type = Resource;
    using shared_resource = std::shared_ptr<Resource>;
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    template <typename U>
    struct rebind
    {
        using other = pmimallocator<U, Resource>;
    };

    pmimallocator() noexcept = default;
    pmimallocator(const pmimallocator& other) noexcept = default;
    pmimallocator(pmimallocator&&) noexcept = default;

    /* Construct from a shared_ptr */
    pmimallocator(shared_resource r) noexcept
      : m_sptr_resource{r}
    {
    }

    /* Construct from a resource_builder */
    template <typename... Args>
    pmimallocator(resource_builder<resource_type> rb, Args... a) noexcept
      : m_sptr_resource{rb.sbuild(std::move(a)...)}
    {
    }

    /* Construct from a resource */
    pmimallocator(const resource_type& r) noexcept
      : m_sptr_resource{std::make_shared<resource_type>(r)}
    {
    }

    template <typename U>
    pmimallocator(const pmimallocator<U, Resource>& other)
      : m_sptr_resource{other.m_sptr_resource}
    {
    }

    pmimallocator& operator=(const pmimallocator&) noexcept = default;
    pmimallocator& operator=(pmimallocator&&) noexcept = default;

    pmimallocator select_on_container_copy_construction()
    {
        return *this;
    }

    /* Destructor */
    ~pmimallocator() {}

    /** @brief Allocate storage for n objects of T from the arena. */
    [[nodiscard]] T* allocate(const std::size_t n)
    {
        if (n > m_sptr_resource->get_size() / sizeof(T))
        {
            /* Check for overflow */
            throw std::bad_alloc();
        }
        return static_cast<pointer>(m_sptr_resource->allocate(n * sizeof(T)));
    }

    /** @brief Return storage to the arena; n may be omitted (mimalloc backend). */
    void deallocate(T* p, std::size_t n = 0)
    {
        void* tmp = static_cast<void*>(p);
        return m_sptr_resource->deallocate(tmp, n);
    }

    /** @brief Remote key/offset of ptr for RMA, when the resource was built
     * with register_memory() and a network backend. */
    auto get_key(T* ptr)
    {
        return m_sptr_resource->get_key(ptr);
    }

    /* Max size */

    size_type max_size() const noexcept
    {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    friend bool operator==(const pmimallocator& lhs, const pmimallocator& rhs)
    {
        return (lhs.m_sptr_resource == rhs.m_sptr_resource);
    }

    friend bool operator!=(const pmimallocator& lhs, const pmimallocator& rhs)
    {
        return (lhs.m_sptr_resource != rhs.m_sptr_resource);
    }

private:
    template <typename U, typename R>
    friend class pmimallocator;

    shared_resource m_sptr_resource;
};

// TODO (optional) : All of this

/* Construct */

// void construct( pointer p, const_reference val );

// template <class U, class... Args>
// void construct(U* p, Args&&... args) {
//     ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
// }

/* Destroy */

// void destroy( pointer p );

// template <class U>
// void destroy(U* p) {
//     p->~U();
// }

/* Address */

// pointer address( reference x ) const;

// pointer address( reference x ) const noexcept;

// const_pointer address( const_reference x ) const;

// const_pointer address( const_reference x ) const noexcept;

/* Allocate at least */

// [[nodiscard]] constexpr std::allocation_result<T*, std::size_t>
// allocate_at_least( std::size_t n );
