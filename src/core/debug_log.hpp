// src/core/debug_log.hpp — Qt-free debug logging, asserts, and function tracing.
#pragma once
#include <cstdio>
#include <cstdlib>

//
// Compile-time toggles (set via CMake -D flags, optional):
//   PROGRESSIVE_DISABLE_ASSERT  — removes all PROGRESSIVE_ASSERT checks
//   PROGRESSIVE_DISABLE_LOG     — removes all LOG() output
// By default (no flags): everything is ON for debug builds.
//

#ifndef PROGRESSIVE_DISABLE_ASSERT
#define PROGRESSIVE_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[ASSERT] %s:%d: FAILED %s — %s\n", \
                         __FILE__, __LINE__, #cond, msg); \
            std::abort(); \
        } \
    } while(0)
#else
#define PROGRESSIVE_ASSERT(cond, msg) ((void)0)
#endif

enum class LogChannel {
    GUI,
    SYNC,
    E2EE,
    NET,
    MEM,
    DBG
};

#ifndef PROGRESSIVE_DISABLE_LOG
#define LOG(ch, fmt, ...) \
    do { \
        switch (ch) { \
            case LogChannel::GUI:  std::fprintf(stderr, "[GUI]  " fmt "\n", ##__VA_ARGS__); break; \
            case LogChannel::SYNC: std::fprintf(stderr, "[SYNC] " fmt "\n", ##__VA_ARGS__); break; \
            case LogChannel::E2EE: std::fprintf(stderr, "[E2EE] " fmt "\n", ##__VA_ARGS__); break; \
            case LogChannel::NET:  std::fprintf(stderr, "[NET]  " fmt "\n", ##__VA_ARGS__); break; \
            case LogChannel::MEM:  std::fprintf(stderr, "[MEM]  " fmt "\n", ##__VA_ARGS__); break; \
            case LogChannel::DBG:  std::fprintf(stderr, "[DBG]  " fmt "\n", ##__VA_ARGS__); break; \
        } \
    } while(0)
#else
#define LOG(ch, fmt, ...) ((void)0)
#endif

class TraceFn {
    const char* name_;
public:
    explicit TraceFn(const char* name) : name_(name) {
        std::fprintf(stderr, "[TRACE] -> %s\n", name_);
    }
    ~TraceFn() {
        std::fprintf(stderr, "[TRACE] <- %s\n", name_);
    }
};
