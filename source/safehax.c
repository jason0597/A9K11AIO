#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "asm.h"

#include "../payload/arm11bin.h"

//i know ifdefs are bad practice but this is what i could come up with for the bundled arm9 payload
//if arm9bin.h exists (the ARM9BIN_EXISTS macro is defined by the makefile if it exists), include it, and set the bundled_arm9_payload_exists equal to true
//else, declare the arm9_payload_size (because if arm9bin.h was included it would declare this for us, but if arm9bin.h doesn't exist we have to declare it ourselves)
//and set the bundled_arm9_payload_exists flag equal to false

#define ARM9BIN_EXISTS

#ifdef ARM9BIN_EXISTS
#include "../arm9/arm9bin.h"
bool bundled_arm9_payload_exists = true;
#else
u8 *arm9_payload; //this is just a dummy variable so that compilation can go smooth in line 167, it won't get used because it will never go through the if statement at line 161
int arm9_payload_size;  
bool bundled_arm9_payload_exists = false;
#endif

#define FCRAM(x)   					 	(void *)(0xE0000000 + (x)) 
#define AXIWRAM(x) 					 	(void *)(0xDFF00000 + (x)) 
#define KMEMORY    					 	((u32 *)(AXIWRAM(0xF4000)))  
#define CURRENT_KTHREAD              	(*((u8**)0xFFFF9000))
#define CURRENT_KPROCESS             	(*((u8**)0xFFFF9004))
#define KPROCESS_ACL_START           	(is_new_3ds ? 0x90 : 0x88)
#define KPROCESS_PID_OFFSET          	(is_new_3ds ? 0xBC : 0xB4)
#define SVC_ACL_SIZE                 	(0x10)
#define KTHREAD_THREADPAGEPTR_OFFSET 	(0x8C)
#define KSVCTHREADAREA_BEGIN_OFFSET  	(0xC8) 

extern bool is_new_3ds;      //see main.c
void* payload_buf;
u32 g_original_pid = 0; 

static void K_PatchPID(void) {
    u8 *proc = CURRENT_KPROCESS;
    u32 *pidPtr = (u32*)(proc + KPROCESS_PID_OFFSET);

    g_original_pid = *pidPtr;

    // We're now PID zero, all we have to do is reinitialize the service manager in user-mode.
    *pidPtr = 0;
}

static void K_RestorePID(void) {
    u8 *proc = CURRENT_KPROCESS;
    u32 *pidPtr = (u32*)(proc + KPROCESS_PID_OFFSET);

    // Restore the original PID
    *pidPtr = g_original_pid;
}

static void K_PatchACL(void) {
    // Patch the process first (for newly created threads).
    u8 *proc = CURRENT_KPROCESS;
    u8 *procacl = proc + KPROCESS_ACL_START;
    memset(procacl, 0xFF, SVC_ACL_SIZE);

    // Now patch the current thread.
    u8 *thread = CURRENT_KTHREAD;
    u8 *thread_pageend = *(u8**)(thread + KTHREAD_THREADPAGEPTR_OFFSET);
    u8 *thread_page = thread_pageend - KSVCTHREADAREA_BEGIN_OFFSET;
    memset(thread_page, 0xFF, SVC_ACL_SIZE);
}

static void initsrv_allservices(void) {
    printf("Patching PID\n");
    svcMiniBackdoor(K_PatchPID);

    printf("Reiniting srv\n");
    srvExit();
    srvInit();

    printf("Restoring PID\n");
    svcMiniBackdoor(K_RestorePID);
}

static void patch_svcaccesstable(void) {
    printf("Patching SVC access table\n");
    svcMiniBackdoor(K_PatchACL);
}

static Result patch_arm11_codeflow(void) {
	disable_interrupts();
	
	memcpy(FCRAM(0x3F00000), payload_buf, arm9_payload_size);
	memcpy(FCRAM(0x3FFF000), payload_buf + 0xFF000, 0xE20);
	
	for (int i = 0; i < 0x2000/4; i++) {
		if (KMEMORY[i] == 0xE12FFF14 && KMEMORY[i+2] == 0xE3A01000) { //hook arm11 launch
			KMEMORY[i+3] = 0xE51FF004; //LDR PC, [PC,#-4]
			KMEMORY[i+4] = 0x23FFF000;
			flush_dcache();
            invalidate_icache();
			return 0;
			break;
		}
	}

	return -1;
}

