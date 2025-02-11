


#include "utils.h"
#include "sched.h"
#include "spinlock.h"


void initlock(struct spinlock *lk, char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
}

void acquire(struct spinlock *lk) {
#if SPINLOCK_DEBUG
    volatile long cnt = 0;
#endif


    push_off(); // disable interrupts to avoid deadlock.
    if (!lk || holding(lk)) {
        printf("%s ", lk->name);
        panic("acquire");
    }

    while (lk->locked == 1)
        ;
    lk->locked = 1;

    __sync_synchronize();

    lk->cpu = mycpu();
}

void release(struct spinlock *lk) {
    if (!lk || !holding(lk)) {
        printf("%s ", lk->name);
        panic("release");
    }

    lk->cpu = 0;

    __sync_synchronize();

    lk->locked = 0;

    pop_off();
}

int holding(struct spinlock *lk) {
    int r;
    r = (lk->locked && lk->cpu == mycpu());
    return r;
}

void push_off(void) {
    int old = intr_get();

    disable_irq();
    if (mycpu()->noff == 0)
        mycpu()->intena = old;
    mycpu()->noff += 1;
}

void pop_off(void) {
    struct cpu *c = mycpu();
    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        enable_irq();
}
