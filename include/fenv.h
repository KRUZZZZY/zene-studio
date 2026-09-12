#pragma once

#include_next <fenv.h>

#if defined(__APPLE__) && defined(__MACH__)

// Public domain polyfill for feenableexcept on OS X
// http://www-personal.umich.edu/~williams/archive/computation/fe-handling-example.c
//
// Darwin's <fenv.h> does not declare feenableexcept/fedisableexcept, so this
// header shadows it (it is on the include path, hence #include_next above) and
// supplies them.  The two registers the polyfill pokes are ARCHITECTURE
// SPECIFIC, and Darwin names them per architecture (Apple's SDK fenv.h):
//
//   __i386__ / __x86_64__ : struct { unsigned short __control;   // x87 control
//                                    unsigned short __status;    // word/status
//                                    unsigned int   __mxcsr;     // SSE
//                                    char           __reserved[8]; }
//                           FE_INEXACT 0x20 .. FE_INVALID 0x01 -- the x87
//                           control-word bits, masked = 1, and the MXCSR
//                           equivalents sit seven places higher, hence the
//                           `new_excepts << 7` below.
//
//   __arm64__             : struct { unsigned long long __fpsr;  // status
//                                    unsigned long long __fpcr; } // control
//                           A single control register, no x87/SSE pair.  Its
//                           trap-enable bits (__fpcr_trap_* = 0x00000100 ..
//                           0x00008000) sit exactly eight places above the
//                           FE_* flag bits (0x0001 .. 0x0080), and a set bit
//                           means the exception TRAPS -- the opposite polarity
//                           of the x87 control word, whose set bit means
//                           MASKED.  feenableexcept therefore sets those bits
//                           where it clears the x87 ones, and fedisableexcept
//                           clears them where it sets the x87 ones.  The value
//                           both return stays the same thing the x86 branch
//                           returns -- the previous MASKED set -- so the two
//                           branches keep one contract (the only caller,
//                           src/core/main.cpp's LMMS_DEBUG_FPE block, ignores
//                           it either way).
//
// Accessing __control/__mxcsr on arm64 was a hard compile error on the macOS
// arm64 CI job (and, for the x86 spelling, on any Apple SDK whose fenv_t is
// selected per architecture), which is why the branches below are separate.
// The masks are taken from that SDK header, not invented.

#if defined(__i386__) || defined(__x86_64__)

inline int feenableexcept(unsigned int excepts)
{
    static fenv_t fenv;
    unsigned int new_excepts = excepts & FE_ALL_EXCEPT;
    // previous masks
    unsigned int old_excepts;

    if (fegetenv(&fenv)) {
        return -1;
    }
    old_excepts = fenv.__control & FE_ALL_EXCEPT;

    // unmask
    fenv.__control &= ~new_excepts;
    fenv.__mxcsr   &= ~(new_excepts << 7);

    return fesetenv(&fenv) ? -1 : old_excepts;
}

inline int fedisableexcept(unsigned int excepts)
{
    static fenv_t fenv;
    unsigned int new_excepts = excepts & FE_ALL_EXCEPT;
    // all previous masks
    unsigned int old_excepts;

    if (fegetenv(&fenv)) {
        return -1;
    }
    old_excepts = fenv.__control & FE_ALL_EXCEPT;

    // mask
    fenv.__control |= new_excepts;
    fenv.__mxcsr   |= new_excepts << 7;

    return fesetenv(&fenv) ? -1 : old_excepts;
}

#elif defined(__arm64__) || defined(__aarch64__)

// The arm64 FPCR trap-enable bits are the FE_* flag bits shifted left by 8;
// FE_ALL_EXCEPT is the mask of exactly those six flags (0x009f), so
// FE_ALL_EXCEPT << 8 is exactly the mask of the controllable trap bits
// (0x9f00).  __fpcr_flush_to_zero (0x01000000) is a mode, not a trap enable,
// and is deliberately outside that mask: it must survive untouched.

inline int feenableexcept(unsigned int excepts)
{
    static fenv_t fenv;
    unsigned int new_excepts = excepts & FE_ALL_EXCEPT;
    // previously masked (non-trapping) exceptions
    unsigned int old_excepts;

    if (fegetenv(&fenv)) {
        return -1;
    }
    old_excepts = (unsigned int)(~(fenv.__fpcr >> 8)) & (unsigned int)FE_ALL_EXCEPT;

    // enable trapping: the FPCR bit is an enable, so set it
    fenv.__fpcr |= (unsigned long long)new_excepts << 8;

    return fesetenv(&fenv) ? -1 : (int)old_excepts;
}

inline int fedisableexcept(unsigned int excepts)
{
    static fenv_t fenv;
    unsigned int new_excepts = excepts & FE_ALL_EXCEPT;
    // previously masked (non-trapping) exceptions
    unsigned int old_excepts;

    if (fegetenv(&fenv)) {
        return -1;
    }
    old_excepts = (unsigned int)(~(fenv.__fpcr >> 8)) & (unsigned int)FE_ALL_EXCEPT;

    // mask them: clear the trap-enable bits
    fenv.__fpcr &= ~((unsigned long long)new_excepts << 8);

    return fesetenv(&fenv) ? -1 : (int)old_excepts;
}

#endif // defined(__i386__) || defined(__x86_64__) / defined(__arm64__)

#endif // defined(__APPLE__) && defined(__MACH__)
