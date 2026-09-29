#pragma once

// Force-included into every MSVC host-test translation unit (see test/CMakeLists.txt).
// GCC attributes in firmware headers, such as always_inline in GlyphBitmap.h, are
// optimisation hints only; MSVC does not know the syntax.
#define __attribute__(x)
