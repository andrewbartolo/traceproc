#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

void
print_message_and_die(const char* format, ...)
{
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, argptr);
    fprintf(stderr, "\n");
    va_end(argptr);
    exit(1);
}
