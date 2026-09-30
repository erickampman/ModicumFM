//
//  FM6OpAlgorithms.hpp
//
//  The 32 DX7-style operator routings for FM6Op, plus a role analysis of each (which operators
//  are carriers, and how many modulation hops each modulator sits from the output). Header-only
//  and dependency-free so the engine, the UI and the tests all share one table.
//
//  Operators are indexed 0..5 (= Op1..Op6). Every modulator has a HIGHER index than the operator
//  it modulates, so one pass from Op6 down to Op1 evaluates a whole algorithm per sample.
//
//  Role analysis is the foundation for the role-based macros (see docs/ROADMAP.md): a macro acts
//  on "all modulators", "the deepest modulators", "the carriers" etc., so it works on any
//  algorithm without knowing operator numbers.
//

#pragma once
#include <cstdint>

constexpr int kFM6OpCount      = 6;
constexpr int kFM6OpAlgorithms = 32;

struct FM6OpAlgo {
    uint8_t modMask[kFM6OpCount]; // modMask[c] = ops that modulate op c (bit i = op index i)
    uint8_t carrierMask;          // ops summed to the output
    uint8_t fbDst;                // op whose phase receives the feedback
    uint8_t fbSrc;                // op whose output is fed back (== fbDst for self-feedback;
                                  // DX7 algorithms 4 and 6 feed a lower op back into Op6)
};

// Build a mask from 1-based operator numbers: M(2,3) = ops 2 and 3.
constexpr uint8_t fm6Bit(int opNumber) { return static_cast<uint8_t>(1u << (opNumber - 1)); }
template <typename... T> constexpr uint8_t fm6Mask(T... ops) { return static_cast<uint8_t>((0 | ... | fm6Bit(ops))); }

// Entries list, for Op1..Op6 in order, which ops modulate it; then carriers; then feedback
// destination / source (1-based).
#define FM6_ALGO(m1, m2, m3, m4, m5, m6, car, fbd, fbs) \
    { { m1, m2, m3, m4, m5, m6 }, car, (uint8_t)((fbd) - 1), (uint8_t)((fbs) - 1) }
