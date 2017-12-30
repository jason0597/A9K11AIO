#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "asm.h"

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
 
extern void* payload_buf;       //see main.c
extern int arm9_payload_size;   //see main.c
extern bool is_new_3ds;         //see main.c
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