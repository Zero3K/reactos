////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __CDRW_WCACHE_H__
#define __CDRW_WCACHE_H__

// Cache Manager callbacks - similar to BTRFS implementation
void init_cache();
extern CACHE_MANAGER_CALLBACKS cache_callbacks;

#endif // __CDRW_WCACHE_H__
