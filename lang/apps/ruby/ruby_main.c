/* Ruby Runtime for Vibe OS - mruby Integration */

#include "vibe_app.h"
#include "vibe_app_runtime.h"
#include <string.h>

/* Forward declarations for mruby */
typedef struct mrb_state mrb_state;
typedef struct mrb_value mrb_value;

extern mrb_state *mrb_open(void);
extern void mrb_close(mrb_state *mrb);
extern mrb_value mrb_load_string(mrb_state *mrb, const char *code);
extern int mrb_exc_p(mrb_state *mrb, mrb_value v);
extern mrb_value mrb_funcall(mrb_state *mrb, mrb_value obj, const char *method, int argc, ...);

int vibe_app_main(int argc, char **argv) {
    vibe_app_console_write("Ruby Runtime (mruby)\n\n");
    
    mrb_state *mrb = mrb_open();
    if (!mrb) {
        vibe_app_console_write("ERROR: Failed to open mruby\n");
        return 1;
    }
    
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
                mrb_value result = mrb_load_string(mrb, buffer);
                if (mrb_exc_p(mrb, result)) {
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
        vibe_app_console_write("REPL Mode (type 'exit' to quit)\n");
        vibe_app_console_write("irb> ");
        
        char buffer[512] = {0};
        int pos = 0;
        
        while (1) {
            unsigned char ch = vibe_app_poll_key();
            if (ch == 0) continue;
            
            if (ch == '\r' || ch == '\n') {
                vibe_app_console_putc('\n');
                
                if (pos > 0) {
                    buffer[pos] = 0;
                    
                    if (pos == 4 && buffer[0] == 'e' && buffer[1] == 'x' && 
                        buffer[2] == 'i' && buffer[3] == 't') {
                        break;
                    }
                    
                    mrb_value result = mrb_load_string(mrb, buffer);
                    if (!mrb_exc_p(mrb, result)) {
                        vibe_app_console_write("=> ");
                        vibe_app_console_write(buffer);
                        vibe_app_console_write("\n");
                    }
                    pos = 0;
                }
                
                vibe_app_console_write("irb> ");
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
    
    mrb_close(mrb);
    return 0;
}
