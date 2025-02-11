#define K2_DEBUG_WARN

#include <stddef.h>
#include <stdint.h>

#include "plat.h"
#include "utils.h"
#include "sched.h"

extern void test_ktimer(); 
extern void test_fb(); 
extern void test_kern_tasks_print(); 
extern void test_kern_tasks_donut(); 
extern void test_kern_task_mgmt(); 
extern void test_kern_reader_writer(); 
extern void donut(int x, int y); 	

struct cpu cpus[NCPU]; 

void kernel_main() {
	uart_init();
	init_printf(NULL, putc);


	printf("------ kernel boot ------  core %d\n\r", cpuid());
	printf("build time (kernel.c) %s %s\n", __DATE__, __TIME__); // simplicity 
			
	paging_init(); 
	sched_init(); 	// must be before schedule() or timertick() 
	fb_init(); 		// reserve fb memory other page allocations
	sys_timer_init(); 		// kernel timer: delay, timekeeping...
	enable_interrupt_controller(0);
	
	
	enable_irq(); generic_timer_init();


	
	// OS = overly sensitive
	schedule(); 
	
    while (1) {
		
        V("idle task");
        asm volatile("wfi");


    }
}


void init(int arg) {
	
	int wpid; 
    W("entering init");

	
	
	test_kern_reader_writer(); 

	while (1) {
		wpid = wait(0 ); 
		if (wpid < 0) {
			W("init: wait failed with %d", wpid);
			panic("init: maybe no child. has nothing to do. bye"); 
		} else {
			W("wait returns pid=%d", wpid);
			
		}
	}
}