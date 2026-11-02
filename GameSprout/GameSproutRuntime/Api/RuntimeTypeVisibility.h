#pragma once

// Public SDK interface RTTI must compare across independently loaded modules.
// In particular, Apple libc++ encodes non-unique hidden type_info names; making
// the interface type visible preserves value equality without comparing addresses.
#if defined(__clang__)
#define GAMESPROUT_RUNTIME_PUBLIC_TYPE __attribute__((type_visibility("default")))
#elif defined(__GNUC__)
#define GAMESPROUT_RUNTIME_PUBLIC_TYPE __attribute__((visibility("default")))
#else
#define GAMESPROUT_RUNTIME_PUBLIC_TYPE
#endif
