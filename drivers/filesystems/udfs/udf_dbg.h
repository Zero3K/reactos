////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
    Module name:

   Udf_dbg.h

    Abstract:

   This file contains small set of debug macroses.
   It is used by the UDF project.

   PERFORMANCE OPTIMIZATION NOTES:
   
   When UDF_DBG is defined (automatically when NDEBUG is not set), several
   debugging features are available that can significantly impact performance:

   1. TRACK_SYS_ALLOCS - Memory allocation tracking
      - Maintains descriptor array of 8192 entries
      - Tracks allocation counters
      - Linear search through descriptors on each alloc/free
      - SIGNIFICANT PERFORMANCE IMPACT - only enable for memory debugging

   2. TRACK_SYS_ALLOC_CALLERS - Caller tracking for allocations  
      - Stores source file ID and line number for each allocation
      - Additional overhead on top of TRACK_SYS_ALLOCS
      - SIGNIFICANT PERFORMANCE IMPACT - only enable for memory debugging

   3. TRACK_RESOURCES - Resource acquisition tracking
      - Logs every resource operation with thread IDs
      - Maintains acquisition counters
      - MODERATE PERFORMANCE IMPACT - only enable for resource debugging

   4. TRACK_REF_COUNTERS - Reference counter tracking
      - Logs every interlocked operation
      - MODERATE PERFORMANCE IMPACT - only enable for ref count debugging

   5. ENABLE_PROTECTED_MEM_RTL - Protected memory operations
      - Wraps every memory copy/move/compare in SEH try-catch blocks
      - Adds exception handling overhead to every memory operation
      - SIGNIFICANT PERFORMANCE IMPACT - only enable for memory corruption debugging

   By default, when UDF_DBG is defined, only basic debug printing and assertions
   are enabled. The expensive tracking features above are commented out by default
   to avoid performance degradation in debug builds.

   For production builds, ensure NDEBUG is defined to disable UDF_DBG entirely.
*/

#ifndef _UDF_DEBUG_H_
#define _UDF_DEBUG_H_

//======================================

//#define ALWAYS_CHECK_WAIT_TIMEOUT
//#define PRINT_ALWAYS

#ifdef UDF_DBG

//#define CHECK_ALLOC_FRAMES

// Memory allocation tracking - EXPENSIVE - causes significant slowdown
// Only enable when specifically debugging memory issues
//#define TRACK_SYS_ALLOCS
//#define TRACK_SYS_ALLOC_CALLERS

// Resource tracking - EXPENSIVE - causes significant slowdown  
// Only enable when specifically debugging resource issues
//#define TRACK_RESOURCES
//#define TRACK_REF_COUNTERS

// Protected memory RTL - EXPENSIVE - causes significant slowdown
// Wraps every memory operation in SEH try-catch blocks
// Only enable when specifically debugging memory corruption
//#define ENABLE_PROTECTED_MEM_RTL

#endif //UDF_DBG

// PROTECTED_MEM_RTL is expensive - only enable when ENABLE_PROTECTED_MEM_RTL is defined
#ifdef ENABLE_PROTECTED_MEM_RTL
#define PROTECTED_MEM_RTL
#endif

//#define UDF_SIMULATE_WRITES

//#define USE_PERF_PRINT

#define USE_KD_PRINT
#define USE_MM_PRINT
#define USE_AD_PRINT
#define UDF_DUMP_EXTENT
//#define USE_TH_PRINT
//#define USE_TIME_PRINT

//======================================

#if defined UDF_DBG || defined PRINT_ALWAYS

  ULONG
  _cdecl
  DbgPrint(
      PCH Format,
      ...
      );


  #ifdef KdPrint
    #undef KdPrint
  #endif

  #ifdef USE_KD_PRINT
    #define KdPrint(_x_) DbgPrint _x_
  #else
    #define KdPrint(a)  {NOTHING;}
  #endif //USE_KD_PRINT

  #ifdef USE_MM_PRINT
    #define MmPrint(_x_) DbgPrint _x_
  #else
    #define MmPrint(_x_) {NOTHING;}
  #endif //USE_MM_PRINT

  #ifdef USE_TIME_PRINT
    extern ULONG UdfTimeStamp;
    #define TmPrint(_x_) {UdfTimeStamp++;KdPrint(("TM:%d: ",UdfTimeStamp));KdPrint(_x_);}
  #else
    #define TmPrint KdPrint
  #endif //USE_MM_PRINT

  #ifdef USE_PERF_PRINT
    #define PerfPrint(_x_) DbgPrint _x_
  #else
    #define PerfPrint(_x_) {NOTHING;}
  #endif //USE_MM_PRINT

  #ifdef USE_AD_PRINT
    #define AdPrint(_x_) {DbgPrint("Thrd:%x:",PsGetCurrentThread());DbgPrint _x_;}
  #else
    #define AdPrint(_x_) {NOTHING;}
  #endif

  #ifdef USE_TH_PRINT
    #define ThPrint(_x_) {DbgPrint("Thrd:%x:",PsGetCurrentThread());DbgPrint _x_;}
  #else
    #define ThPrint(_x_) {NOTHING;}
  #endif

  #ifdef UDF_DUMP_EXTENT
    #define ExtPrint(_x_)  KdPrint(_x_)
  #else
    #define ExtPrint(_x_)  {NOTHING;}
  #endif

