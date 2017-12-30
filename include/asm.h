#include <3ds/types.h>

Result svcMiniBackdoor(void* target);
void invalidate_icache();

u32 svc_30(void *entry_fn, ...); // can pass up to two arguments to entry_fn(...)
Result svcGlobalBackdoor(s32 (*callback)(void));
bool checkSvcGlobalBackdoor(void);