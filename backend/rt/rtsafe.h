#pragma once
#include <xmmintrin.h>

// Mark audio thread functions RT_SAFE. In the build-rt build, Clang stops the program if they do anything unsafe.
#if defined(__has_feature)
#  if __has_feature(realtime_sanitizer)
#    define RT_SAFE [[clang::nonblocking]]
#  endif
#endif
#ifndef RT_SAFE
#  define RT_SAFE
#endif

// Treats tiny numbers as zero, or fading tails get very slow. First line of the audio callback.
class DenormalGuard {
public:
    DenormalGuard() noexcept : m_saved(_mm_getcsr()) { _mm_setcsr(m_saved | 0x8040); }
    ~DenormalGuard() { _mm_setcsr(m_saved); }
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
private:
    unsigned int m_saved;
};
