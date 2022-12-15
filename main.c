#include <stdio.h>

#include "shell.h"

char line_buf[SHELL_DEFAULT_BUFSIZE];

int main(void) {
  puts("hi from RIOT!");

  shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

  return 0;
}
