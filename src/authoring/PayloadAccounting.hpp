#pragma once

#include <cstddef>
#include <limits>

namespace liquid::authoring::detail {

// Contract logical payload accounting (LIQUID_IMPLEMENTATION_CONTRACT.md):
// 8 bytes per numeric scalar, ID or enum, 1 per boolean or optional-presence
// flag, byte length for strings, and 8 per collection element or record field.
// The one authoring definition, shared by the session charges and the
// evaluation store and record accounting, so a reservation and the charge it
// covers can never disagree.
// shortcut: the L0 manifest keeps its own private copy in
// src/scripting/LuaCapabilityManifest.cpp, outside the authoring allowlist.
// Upgrade trigger: share this definition when that file is in an authorized
// allowlist.
inline constexpr std::size_t LogicalScalarBytes = 8;
inline constexpr std::size_t LogicalFlagBytes = 1;
inline constexpr std::size_t LogicalElementBytes = 8;

// Saturates instead of wrapping; a saturated charge always exceeds every
// finite budget and the session ceiling.
constexpr std::size_t saturating_add(std::size_t total, std::size_t bytes) {
    return bytes > std::numeric_limits<std::size_t>::max() - total
        ? std::numeric_limits<std::size_t>::max()
        : total + bytes;
}

}
