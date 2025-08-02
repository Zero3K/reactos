////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
#include "udffs.h"

// Cache Manager callbacks - similar to BTRFS implementation
CACHE_MANAGER_CALLBACKS cache_callbacks;

static BOOLEAN __stdcall acquire_for_lazy_write(PVOID Context, BOOLEAN Wait) {
    PFILE_OBJECT FileObject = (PFILE_OBJECT)Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: acquire_for_lazy_write(%p, %u)\n", Context, Wait));

    // For build and git clone performance, try shared VCB access first to reduce contention
    // More aggressive approach for git's mixed I/O patterns
    if (!ExAcquireResourceSharedLite(&fcb->Vcb->VcbResource, Wait)) {
        // If VCB access fails and we can't wait, return immediately
        if (!Wait) return FALSE;
        // Try with Wait=TRUE if originally requested
        if (!ExAcquireResourceSharedLite(&fcb->Vcb->VcbResource, TRUE))
            return FALSE;
    }

    if (!ExAcquireResourceExclusiveLite(fcb->Header.Resource, Wait)) {
        ExReleaseResourceLite(&fcb->Vcb->VcbResource);
        return FALSE;
    }

    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);

    return TRUE;
}

static void __stdcall release_from_lazy_write(PVOID Context) {
    PFILE_OBJECT FileObject = (PFILE_OBJECT)Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: release_from_lazy_write(%p)\n", Context));

    ExReleaseResourceLite(fcb->Header.Resource);
    ExReleaseResourceLite(&fcb->Vcb->VcbResource);

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
        IoSetTopLevelIrp(NULL);
}

static BOOLEAN __stdcall acquire_for_read_ahead(PVOID Context, BOOLEAN Wait) {
    PFILE_OBJECT FileObject = (PFILE_OBJECT)Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: acquire_for_read_ahead(%p, %u)\n", Context, Wait));

    // For build and git clone performance, always try to acquire with wait=FALSE for read-ahead
    // to avoid blocking other operations - more aggressive than previous optimization
    if (!Wait) {
        // For git clone patterns, try shared access first, then try without waiting
        if (!ExAcquireResourceSharedLite(fcb->Header.Resource, FALSE)) {
            // If we can't get shared access immediately, try VCB resource instead
            if (!ExAcquireResourceSharedLite(&fcb->Vcb->VcbResource, FALSE))
                return FALSE;
            ExReleaseResourceLite(&fcb->Vcb->VcbResource);
            if (!ExAcquireResourceSharedLite(fcb->Header.Resource, FALSE))
                return FALSE;
        }
    } else {
        if (!ExAcquireResourceSharedLite(fcb->Header.Resource, Wait))
            return FALSE;
    }

    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);

    return TRUE;
}

static void __stdcall release_from_read_ahead(PVOID Context) {
    PFILE_OBJECT FileObject = (PFILE_OBJECT)Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: release_from_read_ahead(%p)\n", Context));

    ExReleaseResourceLite(fcb->Header.Resource);

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
        IoSetTopLevelIrp(NULL);
}

void init_cache() {
    cache_callbacks.AcquireForLazyWrite = acquire_for_lazy_write;
    cache_callbacks.ReleaseFromLazyWrite = release_from_lazy_write;
    cache_callbacks.AcquireForReadAhead = acquire_for_read_ahead;
    cache_callbacks.ReleaseFromReadAhead = release_from_read_ahead;
}
