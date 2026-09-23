#pragma once

#if !defined(PAIR_IGNORE_LOCAL_SETTINGS) && __has_include("PairSettings.local.h")
#include "PairSettings.local.h"
#endif

#ifndef PAIR_NODE_A
#define PAIR_NODE_A 0u
#endif
#ifndef PAIR_NODE_B
#define PAIR_NODE_B 0u
#endif
#ifndef PAIR_ENABLE_THERMAL_EXPERIMENTAL
#define PAIR_ENABLE_THERMAL_EXPERIMENTAL 0
#endif

static_assert((PAIR_NODE_A == 0 && PAIR_NODE_B == 0) || (PAIR_NODE_A != 0 && PAIR_NODE_B != 0 && PAIR_NODE_A != PAIR_NODE_B),
              "Configure both distinct node IDs, or leave both zero to disable "
              "the fixed-pair queue");