#else // defined UDF_DBG || defined PRINT_ALWAYS

  #define MmPrint(_x_)   {NOTHING;}
  #define TmPrint(_x_)   {NOTHING;}
  #define PerfPrint(_x_) {NOTHING;}
  #define AdPrint(_x_)   {NOTHING;}
  #define ThPrint(_x_)   {NOTHING;}
  #define ExtPrint(_x_)  {NOTHING;}

#endif // defined UDF_DBG || defined PRINT_ALWAYS

NTSTATUS
DbgWaitForSingleObject_(
    IN PVOID Object,
    IN PLARGE_INTEGER Timeout OPTIONAL
    );

#if defined ALWAYS_CHECK_WAIT_TIMEOUT
  #define DbgWaitForSingleObject(o, to)   DbgWaitForSingleObject_(o, to)
#else
  #define DbgWaitForSingleObject(o, to)   KeWaitForSingleObject(o, Executive, KernelMode, FALSE, to);
#endif

#ifdef UDF_DBG

#ifdef UDF_DBG
  #define BrutePoint() DbgBreakPoint()
#else
  #define BrutePoint() {}
#endif // UDF_DBG

#ifdef TRACK_SYS_ALLOCS

PVOID DebugAllocatePool(POOL_TYPE Type,ULONG size
#ifdef TRACK_SYS_ALLOC_CALLERS
, ULONG SrcId, ULONG SrcLine
#endif //TRACK_SYS_ALLOC_CALLERS
);
VOID DebugFreePool(PVOID addr);

#ifdef TRACK_SYS_ALLOC_CALLERS
  #define DbgAllocatePoolWithTag(a,b,c) DebugAllocatePool(a,b,UDF_BUG_CHECK_ID,__LINE__)
  #define DbgAllocatePool(x,y) DebugAllocatePool(x,y,UDF_BUG_CHECK_ID,__LINE__)
#else //TRACK_SYS_ALLOC_CALLERS
  #define DbgAllocatePoolWithTag(a,b,c) DebugAllocatePool(a,b)
  #define DbgAllocatePool(x,y) DebugAllocatePool(x,y)
#endif //TRACK_SYS_ALLOC_CALLERS
#define DbgFreePool(x) DebugFreePool(x)

#else //TRACK_SYS_ALLOCS

#define DbgAllocatePoolWithTag(a,b,c) ExAllocatePoolWithTag(a,b,c)
#define DbgAllocatePool(x,y) ExAllocatePoolWithTag(x,y,'Fnwd')
#define DbgFreePool(x) ExFreePool(x)

#endif //TRACK_SYS_ALLOCS


#ifdef PROTECTED_MEM_RTL

#if defined(__MINGW32__) || defined(__MINGW64__)
// MinGW-specific solution: use separate functions defined in udf_dbg.cpp to avoid SEH conflicts
void DbgMoveMemoryImpl(PVOID d, PVOID s, ULONG l);
void DbgCopyMemoryImpl(PVOID d, PVOID s, ULONG l);
ULONG DbgCompareMemoryImpl(PVOID d, PVOID s, ULONG l);

#define DbgMoveMemory(d, s, l) DbgMoveMemoryImpl((d), (s), (l))
#define DbgCopyMemory(d, s, l) DbgCopyMemoryImpl((d), (s), (l))
#define DbgCompareMemory(d, s, l) DbgCompareMemoryImpl((d), (s), (l))

