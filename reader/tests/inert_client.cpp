// SPDX-License-Identifier: AGPL-3.0-only
// TEST BINARY ONLY: same recovery provider, no graphics/input/vendor linkage.
#include "session.h"
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <string>
static volatile sig_atomic_t end=0;static void done(int){end=1;}
int main(int argc,char**argv){if(argc!=2)return 78;if(std::string(argv[1])=="--arm")return spike::armOwnedSession();auto lease=spike::receiveQualifiedRecoveryLease();if(!lease)return 78;std::puts("INERT_PAYLOAD_ENTERED actual_recovery_provider=1");std::fflush(stdout);std::string kind=argv[1];
 if(kind=="crash")raise(SIGKILL);
 if(kind=="descendant"){if(fork()==0){signal(SIGTERM,SIG_IGN);for(;;)pause();}usleep(100000);return 0;}
 if(kind=="normal"||kind=="restore-failure"||kind=="pruning"||kind=="forged-permit"){usleep(500000);return 0;}
 if(kind=="permit-reuse"){auto second=spike::receiveQualifiedRecoveryLease();if(second)return 1;std::puts("PERMIT_REUSE_REJECTED");return 0;}
 if(kind=="term-responsive"||kind=="duplicate"){signal(SIGTERM,done);while(!end)usleep(10000);return 0;}
 signal(SIGTERM,SIG_IGN);for(;;)pause();
}
