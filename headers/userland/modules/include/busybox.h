#ifndef BUSYBOX_H
#define BUSYBOX_H

/* one binary providing multiple small utilities */

int busybox_main(int argc, char **argv);
int busybox_command_uses_external_app(int argc, char **argv);

#endif // BUSYBOX_H
