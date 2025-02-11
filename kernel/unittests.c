#include "plat.h"
#include "utils.h"
#include "debug.h"
#include "sched.h"

static void handler(TKernelTimerHandle hTimer, void *param, void *context) {
	unsigned sec, msec; 
	current_time(&sec, &msec);
	I("%u.%03u: fired. on cpu %d. htimer %ld, param %lx, contex %lx", sec, msec,
		cpuid(), hTimer, (unsigned long)param, (unsigned long)context); 
}

void test_ktimer() {
	unsigned sec, msec; 

	current_time(&sec, &msec); 
	I("%u.%03u start delaying 500ms...", sec, msec); 
	ms_delay(500); 
	current_time(&sec, &msec);
	I("%u.%03u ended delaying 500ms", sec, msec); 

	int t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	ms_delay(1000);
	I("timer %d should have fired", t); 

	t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	t = ktimer_start(1000, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	ms_delay(2000); 
	I("both timers should have fired"); 

	t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t);
	ms_delay(100); 
	int c = ktimer_cancel(t); 
	I("timer cancel return val = %d", c);
	BUG_ON(c < 0);

	I("there shouldn't be more callback"); 
}

extern void fb_showpicture(void); 
#include "fb.h"

#define PIXELSIZE 4  
typedef unsigned int PIXEL; 
#define N 256       // project idea: four color quads has glitches. fix it 

static inline void setpixel(unsigned char *buf, int x, int y, int pit, PIXEL p) {
    assert(x>=0 && y>=0); 
    *(PIXEL *)(buf + y*pit + x*PIXELSIZE) = p; 
}

/* 
    this shows how to flip fb, i.e. change display contents by changing only 
    the "viewport" into the underlying pixel buffers.     

    create a vir fb with four quads, with R/G/B/black. 
    phys (viewport) is of one quad size. 
    then cycle the viewport through the four quads 

    dependency: delay 
    project idea: use virtual timer instead 
        (better efficiency)

    known bug on qemu: some color quads wont dispay correctly.
    ok on rpi3 hw. likely a qemu bug
*/
void test_fb() {


    fb_fini(); 

    the_fb.width = N;
    the_fb.height = N;

    the_fb.vwidth = N*2; 
    the_fb.vheight = N*2; 

    if (fb_init() != 0) BUG();     

    PIXEL b=0x00ff0000, g=0x0000ff00, r=0x000000ff; 
    int x, y;
    int pitch = the_fb.pitch; 
    for (y=0;y<N;y++)
        for (x=0;x<N;x++)
            setpixel(the_fb.fb,x,y,pitch,r); 

    for (y=0;y<N;y++)
        for (x=N;x<2*N;x++)
            setpixel(the_fb.fb,x,y,pitch,(b|r));             

    for (y=N;y<2*N;y++)
        for (x=0;x<N;x++)
            setpixel(the_fb.fb,x,y,pitch,g); 

    for (y=N;y<2*N;y++)
        for (x=N;x<2*N;x++)
            setpixel(the_fb.fb,x,y,pitch,b);             


    __asm_flush_dcache_range(the_fb.fb, the_fb.fb + the_fb.size); 

    while (1) {
        fb_set_voffsets(0,0);
        ms_delay(1500); 
        fb_set_voffsets(0,N);
        ms_delay(1500); 
        fb_set_voffsets(N,0);
        ms_delay(1500); 
        fb_set_voffsets(N,N);
        ms_delay(1500); 
    }
}


static void kern_task_print(const char *str) {
	printf("Kernel task started at EL %d, pid %d\r\n", get_el(), myproc()->pid);

	while (1) {
		printf("%s", str); 
		ms_delay(10); // NB: spin waiting (silly). for testing sched only
		yield();
	}
}

void test_kern_tasks_print(void) {
	int res = copy_process(PF_KTHREAD, (unsigned long)&kern_task_print, 
		(unsigned long) "12345678" ,
		"kern-1"); 
	BUG_ON(res<0); 

	res = copy_process(PF_KTHREAD, (unsigned long)&kern_task_print, 
		(unsigned long) "abcdefg" ,
		"kern-2"); 
	BUG_ON(res<0);

	while (1)
        	yield();
}


