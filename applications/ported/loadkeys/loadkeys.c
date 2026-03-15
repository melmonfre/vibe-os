#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define SYSCALL_KEYBOARD_SET_LAYOUT 21
#define SYSCALL_KEYBOARD_GET_LAYOUT 22
#define SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS 23

// This is a mock implementation of the syscalls for now.
// The real implementation will be in the kernel.
static inline int syscall(int num, ...) {
    // This is just a placeholder.
    return 0;
}

void print_help() {
    char available_layouts[256];
    char current_layout[32];

    syscall(SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS, available_layouts, sizeof(available_layouts));
    syscall(SYSCALL_KEYBOARD_GET_LAYOUT, current_layout, sizeof(current_layout));

    printf("Uso: loadkeys <layout>
");
    printf("
");
    printf("Layouts disponíveis:
%s
", available_layouts);
    printf("Layout atual:
%s
", current_layout);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }

    if (syscall(SYSCALL_KEYBOARD_SET_LAYOUT, argv[1]) != 0) {
        printf("layout desconhecido: %s
", argv[1]);
        return 1;
    }

    printf("Layout do teclado alterado para: %s
", argv[1]);

    return 0;
}
