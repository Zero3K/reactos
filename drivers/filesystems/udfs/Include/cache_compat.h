////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDF_CACHE_COMPAT_H__
#define __UDF_CACHE_COMPAT_H__

//
// Cache Compatibility Layer
// This header provides a unified interface for both WCache and WinDiskCache
// implementations. When UDF_USE_WDISK_CACHE is defined, WCache function
// calls are redirected to their WinDiskCache equivalents.
//

#ifdef UDF_USE_WDISK_CACHE

// Include WinDiskCache header
#include "wdisk_cache_lib.h"

// Redirect WCache function calls to WinDiskCache equivalents
#define WCacheInit__                 WDiskCacheInit__
#define WCacheReadBlocks__           WDiskCacheReadBlocks__
#define WCacheWriteBlocks__          WDiskCacheWriteBlocks__
#define WCacheFlushBlocks__          WDiskCacheFlushBlocks__
#define WCacheDiscardBlocks__        WDiskCacheDiscardBlocks__
#define WCacheFlushAll__             WDiskCacheFlushAll__
#define WCachePurgeAll__             WDiskCachePurgeAll__
#define WCacheRelease__              WDiskCacheRelease__
#define WCacheIsInitialized__        WDiskCacheIsInitialized__
#define WCacheSetMode__              WDiskCacheSetMode__
#define WCacheIsCached__             WDiskCacheIsCached__
#define WCacheDirect__               WDiskCacheDirect__
#define WCacheStartDirect__          WDiskCacheStartDirect__
#define WCacheEODirect__             WDiskCacheEODirect__
#define WCacheGetWriteBlockCount__   WDiskCacheGetWriteBlockCount__
#define WCacheSyncReloc__            WDiskCacheSyncReloc__
#define WCacheChFlags__              WDiskCacheChFlags__

// Redirect mode constants
#define WCACHE_MODE_ROM              WDISK_MODE_ROM
#define WCACHE_MODE_RW               WDISK_MODE_RW
#define WCACHE_MODE_R                WDISK_MODE_R

// Redirect flag constants  
#define WCACHE_CACHE_WHOLE_PACKET    WDISK_CACHE_WHOLE_PACKET
#define WCACHE_DO_NOT_COMPARE        WDISK_DO_NOT_COMPARE
#define WCACHE_NO_WRITE_THROUGH      WDISK_NO_WRITE_THROUGH

#elif defined(UDF_USE_WCACHE)

// Include WCache header
#include "wcache_lib.h"

#else

// Neither cache implementation is enabled
#error "Either UDF_USE_WCACHE or UDF_USE_WDISK_CACHE must be defined"

#endif

#endif // __UDF_CACHE_COMPAT_H__