Result safehax(void) {
	initsrv_allservices();
	patch_svcaccesstable();

	if (pmInit()) { return -1; }
	
	printf("Allocating memory...\n");
	payload_buf = memalign(0x1000, 0x100000);
	if (!payload_buf) { return -2; }

	//==========================================
	//ARM9 payload reading
	//
	//first try to read safehaxpayload.bin
	//	if opening fails, and there is no bundled arm9 payload, return
	//	else, goto arm9_payload_fallback
	//then, check the file size of safehaxpayload.bin is bigger than 0xFF000
	//	if it's bigger than 0xFF000, and there is no bundled arm9 payload, return
	//	else, goto arm9_payload_fallback
	//==========================================

	bool safehaxpayload_reading_success = false;
	printf("Reading ARM9 payload...\n");
	printf("Opening safehaxpayload.bin...\n");
	FILE* FileIn = fopen("sdmc:/safehaxpayload.bin", "rb");
	if (!FileIn) { 
		if (bundled_arm9_payload_exists) { 
			printf("Failed to open safehaxpayload.bin!\nFalling back to bundled payload...\n");
			goto arm9_payload_fallback; 
		}
		else { 
			printf("Failed to open safehaxpayload.bin!\nNo bundled payload to fallback on!\nExiting...\n");
			return -3; 
		}
	}
	fseek(FileIn, 0, SEEK_END);
	if (ftell(FileIn) > 0xFF000) {
		fclose(FileIn);
		if (bundled_arm9_payload_exists) { 
			printf("safehaxpayload.bin too big!\nFalling back to bundled payload...\n");
			goto arm9_payload_fallback; 
		}
		else { 
			printf("safehaxpayload.bin too big!\nNo bundled payload to fallback on!\nExiting...\n");
			return -4; 
		}
	}
	arm9_payload_size = ftell(FileIn); 
	rewind(FileIn);
	fread(payload_buf, 1, arm9_payload_size, FileIn);
	fclose(FileIn);
	safehaxpayload_reading_success = true;

arm9_payload_fallback:

	if (!safehaxpayload_reading_success && bundled_arm9_payload_exists) {
		if (arm9_payload_size > 0xFF000) {
			printf("Bundled payload too big!\nExiting...");
			return -8;
		}
		for (int i = 0; i < arm9_payload_size; i++ /*one by one because we are dealing with bytes and we have a u8 array*/) {
			*((u32*)(payload_buf + i)) = arm9_payload[i];
		}
	}

	//==========================================
	//==========================================
	//==========================================

	printf("Injecting ARM11 payload...\n");
	if (arm11_payload_size > 0xE00) { return -5; }
	for (int i = 0; i < arm11_payload_size; i++ /*one by one because we are dealing with bytes and we have a u8 array*/) {
		*((u32*)(payload_buf + 0xFF000 + i)) = arm11_payload[i];
	}

	/* Setup Framebuffers - https://github.com/mid-kid/CakeBrah/blob/master/source/brahma.c#L364 */
	printf("Setting framebuffers...\n");
	*((u32 *)(payload_buf + 0xFFE00)) = (u32)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL) + 0xC000000;
	*((u32 *)(payload_buf + 0xFFE04)) = (u32)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) + 0xC000000;
	*((u32 *)(payload_buf + 0xFFE08)) = (u32)gfxGetFramebuffer(GFX_BOTTOM, 0, NULL, NULL) + 0xC000000;
	gfxSwapBuffers();
		
	printf("Patching ARM11...\n");
	Result backdoor_res = svcMiniBackdoor(patch_arm11_codeflow);
	if (backdoor_res) { return -6; }
	
	/* Relaunch Firmware - This will clear the global flag preventing SAFE_MODE launch. */
	printf("Reloading firmware...\n");
    if (PM_LaunchFIRMSetParams(2, 0, NULL)) { return -7; }

    return 0;
}