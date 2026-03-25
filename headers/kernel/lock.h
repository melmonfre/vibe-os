#ifndef KERNEL_LOCK_H
#define KERNEL_LOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t value;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
int spinlock_trylock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
uint32_t spinlock_lock_irqsave(spinlock_t *lock);
void spinlock_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

#endif