#elif defined(__cplusplus)
// Use lambdas for other C++ compilers to avoid SEH naming conflicts
#define DbgMoveMemory(d, s, l) \
    [&]() { \
        _SEH2_TRY { \
            RtlMoveMemory(d, s, l); \
        } _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) { \
            BrutePoint(); \
        } _SEH2_END; \
    }()

#define DbgCopyMemory(d, s, l) \
    [&]() { \
        _SEH2_TRY { \
            RtlCopyMemory(d, s, l); \
        } _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) { \
            BrutePoint(); \
        } _SEH2_END; \
    }()

#define DbgCompareMemory(d, s, l) \
    [&]() -> ULONG { \
        _SEH2_TRY { \
            return RtlCompareMemory(d, s, l); \
        } _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) { \
            BrutePoint(); \
        } _SEH2_END; \
        return -1; \
    }()
#else
// C compilation - use original macro definitions
#define DbgMoveMemory(d, s, l)   \
_SEH2_TRY {                               \
    RtlMoveMemory(d, s, l);               \
} _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) {  \
    BrutePoint();                         \
} _SEH2_END;

#define DbgCopyMemory(d, s, l)   \
_SEH2_TRY {                               \
    RtlCopyMemory(d, s, l);               \
} _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) {  \
    BrutePoint();                         \
} _SEH2_END;

#define DbgCompareMemory(d, s, l) \
({ \
    ULONG _result = -1; \
    _SEH2_TRY { \
        _result = RtlCompareMemory(d, s, l); \
    } _SEH2_EXCEPT (EXCEPTION_EXECUTE_HANDLER) { \
        BrutePoint(); \
    } _SEH2_END; \
    _result; \
})
#endif

#else //PROTECTED_MEM_RTL

#define DbgMoveMemory(d, s, l)     RtlMoveMemory(d, s, l)
#define DbgCopyMemory(d, s, l)     RtlCopyMemory(d, s, l)
#define DbgCompareMemory(d, s, l)  RtlCompareMemory(d, s, l)

#endif //PROTECTED_MEM_RTL

//#define KdPrint(_x_)

#ifdef VALIDATE_STRUCTURES
#define ValidateFileInfo(fi)            \
{    /* validate FileInfo */            \
    if (!fi || (fi)->IntegrityTag) {            \
        KdPrint(("UDF: ERROR! Using deallocated structure !!!\n"));\
        BrutePoint();                   \
    }                                   \
    if (fi && !fi->Dloc) {               \
        KdPrint(("UDF: ERROR! FI without Dloc !!!\n"));\
        BrutePoint();                   \
    }                                   \
}

#else
#define ValidateFileInfo(fi)  {}
#endif

#if defined (_X86_) && defined (_MSC_VER)

__inline VOID UDFTouch(IN PVOID addr)
{
    __asm {
        mov  eax,addr
        mov  al,[byte ptr eax]
    }
}

#else   // NO X86 optimization , use generic C/C++

__inline VOID UDFTouch(IN PVOID addr)
{
    UCHAR a = ((PUCHAR)addr)[0];
    a = a;
}

#endif // _X86_

#else // UDF_DBG

#define DbgAllocatePool(x,y) ExAllocatePoolWithTag(x,y,'Fnwd')
#define DbgFreePool(x) ExFreePool(x)
#define DbgAllocatePoolWithTag(a,b,c) ExAllocatePoolWithTag(a,b,c)

#define DbgMoveMemory(d, s, l)     RtlMoveMemory(d, s, l)
#define DbgCopyMemory(d, s, l)     RtlCopyMemory(d, s, l)
#define DbgCompareMemory(d, s, l)  RtlCompareMemory(d, s, l)

#define UDFBreakPoint() {}
#define BrutePoint() {}
#define ValidateFileInfo(fi)  {}

#define UDFTouch(addr) {}

#endif // UDF_DBG

#if defined UDF_DBG || defined PRINT_ALWAYS

#define KdDump(a,b)                         \
if ((a)!=NULL) {                             \
    ULONG i;                                \
    for(i=0; i<(b); i++) {                  \
        ULONG c;                            \
        c = (ULONG)(*(((PUCHAR)(a))+i));    \
        KdPrint(("%2.2x ",c));              \
        if ((i & 0x0f) == 0x0f) KdPrint(("\n"));   \
    }                                       \
    KdPrint(("\n"));                        \
}

#else

#define KdDump(a,b) {}

#endif // UDF_DBG

#define UserPrint  KdPrint

#endif  // _UDF_DEBUG_H_
