/* Python Runtime for Vibe OS - MicroPython Integration */

#include "vibe_app.h"
#include "vibe_app_runtime.h"
#include <string.h>

/* Forward declarations for MicroPython */
typedef struct mp_obj_t mp_obj_t;
typedef struct mp_lexer_t mp_lexer_t;

extern void mp_init(void);
extern void mp_deinit(void);
extern mp_obj_t mp_parse_and_execute(const char *code);
extern int mp_has_error(void);

int vibe_app_main(int argc, char **argv) {
    vibe_app_console_write("Python Runtime (MicroPython)\n\n");
    
    mp_init();
    
    if (argc > 1) {
        /* File execution mode */
        const char *file_data;
        int file_size;
        
        vibe_app_console_write("Executing: ");
        vibe_app_console_write(argv[1]);
        vibe_app_console_write("\n\n");
        
        if (vibe_app_read_file(argv[1], &file_data, &file_size) == 0) {
            char buffer[4096] = {0};
            if (file_size < (int)sizeof(buffer) - 1) {
                memcpy(buffer, file_data, file_size);
                buffer[file_size] = 0;
                mp_parse_and_execute(buffer);
                if (mp_has_error()) {
                    vibe_app_console_write("ERROR: Exception during execution\n");
                }
            }
        } else {
            vibe_app_console_write("ERROR: File not found: ");
            vibe_app_console_write(argv[1]);
            vibe_app_console_write("\n");
        }
    } else {
        /* REPL mode */
        vibe_app_console_write("REPL Mode (type 'exit()' to quit)\n");
        vibe_app_console_write(">>> ");
        
        char buffer[512] = {0};
        int pos = 0;
        
        while (1) {
            unsigned char ch = vibe_app_poll_key();
            if (ch == 0) continue;
            
            if (ch == '\r' || ch == '\n') {
                vibe_app_console_putc('\n');
                
                if (pos > 0) {
                    buffer[pos] = 0;
                    
                    if (pos == 6 && buffer[0] == 'e' && buffer[1] == 'x' && 
                        buffer[2] == 'i' && buffer[3] == 't' && buffer[4] == '(' && 
                        buffer[5] == ')') {
                        break;
                    }
                    
                    mp_parse_and_execute(buffer);
                    pos = 0;
                }
                
                vibe_app_console_write(">>> ");
            } else if (ch == '\b' || ch == 127) {
                if (pos > 0) {
                    pos--;
                    vibe_app_console_putc('\b');
                    vibe_app_console_putc(' ');
                    vibe_app_console_putc('\b');
                }
            } else if (ch >= 32 && ch < 127) {
                if (pos < (int)sizeof(buffer) - 1) {
                    buffer[pos++] = ch;
                    vibe_app_console_putc(ch);
                }
            }
        }
        
        vibe_app_console_write("\nGoodbye!\n");
    }
    
    mp_deinit();
    return 0;
}
