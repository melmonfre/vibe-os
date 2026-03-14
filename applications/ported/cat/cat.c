/*
 * Cat - VibeOS Ported Application
 * GNU coreutils cat implementation
 * 
 * Print FILE(s) to standard output.
 * 
 * Usage: cat [FILE]...
 * 
 * Concatenate FILE(s) to standard output.
 * With no FILE, or when FILE is -, read from standard input.
 */

#include "compat/include/compat.h"

/* Read and print a file by name */
static int cat_file(const char *filename) {
    int rc;
    const char *data;
    int size;
    
    if (!filename || filename[0] == '\0' || 
        (filename[0] == '-' && filename[1] == '\0')) {
        /* Read from stdin - stub for now */
        printf("cat: stdin not supported yet\n");
        return 0;
    }
    
    /* Try to read file via app runtime */
    rc = vibe_app_read_file(filename, (const char **)&data, &size);
    if (rc != 0 || !data || size <= 0) {
        printf("cat: %s: No such file or directory\n", filename);
        return 1;
    }
    
    /* Output file contents */
    for (int i = 0; i < size; i++) {
        putchar((unsigned char)data[i]);
    }
    
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    int exit_status = 0;
    
    if (argc <= 1) {
        /* No files - read from stdin (stubbed) */
        printf("cat: reading from stdin not yet implemented\n");
        return 0;
    }
    
    /* Process each file argument */
    for (int i = 1; i < argc; i++) {
        int rc = cat_file(argv[i]);
        if (rc != 0) {
            exit_status = 1;
        }
    }
    
    return exit_status;
}
