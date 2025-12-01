#include <stdio.h>
#include <setjmp.h>

/** Holds information to implement exception handling. */
__thread jmp_buf ex_buf__;