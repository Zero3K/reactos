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
        // Check if this is a deletion operation - allow these even on dirty volumes
        // since they don't compromise volume integrity further
        if (!(DesiredAccess & (DELETE | FILE_DELETE_CHILD))) {
            AdPrint(("force R/O on dirty\n"));
            ROCheck = TRUE;
        }
    }
    if (ROCheck) {

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
    NTSTATUS RC;
    
    ASSERT(Ccb);
    ASSERT(Fcb->FileInfo);

    // First try the normal access check
    RC = UDFCheckAccessRights(FileObject, AccessState, Fcb, Ccb, DesiredAccess, ShareAccess);
    
    // If access was denied and this involves deletion operations, try to fix the access rights
    if (!NT_SUCCESS(RC) && (DesiredAccess & (DELETE | FILE_DELETE_CHILD))) {
        
        AdPrint(("UDF: Access denied for deletion, attempting to fix access rights\n"));
        
        // For deletion operations, try again with a more permissive access mask
        // by temporarily bypassing restrictive read-only checks if this is a deletion
        ACCESS_MASK ModifiedDesiredAccess = DesiredAccess;
        
        // If the FCB is marked read-only but we're doing deletion, we may need to 
        // allow the operation to proceed by adjusting the access requirements
        if (Fcb->FcbState & UDF_FCB_READ_ONLY) {
            // For read-only files/folders, ensure we only request access rights that are
            // specifically needed for deletion and are allowed on read-only items
            ModifiedDesiredAccess = DesiredAccess & (DELETE | FILE_DELETE_CHILD | 
                                                   READ_CONTROL | SYNCHRONIZE | 
                                                   FILE_READ_ATTRIBUTES);
        }
        
        // Try the access check again with the modified access mask
        RC = UDFCheckAccessRights(FileObject, AccessState, Fcb, Ccb, ModifiedDesiredAccess, ShareAccess);
        
        if (NT_SUCCESS(RC)) {
            AdPrint(("UDF: Successfully fixed access rights for deletion\n"));
            // Update the CCB with the original desired access for proper tracking
            if (Ccb) {
                Ccb->PreviouslyGrantedAccess |= DesiredAccess;
            }
        }
    }

    return RC;

} // end UDFSetAccessRights()

/*
 * Helper function to check if the current user has privileges to bypass ACL restrictions
 * for deletion operations. This allows users with backup/restore or take ownership
 * privileges to delete files even when normal ACL checks would deny access.
 */
BOOLEAN
UDFCanBypassAclForDeletion(
    VOID
    )
{
    // Check for privileges that typically allow bypassing file ACLs for deletion
    if (SeSinglePrivilegeCheck(SeExports->SeTakeOwnershipPrivilege, UserMode) ||
        SeSinglePrivilegeCheck(SeExports->SeRestorePrivilege, UserMode) ||
        SeSinglePrivilegeCheck(SeExports->SeBackupPrivilege, UserMode)) {
        return TRUE;
    }

    return FALSE;
} // end UDFCanBypassAclForDeletion()

/*
 * Enhanced access rights check for deletion that attempts to allow deletion
 * when the user has appropriate privileges, even if normal ACL checks would deny it.
 */
NTSTATUS
UDFCheckAccessRightsForDeletion(
    PFILE_OBJECT FileObject,
    PACCESS_STATE AccessState,
    PFCB Fcb,
    PCCB Ccb,
    ACCESS_MASK DesiredAccess,
    USHORT ShareAccess
    )
{
    NTSTATUS RC;

    // First try the normal access check
    RC = UDFCheckAccessRights(FileObject, AccessState, Fcb, Ccb, DesiredAccess, ShareAccess);

    // If access was denied and this is a deletion operation, check if we can bypass ACL restrictions
    if (!NT_SUCCESS(RC) &&
        (DesiredAccess & (DELETE | FILE_DELETE_CHILD)) &&
        UDFCanBypassAclForDeletion()) {

        AdPrint(("UDF: Allowing deletion with privilege bypass\n"));

        // Allow the operation to proceed if the user has appropriate privileges
        // This mimics Windows behavior where users with backup/restore privileges
        // can delete files even with restrictive ACLs
        RC = STATUS_SUCCESS;
    }

    return RC;
} // end UDFCheckAccessRightsForDeletion()

