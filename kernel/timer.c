#define K2_DEBUG_INFO

#include "plat.h"
#include "utils.h"
#include "printf.h"
#include "spinlock.h"
#include "sched.h"


#define SCHED_TICK_HZ	10

/* sched interval, for arm generic timer. 
when on qemu, arm generic timer is at 1MHz by default
on rpi3, it's also 1MHz. */
int interval = (100 * 1000 * 1000 / SCHED_TICK_HZ);


/**
 *  Arm generic timers. Each core has its own instance. 
 *
 *  Here, the physical timer at EL1 is used with the TimerValue views.
 *  Once the count-down reaches 0, the interrupt line is HIGH until
 *  a new timer value > 0 is written into the CNTP_TVAL_EL0 system register.
 *
 *  Read: 
 *  https://fxlin.github.io/p1-kernel/exp3/rpi-os/#arms-generic-hardware-timer
 * 
 *  Reference: AArch64-referenc-manual p.2326 at
 *  https://developer.arm.com/docs/ddi0487/ca/arm-architecture-reference-manual-armv8-for-armv8-a-architecture-profile
 */

static void generic_timer_reset(int intv) {	
	asm volatile("msr CNTP_TVAL_EL0, %0" : : "r"(intv));  // TVAL is 32bit, signed
}

void generic_timer_init (void) {
	asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(1));

	generic_timer_reset(interval);	// kickoff 1st time firing
}

void handle_generic_timer_irq(void)  {
	
	/* 	Reset the timer before calling timer_tick() (which calls 
	schedule()..), not after it. Otherwise, enable_irq() inside 
	timer_tick() will trigger a new timer irq IMMEDIATELY (looks like hw 
	checks for the generic timer's condition whenever DAIF is set? or the 
	behavior of qemu?). As a result, timer_irq handler will be called 
	back to back, corrupting the kernel stack  */

	generic_timer_reset(interval);
	
	
	timer_tick();
}


/* 
	Rpi3's "system Timer". 
	- Support "virtual timers" and timekeeping (current_time(), sys_sleep()). 
	- Efficient. No periodic interrupts. Instead, set & fire on demand. 
	- IRQ always routed to core 0.

	cf: test_ktimer() on how to use.

	NB: in earlier qemu (<5), emulation for system timer is incomplete --
	cannot fire interrupts. 
	https://fxlin.github.io/p1-kernel/exp3/rpi-os/#fyi-other-timers-on-rpi3		

*/
#if defined(PLAT_RPI3) || defined(PLAT_RPI3QEMU)
#define N_TIMERS 20 	// # of vtimers
#define CLOCKHZ	1000000	// rpi3 use 1MHz clock for system counter. 

#define TICKPERSEC (CLOCKHZ)
#define TICKPERMS (CLOCKHZ / 1000)
#define TICKPERUS (CLOCKHZ / 1000 / 1000)

static inline unsigned long current_counter() {
	return ((unsigned long) get32(TIMER_CHI) << 32) | get32(TIMER_CLO); 
}


#ifdef PLAT_RPI3


static unsigned int cycles_per_ms = 5011;
static unsigned int cycles_per_us = 5; 
#elif defined(PLAT_RPI3QEMU)

static unsigned int cycles_per_ms = 434782;
static unsigned int cycles_per_us = 434; 
#endif

__attribute__((unused))
static void sys_timer_tune_delay() {
	unsigned long cur0 = current_counter(), ms, us; 
	unsigned long ncycles = 100 * 1000 * 1000; 	// run 100M cycles. delay should >1ms
	delay(ncycles); 	
	us = (current_counter() - cur0) / TICKPERUS; 
	ms = us / 1000; 

	cycles_per_us = ncycles / us; 
	cycles_per_ms = ncycles / ms; 
	I("cycles_per_us %u cycles_per_ms %u", cycles_per_us, cycles_per_ms);
}

void ms_delay(unsigned ms) {
	BUG_ON(!cycles_per_ms);
	delay(cycles_per_ms * ms); 
}

void us_delay(unsigned us) {
	BUG_ON(!cycles_per_us);
	delay(cycles_per_us * us); 
}

