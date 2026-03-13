#include <stage2/include/userland.h>
#include <stddef.h>
#include <stdint.h>

#define USERLAND_LOAD_ADDR 0x20000u

typedef void (*userland_entry_t)(void);

extern const uint8_t _binary_userland_bin_start[];
extern const uint8_t _binary_userland_bin_end[];

void userland_run(void) {
    /* Calculate size */
    const uint8_t *start = _binary_userland_bin_start;
    const uint8_t *end = _binary_userland_bin_end;
    size_t size = end - start;
    
    if (size == 0) {
        return;
    }
    
    /* Copy to 0x20000 */
    uint8_t *dest = (uint8_t *)USERLAND_LOAD_ADDR;
    for (size_t i = 0; i < size; i++) {
        dest[i] = start[i];
    }
    
    /* Jump to entry point */
    userland_entry_t entry = (userland_entry_t)dest;
    entry();
}
