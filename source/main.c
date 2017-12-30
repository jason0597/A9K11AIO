#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "udsploit.h"
#include "hook_kernel.h"
#include "safehax.h"
#include "asm.h"

#include "../payload/arm11bin.h"
                                                    				
#define PANICIFTRUE(x,y) 				if (x) { panic_flag = true; errormsg = y; goto exit; }

extern void gfxSetFramebufferInfo(gfxScreen_t screen, u8 id);

int arm9_payload_size = 0;
bool is_new_3ds;
void* payload_buf = NULL;

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

	/* https://github.com/saibotu/safehax/commit/1255e2b416551b96aff8ddac48ba8725773ff905 */
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