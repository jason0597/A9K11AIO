#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "udsploit.h"
#include "hook_kernel.h"
#include "safehax.h"
#include "asm.h"

extern void gfxSetFramebufferInfo(gfxScreen_t screen, u8 id);

bool is_new_3ds;

int main() {
	//===============================================
	//=====================SETUP=====================
	//===============================================

	Result hax_res;
	bool panic_flag = true; //by default it is true, but if everything goes well it it set to false
	char* errormsg; 
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

	if (!R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_NAND, 1, &titleID, &entry))) { hax_res = 1; goto exit; }
	if (entry.version > 27476) { hax_res = 2; goto exit; }

	//===============================================
	//===================UDSPLOIT====================
	//===============================================

	consoleSelect(&topScreen);
	if (udsploit()) { hax_res = 3; goto exit; }
	printf("udsploit success\n");
	if (hook_kernel()) { hax_res = 4; goto exit; }

	//===============================================
	//===================SAFEHAX=====================
	//===============================================

	consoleSelect(&bottomScreen);
	hax_res = safehax();

	//===============================================
	//=====================EXIT======================
	//===============================================

exit:
	switch (hax_res) {
		case 1:
			errormsg = "FAILED TO GET NATIVE_FIRM TITLE";
			break;
		case 2:
			errormsg = "UNSUPPORTED FIRMWARE!";
			break;
		case 3:
			errormsg = "UDSPLOIT FAILED!";
			break;
		case 4:
			errormsg = "KERNEL HOOK FAILED!";
			break;
		case -1:
			errormsg = "PM INIT FAILED!";
			break;
		case -2:
			errormsg = "FAILED TO ALLOCATE MEMORY!";
			break;
		case -3:
			errormsg = "FAILED TO OPEN ARM9.BIN!";
			break;
		case -4:
			errormsg = "ARM9 PAYLOAD TOO BIG!";
			break;
		case -5:
			errormsg = "ARM11 PAYLOAD TOO BIG!";
			break;
		case -6:
			errormsg = "FAILED TO PATCH THE ARM11 KERNEL!";
			break;
		case -7:
			errormsg = "FAILED TO RELOAD!";
			break;
		case 0:
		default:
			panic_flag = false;		
			break;	
	}
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