static void kern_task_return(const char *str) {
	printf("Kernel task started at EL %d, pid %d\r\n", get_el(), myproc()->pid);
    printf("%s", str); 
    return;     
}

static void kern_task_exit(const char *str) {
	printf("Kernel task started at EL %d, pid %d\r\n", get_el(), myproc()->pid);
    printf("%s", str); 
    exit_process(0); 
}

void test_kern_task_mgmt(void) {
	int res = copy_process(PF_KTHREAD, (unsigned long)&kern_task_return, 
		(unsigned long) "12345678" ,
		"kern-1");         
	BUG_ON(res<0); 

	res = copy_process(PF_KTHREAD, (unsigned long)&kern_task_exit, 
		(unsigned long) "abcdefg" ,
		"kern-2"); 
	BUG_ON(res<0);    
}


#define NSIZE 32
static struct spinlock testlock = {.locked=0, .cpu=0, .name="testlock"};
static char pipebuf[NSIZE];
static int nwrite=0, nread=0; 

static void do_write(const char *str, int n) {
    
    // ChatGPT'd this part
    int i=0; 
    acquire(&testlock); 

    while (i<n) {

        if (nwrite == nread + NSIZE) {
            
            sleep ( &pipebuf, &testlock);
        } 
        
        else {
            
            pipebuf[nwrite % NSIZE] = str[i];
            nwrite++;
            i++;
            wakeup(&pipebuf); 
        }
    }
    
    release(&testlock);
}

static int do_read(char *str, int n) {
    
    
    int i; 

    acquire(&testlock); 


    while (nread == nwrite) { sleep(&pipebuf, &testlock);}
    
    for (i=0; i<n; i++) {
            
        if (nread == nwrite) {
            
            break;
            
        }



        str[i] = pipebuf[nread % NSIZE];
        nread++;


        // Awaken the reader again
        wakeup(&pipebuf);
        
    }
    
    release(&testlock); 

    return i; 
}

static void task_writer() {
    

    static const char wordsworth[] = "O Nature! Thou art ever kind and free,"
                    "Thy gentle whispers calm the restless soul;"
                    "The streams, the woods, the sky, the endless sea,"
                    "In thee, we find our being's truest goal.";

    while (1) {
        do_write(wordsworth, strlen(wordsworth)); // NB: strlen does NOT count '\0'
        ms_delay(100); // spin waiting (silly). for testing only
    }
}

static void task_reader() {
#define MYBUFLEN 17 // can be small 
    char mybuf[MYBUFLEN];
    int n; 
    while (1) {
        n = do_read(mybuf, MYBUFLEN-1); // need one char for '\0'
        mybuf[n] = '\0';
        W("read: %d bytes. %s", n, mybuf);
    }
}

void test_kern_reader_writer() {
	int res = copy_process(PF_KTHREAD, (unsigned long)&task_writer, 
		0 , "writer");         
	BUG_ON(res<0); 

	res = copy_process(PF_KTHREAD, (unsigned long)&task_reader, 
		0 , "reader"); 
	BUG_ON(res<0);    
}



extern void donut(int idx); 	//donut.c
extern void donut_canvas_init(void); //donut.c don't forget to init canvas -- once
void kern_task_donut(int idx) {
	printf("process started EL %d, pid %d idx %d\r\n", 
        get_el(), myproc()->pid, idx);
	

    
    donut(idx);
    while (1) {
        yield(); 
    }
}

void test_kern_tasks_donut(void) {
    char name[10]; 
    int res; 

    donut_canvas_init(); 
    
    for (int i=0; i<N_DONUTS; i++) {
        snprintf(name, 10, "donut-%d", i); 
        
        res = copy_process(PF_KTHREAD, (unsigned long)&kern_task_donut, 
            (unsigned long) i , name);
        BUG_ON(res<0);
    }

	while (1)
        	yield();
	
}

