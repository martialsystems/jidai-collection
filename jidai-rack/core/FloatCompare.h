// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// Intentional exact float comparison (change detection: has a parameter moved since it was last applied?).
// Same result as `a == b`, including NaN != NaN; it only names the intent so -Wfloat-equal stays on everywhere else.
// Both sides must be the same type, so no hidden promotion. JUCE-free, constexpr.

namespace jidai {

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

template <typename T>
constexpr bool exactlyEqual (T a, T b) noexcept
{
    return a == b;
}

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#endif

}
