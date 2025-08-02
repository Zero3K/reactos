////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
#include "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_SHUTDOWN

VOID
NTAPI
UDFDriverUnload(
    IN PDRIVER_OBJECT DriverObject
    )
{
//    UNICODE_STRING uniWin32NameString;
    LARGE_INTEGER delay;

    //
    // All *THIS* driver needs to do is to delete the device object and the
    // symbolic link between our device name and the Win32 visible name.
    //
    // Almost every other driver ever written would need to do a
    // significant amount of work here deallocating stuff.
    //

    UDFPrint( ("UDF: Unloading!!\n") );

    // prevent mount oparations
    UdfData.Flags |= UDF_DATA_FLAGS_SHUTDOWN;

    // wait for all volumes to be dismounted
    delay.QuadPart = 10*1000*1000*10; // 10 seconds
    
    // Check if there are any mounted volumes and wait for them to be dismounted
    // Instead of infinite loop, we'll do a limited number of checks
    ULONG maxWaitCycles = 30; // Wait up to 5 minutes (30 * 10 seconds)
    ULONG waitCycle = 0;
    
    while(waitCycle < maxWaitCycles) {
        BOOLEAN volumesStillMounted = FALSE;
        
        // Check if there are any volumes still mounted
        UDFAcquireResourceShared(&UdfData.GlobalDataResource, TRUE);
        if (!IsListEmpty(&UdfData.VcbQueue)) {
            volumesStillMounted = TRUE;
        }
        UDFReleaseResource(&UdfData.GlobalDataResource);
        
        if (!volumesStillMounted) {
            UDFPrint(("All volumes dismounted, proceeding with unload\n"));
            break;
        }
        
        UDFPrint(("Waiting for volumes to dismount... (cycle %d/%d)\n", waitCycle + 1, maxWaitCycles));
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        waitCycle++;
    }
    
    if (waitCycle >= maxWaitCycles) {
        UDFPrint(("Timeout waiting for volumes to dismount, forcing unload\n"));
    }

    // Create counted string version of our Win32 device name.


//    RtlInitUnicodeString( &uniWin32NameString, DOS_DEVICE_NAME );


    // Delete the link from our device name to a name in the Win32 namespace.


//    IoDeleteSymbolicLink( &uniWin32NameString );


    // Finally delete our device object


//    IoDeleteDevice( DriverObject->DeviceObject );
}
