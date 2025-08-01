////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
#include "udffs.h"

#ifdef UDF_USE_WDISK_CACHE

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_WDISK_CACHE

#include "Include/wdisk_cache_lib.cpp"

#endif // UDF_USE_WDISK_CACHE