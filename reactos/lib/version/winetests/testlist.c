/* Automatically generated file; DO NOT EDIT!! */

/* stdarg.h is needed for Winelib */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "windef.h"
#include "winbase.h"

extern void func_info(void);

struct test
{
    const char *name;
    void (*func)(void);
};

static const struct test winetest_testlist[] =
{
    { "info", func_info },
    { 0, 0 }
};

#define WINETEST_WANT_MAIN
#include "wine/test.h"
