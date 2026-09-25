#include "graphics/draw/MessageLineLayout.h"
#include "input/QuickHeart.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

static bool measureAllocations;
static size_t allocatedBytes;
void *operator new(size_t size)
{
    if (measureAllocations)
        allocatedBytes += size;
    if (void *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    std::free(p);
}

int main()
{
    using graphics::MessageLineLayout::calculate;
    const std::vector<std::string> mixed = {"header", "hello", QuickHeart::TEXT, "last", "header", "message"};
    const std::vector<bool> headers = {true, false, false, false, true, false};
    assert((calculate(mixed, headers, 12, 6) == std::vector<int>{13, 12, 15, 14, 13, 8}));
    assert(calculate({}, {}, 12, 6).empty());

    std::vector<std::string> lines(100, QuickHeart::TEXT);
    std::vector<bool> body(100, false);
    measureAllocations = true;
    const auto heights = calculate(lines, body, 12, 6);
    measureAllocations = false;
    assert(heights.size() == 100 && allocatedBytes <= 100 * sizeof(int));
    for (int h : heights)
        assert(h == 15);
    std::puts("Message layout: spacing preserved; 100 rows allocate only the output heights (400 bytes)");
}
