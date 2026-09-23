#pragma once

// Whether refract bends around a compiler or library that gets something wrong. Every such place tests
// `REFRACT_CLANG_WORKAROUNDS` or names it in a comment, keeps the straightforward code in the other branch, and has an
// entry in `z80/v4/notes/WASM.md` saying what it avoids and how to tell when it can go. Define it to 0 to build the
// straightforward code with clang and see whether a newer build still needs the workaround.
#ifndef REFRACT_CLANG_WORKAROUNDS
#if defined(__clang__)
#define REFRACT_CLANG_WORKAROUNDS 1
#else
#define REFRACT_CLANG_WORKAROUNDS 0
#endif
#endif
