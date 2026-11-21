#pragma once

namespace ElevationRule {

constexpr bool refuseTyping(bool selfElevated, bool targetQueried, bool targetElevated) noexcept
{
    if (selfElevated)
        return false;
    return !targetQueried || targetElevated;
}

} // namespace ElevationRule