// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#pragma once

// The rack's rule for controls that step through a list (no JUCE, so the rack tests check it directly):
//   click = next, Shift-click = previous (both wrap), right-click = the whole list as a menu, the current item ticked.
// Menu item id = list index + 1; 0 = dismissed.

#include <cstddef>

namespace jidai_ui {

inline int stepListIndex (int current, int count, bool backwards) noexcept
{
    if (count <= 0) return 0;
    current = current < 0 ? 0 : (current >= count ? count - 1 : current);
    return backwards ? (current + count - 1) % count : (current + 1) % count;
}

// The header's UI SCALE steps, in percent.
inline constexpr int kScaleSteps[] = { 75, 100, 125, 150, 200 };
inline constexpr int kScaleStepCount = (int) (sizeof (kScaleSteps) / sizeof (kScaleSteps[0]));

// Click: the next larger step (wraps to the smallest); Shift-click: the next smaller step (wraps to the largest).
inline int nextScaleStep (int percent, bool backwards) noexcept
{
    if (backwards)
    {
        for (int i = kScaleStepCount - 1; i >= 0; --i)
            if (kScaleSteps[i] < percent) return kScaleSteps[i];
        return kScaleSteps[kScaleStepCount - 1];
    }
    for (int i = 0; i < kScaleStepCount; ++i)
        if (kScaleSteps[i] > percent) return kScaleSteps[i];
    return kScaleSteps[0];
}

// The scale a menu result picks, or 0 when the menu was dismissed or the id is out of range.
inline int scaleFromMenuResult (int menuId) noexcept { return menuId >= 1 && menuId <= kScaleStepCount ? kScaleSteps[menuId - 1] : 0; }

} // namespace jidai_ui
