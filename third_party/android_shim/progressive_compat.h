// third_party/android_shim/progressive_compat.h
//
// Force-included via -include to fix missing STL includes in progressive_native
// modules. The Android NDK's <string>, <vector>, etc. transitively include
// <algorithm>, <cctype>, <numeric>, etc. Strict gcc/clang (e.g. gcc 16 on
// DanctNIX Linux) does NOT — so modules using std::remove/std::find/std::sort
// without an explicit #include <algorithm> fail to compile.
//
// This is a build-time compatibility shim, NOT part of the API. It does not
// change any behavior — only makes the implicit transitive includes explicit
// that the Android NDK was providing for free.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
