#include <sched.h>
#include <lang/include/vibe_app_runtime.h>

int sched_yield(void) {
    return vibe_app_thread_yield();
}

int sched_get_priority_max(int policy) {
    (void)policy;
    return 0;
}

int sched_get_priority_min(int policy) {
    (void)policy;
    return 0;
}
