#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "../payload/arm11bin.h"

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
#define PANICIFTRUE(x,y) 				if (x) { panic_flag = true; errormsg = y; goto exit; }

u32 svc_30(void *entry_fn, ...); // can pass up to two arguments to entry_fn(...)
Result svcGlobalBackdoor(s32 (*callback)(void));
bool checkSvcGlobalBackdoor(void);
extern void gfxSetFramebufferInfo(gfxScreen_t screen, u8 id);

u32 g_original_pid = 0;
int arm9_payload_size = 0;
bool is_new_3ds;
void* payload_buf = NULL;

Result hook_kernel();
Result udsploit();

void K_PatchPID(void) {
    u8 *proc = CURRENT_KPROCESS;
    u32 *pidPtr = (u32*)(proc + KPROCESS_PID_OFFSET);

    g_original_pid = *pidPtr;

    // We're now PID zero, all we have to do is reinitialize the service manager in user-mode.
    *pidPtr = 0;
}

void K_RestorePID(void) {
    u8 *proc = CURRENT_KPROCESS;
    u32 *pidPtr = (u32*)(proc + KPROCESS_PID_OFFSET);

    // Restore the original PID
    *pidPtr = g_original_pid;
}

void K_PatchACL(void) {
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

void initsrv_allservices(void) {
    printf("Patching PID\n");
    svc_30(K_PatchPID);

    printf("Reiniting srv\n");
    srvExit();
    srvInit();

    printf("Restoring PID\n");
    svc_30(K_RestorePID);
}

void patch_svcaccesstable(void) {
    printf("Patching SVC access table\n");
    svc_30(K_PatchACL);
}

Result patch_arm11_codeflow(void) {
	__asm__ volatile ( "CPSID AIF\n" "CLREX" );
	
	memcpy(FCRAM(0x3F00000), payload_buf, arm9_payload_size);
	memcpy(FCRAM(0x3FFF000), payload_buf + 0xFF000, 0xE20);
	
	for (int i = 0; i < 0x2000/4; i++) {
		if (KMEMORY[i] == 0xE12FFF14 && KMEMORY[i+2] == 0xE3A01000) { //hook arm11 launch
			KMEMORY[i+3] = 0xE51FF004; //LDR PC, [PC,#-4]
			KMEMORY[i+4] = 0x23FFF000;
			__asm__ volatile ( //flush & invalidate the caches
				"MOV R0, #0\n"
				"MCR P15, 0, R0, C7, C10, 0\n"
				"MCR P15, 0, R0, C7, C5, 0"
			);
			return 0;
			break;
		}
	}

	return -1;
}

int main() {
	//===============================================
	//=====================SETUP=====================
	//===============================================

	bool panic_flag = false;
	char *errormsg = "";
	PrintConsole topScreen, bottomScreen;
	gfxInitDefault();
	consoleInit(GFX_TOP, &topScreen);
	consoleInit(GFX_BOTTOM, &bottomScreen);
	aptInit();
	APT_CheckNew3DS(&is_new_3ds);
	fsInit();	
	sdmcInit();
	romfsInit();
	amInit();

	u64 titleID = is_new_3ds ? 0x0004013820000002 : 0x0004013800000002;
	AM_TitleEntry entry;
	PANICIFTRUE(!R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_NAND, 1, &titleID, &entry)), "FAILED TO GET NATIVE_FIRM TITLE")
 	PANICIFTRUE(entry.version > 27476, "UNSUPPORTED FIRMWARE!") // 11.4^

	//===============================================
	//===================UDSPLOIT====================
	//===============================================

	consoleSelect(&topScreen);
	Result ret = 0;	
	ret = udsploit();
	printf("%08X\n", (unsigned int)ret);
	PANICIFTRUE(ret, "UDSPLOIT FAILED!")
	printf("udsploit success\n");
	ret = hook_kernel();
	PANICIFTRUE(ret, "KERNEL HOOK FAILED!")
	printf("%08X\n", (unsigned int)ret);

	//===============================================
	//===================SAFEHAX=====================
	//===============================================

	consoleSelect(&bottomScreen);
	
	if (checkSvcGlobalBackdoor()) {
		initsrv_allservices();
		patch_svcaccesstable();
	}
	
	PANICIFTRUE(pmInit(), "PM INIT FAILED!");
	
	printf("Allocating memory...\n");
	payload_buf = memalign(0x1000, 0x100000);
	PANICIFTRUE(!payload_buf, "FAILED TO ALLOCATE MEMORY!");
	
	printf("Reading ARM9 payload...\n");
	FILE* FileIn = fopen("sdmc:/arm9.bin", "rb");
	PANICIFTRUE(!FileIn, "FAILED TO OPEN ARM9.BIN!")
	fseek(FileIn, 0, SEEK_END);
	arm9_payload_size = ftell(FileIn);
	rewind(FileIn);
	PANICIFTRUE(arm9_payload_size > 0xFF000, "ARM9 PAYLOAD TOO BIG!")
	fread(payload_buf, 1, arm9_payload_size, FileIn);
	fclose(FileIn);

	printf("Injecting ARM11 payload...\n");
	PANICIFTRUE(arm11_payload_size > 0xE00, "ARM11 PAYLOAD TOO BIG!")
	for (int i = 0; i < arm11_payload_size; i++ /*one by one because we are dealing with bytes and we have a u8 array*/) {
		*((u32*)(payload_buf + 0xFF000 + i)) = arm11bin[i];
	}

	/* Setup Framebuffers - https://github.com/mid-kid/CakeBrah/blob/master/source/brahma.c#L364 */
	printf("Setting framebuffers...\n");
	*((u32 *)(payload_buf + 0xFFE00)) = (u32)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL) + 0xC000000;
	*((u32 *)(payload_buf + 0xFFE04)) = (u32)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) + 0xC000000;
	*((u32 *)(payload_buf + 0xFFE08)) = (u32)gfxGetFramebuffer(GFX_BOTTOM, 0, NULL, NULL) + 0xC000000;
	gfxSwapBuffers();
		
	printf("Patching ARM11...\n");
	Result backdoor_res = checkSvcGlobalBackdoor() ? svcGlobalBackdoor(patch_arm11_codeflow) : svcBackdoor(patch_arm11_codeflow);
	PANICIFTRUE(backdoor_res, "FAILED TO PATCH THE KERNEL!");
	
	/* Relaunch Firmware - This will clear the global flag preventing SAFE_MODE launch. */
	printf("Reloading firmware...\n");
	PANICIFTRUE(PM_LaunchFIRMSetParams(2, 0, NULL), "FAILED TO RELOAD!")
	
	//===============================================
	//=====================EXIT======================
	//===============================================

exit:
	if (panic_flag) {
		consoleSelect(&bottomScreen);
		printf("\x1b[26;3H\x1b[31;1m [!] %s\x1b[28;3H Press [START] to exit", errormsg);
		while (aptMainLoop()) { 
			hidScanInput(); if (hidKeysDown() & KEY_START) { break; } 
			gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
		}
	} 
	else { //fix framebuffer on exit
		gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
		gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
		gfxSetFramebufferInfo(GFX_TOP, 0);
		gfxSetFramebufferInfo(GFX_BOTTOM, 0);
		gfxFlushBuffers();
		gfxConfigScreen(GFX_TOP, true);
		gfxConfigScreen(GFX_BOTTOM, true);
		gspWaitForVBlank();
	}
	
	pmExit();
	romfsExit();
	sdmcExit();
	aptExit();
	fsExit();
	gfxExit();
	return 0;
}