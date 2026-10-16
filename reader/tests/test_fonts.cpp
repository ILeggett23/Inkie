// Test-only: unit tests that paint Inkbound views load the repository fonts,
// as the packaged reader loads its bundled ones at Shell construction.
#include "ib/text.h"
namespace {const bool loaded=ib::text::load(INKBOUND_TEST_FONTS);}
