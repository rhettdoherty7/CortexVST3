#pragma once

namespace cortex
{

#ifndef CORTEX_VERSION_STRING
 #define CORTEX_VERSION_STRING "0.0.0"
#endif

inline constexpr const char* versionString = CORTEX_VERSION_STRING;

/** Bumped whenever the saved plugin state changes shape. */
inline constexpr int stateVersion = 1;

} // namespace cortex