#define M fm6Mask
inline constexpr FM6OpAlgo kFM6OpAlgos[kFM6OpAlgorithms] = {
    //         Op1      Op2   Op3        Op4     Op5   Op6   carriers        fb dst/src
    FM6_ALGO(M(2),    0,    M(4),      M(5),   M(6), 0,    M(1,3),         6, 6), //  1
    FM6_ALGO(M(2),    0,    M(4),      M(5),   M(6), 0,    M(1,3),         2, 2), //  2
    FM6_ALGO(M(2),    M(3), 0,         M(5),   M(6), 0,    M(1,4),         6, 6), //  3
    FM6_ALGO(M(2),    M(3), 0,         M(5),   M(6), 0,    M(1,4),         6, 4), //  4
    FM6_ALGO(M(2),    0,    M(4),      0,      M(6), 0,    M(1,3,5),       6, 6), //  5
    FM6_ALGO(M(2),    0,    M(4),      0,      M(6), 0,    M(1,3,5),       6, 5), //  6
    FM6_ALGO(M(2),    0,    M(4,5),    0,      M(6), 0,    M(1,3),         6, 6), //  7
    FM6_ALGO(M(2),    0,    M(4,5),    0,      M(6), 0,    M(1,3),         4, 4), //  8
    FM6_ALGO(M(2),    0,    M(4,5),    0,      M(6), 0,    M(1,3),         2, 2), //  9
    FM6_ALGO(M(2),    M(3), 0,         M(5,6), 0,    0,    M(1,4),         3, 3), // 10
    FM6_ALGO(M(2),    M(3), 0,         M(5,6), 0,    0,    M(1,4),         6, 6), // 11
    FM6_ALGO(M(2),    0,    M(4,5,6),  0,      0,    0,    M(1,3),         2, 2), // 12
    FM6_ALGO(M(2),    0,    M(4,5,6),  0,      0,    0,    M(1,3),         6, 6), // 13
    FM6_ALGO(M(2),    0,    M(4),      M(5,6), 0,    0,    M(1,3),         6, 6), // 14
    FM6_ALGO(M(2),    0,    M(4),      M(5,6), 0,    0,    M(1,3),         2, 2), // 15
    FM6_ALGO(M(2,3,5),0,    M(4),      0,      M(6), 0,    M(1),           6, 6), // 16
    FM6_ALGO(M(2,3,5),0,    M(4),      0,      M(6), 0,    M(1),           2, 2), // 17
    FM6_ALGO(M(2,3,4),0,    0,         M(5),   M(6), 0,    M(1),           3, 3), // 18
    FM6_ALGO(M(2),    M(3), 0,         M(6),   M(6), 0,    M(1,4,5),       6, 6), // 19
    FM6_ALGO(M(3),    M(3), 0,         M(5,6), 0,    0,    M(1,2,4),       3, 3), // 20
    FM6_ALGO(M(3),    M(3), 0,         M(6),   M(6), 0,    M(1,2,4,5),     3, 3), // 21
    FM6_ALGO(M(2),    0,    M(6),      M(6),   M(6), 0,    M(1,3,4,5),     6, 6), // 22
    FM6_ALGO(0,       M(3), 0,         M(6),   M(6), 0,    M(1,2,4,5),     6, 6), // 23
    FM6_ALGO(0,       0,    M(6),      M(6),   M(6), 0,    M(1,2,3,4,5),   6, 6), // 24
    FM6_ALGO(0,       0,    0,         M(6),   M(6), 0,    M(1,2,3,4,5),   6, 6), // 25
    FM6_ALGO(0,       M(3), 0,         M(5,6), 0,    0,    M(1,2,4),       6, 6), // 26
    FM6_ALGO(0,       M(3), 0,         M(5,6), 0,    0,    M(1,2,4),       3, 3), // 27
    FM6_ALGO(M(2),    0,    M(4),      M(5),   0,    0,    M(1,3,6),       5, 5), // 28
    FM6_ALGO(0,       0,    M(4),      0,      M(6), 0,    M(1,2,3,5),     6, 6), // 29
    FM6_ALGO(0,       0,    M(4),      M(5),   0,    0,    M(1,2,3,6),     5, 5), // 30
    FM6_ALGO(0,       0,    0,         0,      M(6), 0,    M(1,2,3,4,5),   6, 6), // 31
    FM6_ALGO(0,       0,    0,         0,      0,    0,    M(1,2,3,4,5,6), 6, 6), // 32
};
#undef M
#undef FM6_ALGO

// --- Role analysis ------------------------------------------------------------------------

// Depth = modulation hops to the output along the SHORTEST path: carriers are 0, an operator
// modulating a carrier is 1, one modulating that is 2, ... An operator that reaches no carrier
// (can't happen in the table above) gets kFM6OpUnreachable.
constexpr uint8_t kFM6OpUnreachable = 0xff;

struct FM6OpRoles {
    uint8_t depth[kFM6OpCount];
    uint8_t carrierCount;
    uint8_t maxDepth;            // deepest modulator (0 for all-carrier algorithms)

    constexpr bool isCarrier(int op) const { return depth[op] == 0; }
};

constexpr FM6OpRoles fm6OpRoles(const FM6OpAlgo& a) {
    FM6OpRoles r{};
    for (int op = 0; op < kFM6OpCount; ++op)
        r.depth[op] = (a.carrierMask & (1u << op)) ? 0 : kFM6OpUnreachable;
    // Relax: a source's depth is 1 + the shallowest op it modulates. Modulators always have a
    // higher index than their targets, so one ascending pass settles every depth.
    for (int dst = 0; dst < kFM6OpCount; ++dst) {
        for (int src = 0; src < kFM6OpCount; ++src) {
            if ((a.modMask[dst] & (1u << src)) && r.depth[dst] != kFM6OpUnreachable
                && r.depth[dst] + 1 < r.depth[src])
                r.depth[src] = static_cast<uint8_t>(r.depth[dst] + 1);
        }
    }
    for (int op = 0; op < kFM6OpCount; ++op) {
        if (r.depth[op] == 0) ++r.carrierCount;
        else if (r.depth[op] != kFM6OpUnreachable && r.depth[op] > r.maxDepth) r.maxDepth = r.depth[op];
    }
    return r;
}

inline constexpr FM6OpRoles fm6OpRoles(int algorithm) {
    return fm6OpRoles(kFM6OpAlgos[algorithm < 0 ? 0 : (algorithm >= kFM6OpAlgorithms ? kFM6OpAlgorithms - 1 : algorithm)]);
}
