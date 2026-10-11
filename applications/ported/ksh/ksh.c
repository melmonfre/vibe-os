#include <stddef.h>
extern int shell_bsd_main(int, char **);
int main(int argc, char **argv) {
    return shell_bsd_main(argc, argv);
}
