////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: SecurSup.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   Contains code to handle the "Get/Set Security" dispatch entry points.
*
*************************************************************************/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_SECURITY

NTSTATUS
UDFCheckAccessRights(
    PFILE_OBJECT FileObject, // OPTIONAL
    PACCESS_STATE AccessState,
    PFCB Fcb,
    PCCB         Ccb,        // OPTIONAL
    ACCESS_MASK  DesiredAccess,
    USHORT       ShareAccess
    )
{
    NTSTATUS RC;
    BOOLEAN ROCheck = FALSE;

    // Check attr compatibility
    ASSERT(Fcb);
    ASSERT(Fcb->Vcb);

    if (Fcb->FcbState & UDF_FCB_READ_ONLY) {
        ROCheck = TRUE;
    } else
    if ((Fcb->Vcb->origIntegrityType == INTEGRITY_TYPE_OPEN) &&
        Ccb && !(Ccb->Flags & UDF_CCB_VOLUME_OPEN) &&
       (Fcb->Vcb->CompatFlags & UDF_VCB_IC_DIRTY_RO)) {
        AdPrint(("force R/O on dirty\n"));
        ROCheck = TRUE;
    } if (ROCheck) {

        //
        //  Check the desired access for a read-only dirent.  AccessMask will contain
        //  the flags we're going to allow.
        //

        ACCESS_MASK AccessMask = DELETE | READ_CONTROL | WRITE_OWNER | WRITE_DAC |
                            SYNCHRONIZE | ACCESS_SYSTEM_SECURITY | FILE_READ_DATA |
                            FILE_READ_EA | FILE_WRITE_EA | FILE_READ_ATTRIBUTES |
                            FILE_WRITE_ATTRIBUTES | FILE_EXECUTE | FILE_LIST_DIRECTORY |
                            FILE_TRAVERSE;

        //
        //  If this is a subdirectory also allow add file/directory and delete.
        //

        if (FlagOn(Fcb->FcbState, UDF_FCB_DIRECTORY)) {

            AccessMask |= FILE_ADD_SUBDIRECTORY | FILE_ADD_FILE | FILE_DELETE_CHILD;
        }

        if (FlagOn(DesiredAccess, ~AccessMask)) {

            AdPrint(("Cannot open readonly\n"));

            return STATUS_ACCESS_DENIED;
        }
    }

    if (DesiredAccess & ACCESS_SYSTEM_SECURITY) {
        if (!SeSinglePrivilegeCheck(SeExports->SeSecurityPrivilege, UserMode))
            return STATUS_ACCESS_DENIED;
        Ccb->PreviouslyGrantedAccess |= ACCESS_SYSTEM_SECURITY;
    }

    if (FileObject) {
        if (Fcb->FcbCleanup) {
            // The FCB is currently in use by some thread.
            // We must check whether the requested access/share access
            // conflicts with the existing open operations.
            RC = IoCheckShareAccess(DesiredAccess, ShareAccess, FileObject,
                                            &Fcb->ShareAccess, TRUE);

            if (Ccb)
                Ccb->PreviouslyGrantedAccess |= DesiredAccess;
            IoUpdateShareAccess(FileObject, &Fcb->ShareAccess);
        } else {
            IoSetShareAccess(DesiredAccess, ShareAccess, FileObject, &Fcb->ShareAccess);

            if (Ccb)
                Ccb->PreviouslyGrantedAccess = DesiredAccess;

            RC = STATUS_SUCCESS;
        }
    } else {
        // we get here if given file was opened for internal purposes
        RC = STATUS_SUCCESS;
    }
    return RC;
} // end UDFCheckAccessRights()

NTSTATUS
UDFSetAccessRights(
    PFILE_OBJECT FileObject,
    PACCESS_STATE AccessState,
    PFCB Fcb,
    PCCB         Ccb,
    ACCESS_MASK  DesiredAccess,
    USHORT       ShareAccess
    )
{
    ASSERT(Ccb);
    ASSERT(Fcb->FileInfo);

    return UDFCheckAccessRights(FileObject, AccessState, Fcb, Ccb, DesiredAccess, ShareAccess);

} // end UDFSetAccessRights()

