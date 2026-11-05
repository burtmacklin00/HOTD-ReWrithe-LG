// The heap and the task creation (proofs-7): 0x14640 .. 0x15080, first written in the runtime (src/game/runtime.cpp,
// chapter.cpp), moved here for their capture proof. Arcade forms only (the PC's are replayed: ProofPc). The task
// loop itself (task_group_run 0x14D40, task_group_task 0x14CE0, task_exit 0x150D0, task_unlink 0x15110: setjmp /
// longjmp through 0x23B00 / 0x23B20) stays in the runtime. Declarations: decl/core_bounds.h, decl/support_b.h.
//
// The heap: blocks of words from 0x543B00 (the sentinel block, word index 0); header word = size in words (negative =
// free), +1 / +2 = the free list (word indices; a used block's +1 / +2 hold its own payload address: the ring of
// blocks freed together), footer = the size again. Payload = header + 12. Header bit 30: the block is linked into
// another block's ring.
#include "port/game.h"

namespace hotd::port {

namespace heap {
constexpr uint32_t kBase = 0x543B00;
constexpr uint32_t kSerial = 0x5122A4;       // the task serial counter
constexpr uint32_t kTaskMagic = 0x80018691;
constexpr uint32_t kTaskNop = 0x14E90;       // the default exit routine (ret)
} // namespace heap

// 0x14640 heap_reset: one free block of 0x2F13B words from index 3, the end marker at 0x5FFFF8, the task serial
// counter at 0x10000; then skin_buffers_alloc
template <class P> void Game<P>::heap_reset()
{
    if constexpr (!P::pc) {
        mem.set32(0x543B04, 4);
        mem.set32(0x543B08, 4);
        mem.set32(0x543B00, 4);
        mem.set32(0x543B0C, 4);
        mem.set32(0x5FFFFC, 1);
        mem.set32(0x543B14, 0);
        mem.set32(0x543B10, 0xFFFD0EC5u);
        mem.set32(0x543B18, 0);
        mem.set32(0x5122A0, 0);
        mem.set32(0x5FFFF8, 0xFFFD0EC5u);
        mem.set32(heap::kSerial, 0x10000);
        skin_buffers_alloc();
    }
}

// 0x35090 skin_buffers_alloc: three 0x3480-byte buffers (0x51FDC8 ..)
template <class P> void Game<P>::skin_buffers_alloc()
{
    if constexpr (!P::pc) {
        // (Survival Mode: 0x348 bytes more each for every body slot past 16, game.h extraBodySlots; 0 otherwise)
        const uint32_t size = 0x3480 + uint32_t(extraBodySlots) * 0x348;
        mem.set32(0x51FDC8, mem_alloc(size));
        mem.set32(0x51FDCC, mem_alloc(size));
        mem.set32(0x51FDD0, mem_alloc(size));
    }
}

// 0x14710 mem_alloc (size in bytes): first fit from the free list's head, split when more than 4 words remain; none
// big enough: "memory allocation error !!" (text 0x146F0) and the endless loop
template <class P> uint32_t Game<P>::mem_alloc(uint32_t size)
{
    if constexpr (P::pc) {
        (void)size;
        return 0;
    } else {
        auto rd = [&](uint32_t a) { return int32_t(mem.u32(a)); };
        auto wr = [&](uint32_t a, int32_t v) { mem.set32(a, uint32_t(v)); };
        const int32_t n = (int32_t(size + 3) >> 2) + 4;
        uint32_t b = heap::kBase;
        int32_t have;
        for (;;) {
            b = heap::kBase + uint32_t(rd(b + 8)) * 4;
            have = -rd(b);
            if (have >= n)
                break;
            if (rd(b + 8) == 0) {
                text_cursor(16, 23);
                text_printf(0x146F0);
                debug_halt();
                return 0;
            }
        }
        const int32_t prev = rd(b + 4), next = rd(b + 8);
        const int32_t rest = have - n;
        wr(b, have);
        const uint32_t pb = heap::kBase + uint32_t(prev) * 4, nb = heap::kBase + uint32_t(next) * 4;
        if (rest <= 4) {
            wr(pb + 8, next);
            wr(nb + 4, rd(b + 4));
            wr(b + uint32_t(rd(b)) * 4 - 4, rd(b));
        } else {
            const uint32_t r = b + uint32_t(n) * 4;
            wr(r, -rest);
            wr(r + 4, rd(b + 4));
            wr(r + 8, rd(b + 8));
            wr(b + uint32_t(n + rest) * 4 - 4, rd(r));
            wr(pb + 8, rd(pb + 8) + n);
            wr(nb + 4, rd(nb + 4) + n);
            wr(b, n);
            wr(b + uint32_t(n) * 4 - 4, n);
        }
        const uint32_t p = b + 12;
        mem.set32(b + 4, p);
        mem.set32(b + 8, p);
        return p;
    }
}

// 0x14810 heap_block_insert (block, into): links `block` into the ring of `into` (before it) and marks `into` linked
// (bit 30)
template <class P> void Game<P>::heap_block_insert(uint32_t block, uint32_t into)
{
    if constexpr (!P::pc) {
        mem.set32(mem.u32(into - 8) - 4, mem.u32(block - 4));
        mem.set32(mem.u32(block - 4) - 8, mem.u32(into - 8));
        mem.set32(block - 4, into);
        const uint32_t h = mem.u32(into - 12);
        mem.set32(into - 8, block);
        mem.set32(into - 12, h | 0x40000000u);
    } else {
        (void)block, (void)into;
    }
}

// 0x149B0 mem_free: a linked block (bit 30) is unlinked from its ring and freed; otherwise the whole ring starting at
// p is freed
template <class P> void Game<P>::mem_free(uint32_t p)
{
    if constexpr (!P::pc) {
        if ((mem.u32(p - 12) >> 30) & 1) {
            mem.set32(p - 12, mem.u32(p - 12) & ~0x40000000u);
            mem.set32(mem.u32(p - 8) - 4, mem.u32(p - 4));
            mem.set32(mem.u32(p - 4) - 8, mem.u32(p - 8));
            heap_block_free(p);
            return;
        }
        uint32_t cur = p;
        for (;;) {
            const uint32_t next = mem.u32(cur - 4);
            heap_block_free(cur);
            if (next == p)
                return;
            cur = next;
            mem.set32(cur - 12, mem.u32(cur - 12) & ~0x40000000u);
        }
    } else {
        (void)p;
    }
}

// 0x14A50 heap_block_free: frees a block, merging with free neighbours (footer of the one before, header of the one
// after); a merged block bigger than the free list's first block moves to the front
template <class P> void Game<P>::heap_block_free(uint32_t p)
{
    if constexpr (!P::pc) {
        constexpr uint32_t kHeap = heap::kBase;
        auto rd = [&](uint32_t a) { return int32_t(mem.u32(a)); };
        auto wr = [&](uint32_t a, int32_t v) { mem.set32(a, uint32_t(v)); };
        const int32_t before = rd(p - 16);
        uint32_t h = p - 12;
        const int32_t idx = int32_t((h - kHeap) >> 2);
        const int32_t size = rd(h);
        const int32_t after = rd(h + uint32_t(size) * 4);
        if (before >= 0 && after >= 0) {
            // neither neighbour free: to the end of the free list
            wr(h, -size);
            wr(h + uint32_t(size) * 4 - 4, -size);
            wr(p - 4, 0);
            wr(p - 8, rd(kHeap + 4));
            wr(kHeap + 8 + uint32_t(rd(kHeap + 4)) * 4, idx);
            wr(kHeap + 4, idx);
        } else if (before < 0 && after < 0) {
            // both free: the next one leaves the list, all three become the one before
            const uint32_t nb = h + uint32_t(size) * 4;
            wr(kHeap + 8 + uint32_t(rd(nb + 4)) * 4, rd(nb + 8));
            wr(kHeap + 4 + uint32_t(rd(nb + 8)) * 4, rd(nb + 4));
            const uint32_t pb = h + uint32_t(rd(h - 4)) * 4;
            const int32_t merged = rd(pb) + (rd(h + uint32_t(rd(h)) * 4) - rd(h));
            h = pb;
            wr(pb, merged);
            wr(pb + uint32_t(-merged) * 4 - 4, merged);
        } else if (after < 0) {
            // the next one is free: this block takes its place in the list
            const uint32_t nb = h + uint32_t(size) * 4;
            const int32_t merged = rd(nb) - size;
            wr(h, merged);
            wr(h + uint32_t(-merged) * 4 - 4, merged);
            wr(kHeap + 8 + uint32_t(rd(nb + 4)) * 4, idx);
            wr(kHeap + 4 + uint32_t(rd(nb + 8)) * 4, idx);
            wr(h + 4, rd(nb + 4));
            wr(h + 8, rd(nb + 8));
        } else {
            // the one before is free: it grows
            const uint32_t pb = h + uint32_t(before) * 4;
            const int32_t merged = rd(pb) - size;
            h = pb;
            wr(pb, merged);
            wr(pb + uint32_t(-merged) * 4 - 4, merged);
        }
        // a block bigger than the list's first moves to the front
        if (rd(h) >= rd(kHeap + uint32_t(rd(kHeap + 8)) * 4))
            return;
        wr(kHeap + 8 + uint32_t(rd(h + 4)) * 4, rd(h + 8));
        wr(kHeap + 4 + uint32_t(rd(h + 8)) * 4, rd(h + 4));
        wr(h + 4, 0);
        wr(h + 8, rd(kHeap + 8));
        const int32_t i = int32_t((h - kHeap) >> 2);
        wr(kHeap + 4 + uint32_t(rd(kHeap + 8)) * 4, i);
        wr(kHeap + 8, i);
    } else {
        (void)p;
    }
}

// 0x14C10 task_group_create: a 0x24-byte head that runs the start table once (task_group_task 0x14CE0), run at once
template <class P> uint32_t Game<P>::task_group_create(uint32_t table)
{
    if constexpr (P::pc) {
        (void)table;
        return 0;
    } else {
        const uint32_t g = mem_alloc(0x24);
        mem.set32(g + 0x10, g);
        mem.set32(g + 4, heap::kTaskNop);
        mem.set32(g, 0x14CE0);
        mem.set32(g + 0x14, g);
        mem.set32(g + 0x20, table);
        mem.set32(g + 8, 0xFFFFFFFFu);
        task_group_run(g);
        return g;
    }
}

// Tasks: +0 routine, +4 exit routine, +8 serial, +0xC magic, +0x10 / +0x14 the ring (prev / next), +0x18 the creating
// task, +0x1C its +8.

// 0x14EA0 task_create: a new task at the end of the current group's ring
template <class P> uint32_t Game<P>::task_create(uint32_t routine, uint32_t size)
{
    if constexpr (P::pc) {
        (void)routine, (void)size;
        return 0;
    } else {
        const uint32_t p = mem_alloc(size);
        const uint32_t grp = mem.u32(G::task_group_current);
        mem.set32(p + 0x14, grp);
        mem.set32(p + 0x10, mem.u32(grp + 0x10));
        mem.set32(mem.u32(grp + 0x10) + 0x14, p);
        mem.set32(grp + 0x10, p);
        mem.set32(p, routine);
        const uint32_t cur = mem.u32(G::task_current);
        mem.set32(p + 0x18, cur);
        mem.set32(p + 4, heap::kTaskNop);
        mem.set32(p + 0x1C, mem.u32(cur + 8));
        const uint32_t serial = mem.u32(heap::kSerial);
        mem.set32(heap::kSerial, serial + 1);
        mem.set32(p + 0xC, heap::kTaskMagic);
        mem.set32(p + 8, serial);
        return p;
    }
}

// 0x14FA0 task_create_child: a new task right after the current one (it runs later in the same frame)
template <class P> uint32_t Game<P>::task_create_child(uint32_t routine, uint32_t size)
{
    if constexpr (P::pc) {
        (void)routine, (void)size;
        return 0;
    } else {
        const uint32_t p = mem_alloc(size);
        const uint32_t cur = mem.u32(G::task_current);
        mem.set32(p + 0x10, cur);
        mem.set32(p + 0x14, mem.u32(cur + 0x14));
        mem.set32(mem.u32(cur + 0x14) + 0x10, p);
        mem.set32(cur + 0x14, p);
        mem.set32(p, routine);
        mem.set32(p + 0x18, cur);
        mem.set32(p + 4, heap::kTaskNop);
        mem.set32(p + 0x1C, mem.u32(cur + 8));
        const uint32_t serial = mem.u32(heap::kSerial);
        mem.set32(heap::kSerial, serial + 1);
        mem.set32(p + 0xC, heap::kTaskMagic);
        mem.set32(p + 8, serial);
        return p;
    }
}

// 0x15080 task_clear_work: clears the work area +0x20.. (the block's size from its header, minus the task header)
template <class P> void Game<P>::task_clear_work(uint32_t task)
{
    if constexpr (!P::pc) {
        const int32_t n = int32_t(mem.u32(task - 12)) - 12;
        for (int32_t i = 0; i < n; ++i)
            mem.set32(task + 0x20 + uint32_t(i) * 4, 0);
    } else {
        (void)task;
    }
}

} // namespace hotd::port