void current_time(unsigned *sec, unsigned *msec) {
	unsigned long cur = current_counter();
	*sec =  (unsigned) (cur / TICKPERSEC); 
	cur -= (*sec) * TICKPERSEC; 
	*msec = (unsigned) (cur / TICKPERMS);	
}

struct spinlock timerlock;

struct vtimer {
	TKernelTimerHandler *handler; 
	unsigned long elapseat; 	// sys timer ticks (=us)
	void *param; 
	void *context; 
}; 
static struct vtimer timers[N_TIMERS]; 

__attribute__((unused))
static void sys_timer_test() { 
	unsigned int curVal = get32(TIMER_CLO);
	curVal += interval;
	put32(TIMER_C1, curVal);	
}

void sys_timer_init(void)
{
	initlock(&timerlock, "timer"); 
	memzero(timers, sizeof(timers)); 	// all field zeros	
}

static int adjust_sys_timer(void)
{
	unsigned long next = (unsigned long)-1; // upcoming firing time, to be determined

	for (int tt = 0; tt < N_TIMERS; tt++) {
		if (!timers[tt].handler)
			continue; 
		if (timers[tt].elapseat < next) {
			if (timers[tt].elapseat < current_counter()) {
				/* timer expired, but handler not called? this could happen on
				qemu when cpu is slow. call the handler here */
				(*timers[tt].handler)(tt, timers[tt].param, timers[tt].context);
				timers[tt].handler = 0; 
			} else 
				/* give "next" a bit slack so current_counter() won't exceed
				"next" before we retuen from this function */
				next = timers[tt].elapseat + 10*1000 ;
		}
	}

	BUG_ON(current_counter() > next); 

	if (next == 0xFFFFFFFFFFFFFFFF) 
		return 0; 

	put32(TIMER_C1, (unsigned)next);  

	return 0; 
}

static int ktimer_start_nolock(unsigned delayms, TKernelTimerHandler *handler, 
		void *para, void *context) {
	unsigned t; 
	unsigned long cur; 

	for (t = 0; t < N_TIMERS; t++) {
		if (timers[t].handler == 0) 
			break; 
	}
	if (t == N_TIMERS) {
		E("ktimer_start failed. # max timer reached"); 
		return -1; 
	}

	cur = current_counter(); 
	BUG_ON(cur + TICKPERMS * delayms < cur); // 64bit counter wraps around??

	timers[t].handler = handler; 
	timers[t].param = para; 
	timers[t].context = context; 
	timers[t].elapseat = cur + TICKPERMS * delayms; 

	adjust_sys_timer(); 
	return t; 
}

int ktimer_start(unsigned delayms, TKernelTimerHandler *handler, 
		void *para, void *context) {
	int ret;
	acquire(&timerlock); 
	ret = ktimer_start_nolock(delayms, handler, para, context); 
	release(&timerlock); 
	return ret;
}

int ktimer_cancel(int t) {
	unsigned long cur; 

	if (t < 0 || t >= N_TIMERS)
		return -1; 

	cur = current_counter();
	acquire(&timerlock); 

	if (!timers[t].handler) {	// invalid handler
		release(&timerlock); 
		return -1; 
	}

	if (timers[t].elapseat < cur) { // already fired? 
		timers[t].handler = 0; 
		timers[t].context = 0; 
		timers[t].param = 0; 
		release(&timerlock); 
		return -2; 
	}

	timers[t].handler = 0; 

	adjust_sys_timer(); 	
	release(&timerlock);

	return 0;  
}

void sys_timer_irq(void) 
{
	V("called");	

	BUG_ON(!(get32(TIMER_CS) & TIMER_CS_M1));  
	put32(TIMER_CS, TIMER_CS_M1);	// clear timer1 match

	unsigned long cur = current_counter(); 

	acquire(&timerlock); 
	for (int t = 0; t < N_TIMERS; t++) {
		TKernelTimerHandler *h = timers[t].handler; 
		if (h == 0) 
			continue; 
		if (timers[t].elapseat <= cur) { // should fire  
			V("called, id %d h %lx", t, (unsigned long)timers[t].handler);	
			timers[t].handler = 0; 
			(*h)(t, timers[t].param, timers[t].context); 			
		}		
	}
	adjust_sys_timer(); 
	release(&timerlock);
}
#endif 