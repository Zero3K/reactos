////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDF_CACHE_H__
#define __UDF_CACHE_H__

// Windows Cache Manager callbacks - used instead of old custom cache system
void init_cache();
extern CACHE_MANAGER_CALLBACKS cache_callbacks;

#endif // __UDF_CACHE_H__
