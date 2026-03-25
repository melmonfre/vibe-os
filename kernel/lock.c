#include <kernel/lock.h>
#include <kernel/interrupt.h>

static inline uint32_t spinlock_xchg(volatile uint32_t *ptr, uint32_t value) {
    __asm__ volatile("xchgl %0, %1"
                     : "+r"(value), "+m"(*ptr)
                     :
                     : "memory", "cc");
    return value;
}

void spinlock_init(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    lock->value = 0u;
}

void spinlock_lock(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    while (spinlock_xchg(&lock->value, 1u) != 0u) {
        while (lock->value != 0u) {
            __asm__ volatile("pause");
        }
    }
}

int spinlock_trylock(spinlock_t *lock) {
    if (!lock) {
        return 0;
    }
    return spinlock_xchg(&lock->value, 1u) == 0u;
}

void spinlock_unlock(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    __asm__ volatile("" ::: "memory");
    lock->value = 0u;
}

uint32_t spinlock_lock_irqsave(spinlock_t *lock) {
    uint32_t flags = kernel_irq_save();
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t *lock, uint32_t flags) {
    spinlock_unlock(lock);
    kernel_irq_restore(flags);
}
