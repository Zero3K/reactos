////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
#include "udffs.h"

// Cache Manager callbacks - similar to BTRFS implementation
CACHE_MANAGER_CALLBACKS cache_callbacks;

static BOOLEAN __stdcall acquire_for_lazy_write(PVOID Context, BOOLEAN Wait) {
    PFILE_OBJECT FileObject = Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: acquire_for_lazy_write(%p, %u)\n", Context, Wait));

    if (!ExAcquireResourceSharedLite(&fcb->Vcb->VcbResource, Wait))
        return FALSE;

    if (!ExAcquireResourceExclusiveLite(&fcb->NTRequiredFCB->MainResource, Wait)) {
        ExReleaseResourceLite(&fcb->Vcb->VcbResource);
        return FALSE;
    }

    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);

    return TRUE;
}

static void __stdcall release_from_lazy_write(PVOID Context) {
    PFILE_OBJECT FileObject = Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: release_from_lazy_write(%p)\n", Context));

    ExReleaseResourceLite(&fcb->NTRequiredFCB->MainResource);
    ExReleaseResourceLite(&fcb->Vcb->VcbResource);

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
        IoSetTopLevelIrp(NULL);
}

static BOOLEAN __stdcall acquire_for_read_ahead(PVOID Context, BOOLEAN Wait) {
    PFILE_OBJECT FileObject = Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: acquire_for_read_ahead(%p, %u)\n", Context, Wait));

    if (!ExAcquireResourceSharedLite(&fcb->NTRequiredFCB->MainResource, Wait))
        return FALSE;

    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);

    return TRUE;
}

static void __stdcall release_from_read_ahead(PVOID Context) {
    PFILE_OBJECT FileObject = Context;
    FCB* fcb = (FCB*)FileObject->FsContext;

    UDFPrint(("UDF: release_from_read_ahead(%p)\n", Context));

    ExReleaseResourceLite(&fcb->NTRequiredFCB->MainResource);

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
        IoSetTopLevelIrp(NULL);
}

void init_cache() {
    cache_callbacks.AcquireForLazyWrite = acquire_for_lazy_write;
    cache_callbacks.ReleaseFromLazyWrite = release_from_lazy_write;
    cache_callbacks.AcquireForReadAhead = acquire_for_read_ahead;
    cache_callbacks.ReleaseFromReadAhead = release_from_read_ahead;
}