/*************************************************************************
*
* Function: UDFQuerySecurity()
*
* Description:
*   The I/O Manager will invoke this routine to handle a query security
*   request
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL (invocation at higher IRQL will cause execution
*   to be deferred to a worker thread context)
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
NTAPI
UDFQuerySecurity(
    PDEVICE_OBJECT DeviceObject,       // the logical volume device object
    PIRP           Irp)                // I/O Request Packet
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN             AreWeTopLevel = FALSE;

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        if (IrpContext) {
            RC = UDFCommonQuerySecurity(IrpContext, Irp);
        } else {
            UDFCompleteRequest(IrpContext, Irp, STATUS_INSUFFICIENT_RESOURCES);
            RC = STATUS_INSUFFICIENT_RESOURCES;
        }

    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {

        RC = UDFProcessException(IrpContext, Irp);
        UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
    } _SEH2_END;

    if (AreWeTopLevel) {
        IoSetTopLevelIrp(NULL);
    }

    FsRtlExitFileSystem();

    return(RC);
} // end UDFQuerySecurity()

/*************************************************************************
*
* Function: UDFSetSecurity()
*
* Description:
*   The I/O Manager will invoke this routine to handle a set security
*   request
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL (invocation at higher IRQL will cause execution
*   to be deferred to a worker thread context)
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
NTAPI
UDFSetSecurity(
    PDEVICE_OBJECT DeviceObject,       // the logical volume device object
    PIRP           Irp)                // I/O Request Packet
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN             AreWeTopLevel = FALSE;

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        if (IrpContext) {
            RC = UDFCommonSetSecurity(IrpContext, Irp);
        } else {
            UDFCompleteRequest(IrpContext, Irp, STATUS_INSUFFICIENT_RESOURCES);
            RC = STATUS_INSUFFICIENT_RESOURCES;
        }

    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {

        RC = UDFProcessException(IrpContext, Irp);
        UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
    } _SEH2_END;

    if (AreWeTopLevel) {
        IoSetTopLevelIrp(NULL);
    }

    FsRtlExitFileSystem();

    return(RC);
} // end UDFSetSecurity()

/*************************************************************************
*
* Function: UDFCommonQuerySecurity()
*
* Description:
*   The actual work for query security is performed here. This routine
*   returns a default security descriptor for UDF files/directories.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFCommonQuerySecurity(
    PIRP_CONTEXT IrpContext,
    PIRP         Irp)
{
    NTSTATUS RC;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFCB Fcb;
    PCCB Ccb;
    TYPE_OF_OPEN TypeOfOpen;
    ULONG SecurityInformation;
    ULONG BufferLength;
    PVOID Buffer;
    
    // Decode the file object
    TypeOfOpen = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);
    
    // Check for invalid file object
    if (TypeOfOpen == UnopenedFileObject) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // Check if this is a volume open - we don't support security on volumes
    if (TypeOfOpen == UserVolumeOpen) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_DEVICE_REQUEST);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    SecurityInformation = IrpSp->Parameters.QuerySecurity.SecurityInformation;
    BufferLength = IrpSp->Parameters.QuerySecurity.Length;
    Buffer = Irp->UserBuffer;

    // Use the file system's built-in default security descriptor functionality
    // This will create an appropriate default security descriptor based on the request
    RC = SeQuerySecurityDescriptorInfo(&SecurityInformation,
                                       (PSECURITY_DESCRIPTOR)Buffer,
                                       &BufferLength,
                                       NULL);  // No security descriptor stored - use defaults

    if (RC == STATUS_BUFFER_TOO_SMALL) {
        Irp->IoStatus.Information = BufferLength;
    } else if (NT_SUCCESS(RC)) {
        Irp->IoStatus.Information = BufferLength;
    } else {
        Irp->IoStatus.Information = 0;
    }

    UDFCompleteRequest(IrpContext, Irp, RC);
    return RC;
} // end UDFCommonQuerySecurity()

/*************************************************************************
*
* Function: UDFCommonSetSecurity()
*
* Description:
*   The actual work for set security is performed here. For UDF file
*   systems, we don't support modifying security information.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_NOT_SUPPORTED
*
*************************************************************************/
NTSTATUS
UDFCommonSetSecurity(
    PIRP_CONTEXT IrpContext,
    PIRP         Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFCB Fcb;
    PCCB Ccb;
    TYPE_OF_OPEN TypeOfOpen;
    
    // Decode the file object
    TypeOfOpen = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);
    
    // Check for invalid file object
    if (TypeOfOpen == UnopenedFileObject) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // Check if this is a volume open
    if (TypeOfOpen == UserVolumeOpen) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_DEVICE_REQUEST);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    // UDF file systems typically don't support modifying security information
    // Return not supported rather than invalid device request
    Irp->IoStatus.Information = 0;
    UDFCompleteRequest(IrpContext, Irp, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
} // end UDFCommonSetSecurity()

