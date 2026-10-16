#include "session_limits.h"
#include <cstdio>
using namespace recovery::limits;
static_assert(Payload==300*Second && Child==302*Second && Owner==320*Second && ProofCap==315*Second);
static_assert(validDeadline(Second,301*Second));
static_assert(!validDeadline(Second,301*Second+1));
static_assert(!validDeadline(Second,Second));
static_assert(!validDeadline(0,Second));
int main(){std::puts("WINDOW_LIMITS_PASS production=300s immutable_deadline expired_and_overlong_rejected");}
