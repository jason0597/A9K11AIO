#include <3ds/types.h>

void disable_interrupts(void);
void flush_dcache(void);
void invalidate_icache(void);

Result svcMiniBackdoor(void* target);
