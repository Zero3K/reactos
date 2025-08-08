////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: sys_spec.h
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   The main include file for the UDF file system driver.
*
* Author: Alter
*
*************************************************************************/

#ifndef _UDF_ENV_SPEC_H_
#define _UDF_ENV_SPEC_H_

extern NTSTATUS NTAPI UDFPhReadSynchronous(
                   PIRP_CONTEXT IrpContext,
                   PDEVICE_OBJECT      DeviceObject,
                   PVOID           Buffer,
                   SIZE_T          Length,
                   LONGLONG        Offset,
                   PSIZE_T         ReadBytes,
                   ULONG           Flags);

extern NTSTATUS NTAPI UDFPhWriteSynchronous(
                   PDEVICE_OBJECT  DeviceObject,   // the physical device object
                   PVOID           Buffer,
                   SIZE_T          Length,
                   LONGLONG        Offset,
                   PSIZE_T         WrittenBytes,
                   ULONG           Flags);
/*
extern NTSTATUS UDFPhWriteVerifySynchronous(
                   PDEVICE_OBJECT  DeviceObject,   // the physical device object
                   PVOID           Buffer,
                   SIZE_T          Length,
                   LONGLONG        Offset,
                   PSIZE_T         WrittenBytes,
                   ULONG           Flags);
*/
#define UDFPhWriteVerifySynchronous UDFPhWriteSynchronous

NTSTATUS
NTAPI
UDFTSendIOCTL(
    IN ULONG IoControlCode,
    IN PVCB Vcb,
    IN PVOID InputBuffer ,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer ,
    IN ULONG OutputBufferLength,
    IN BOOLEAN OverrideVerify,
    OUT PIO_STATUS_BLOCK Iosb OPTIONAL
    );

NTSTATUS
NTAPI
UDFPhSendIOCTL(
    IN ULONG IoControlCode,
    IN PDEVICE_OBJECT DeviceObject,
    IN PVOID InputBuffer ,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer ,
    IN ULONG OutputBufferLength,
    IN BOOLEAN OverrideVerify,
    OUT PIO_STATUS_BLOCK Iosb OPTIONAL);

// This routine performs low-level write (asynchronously if possible)
extern NTSTATUS UDFTWriteAsync(
    IN PVOID _Vcb,
    IN PVOID Buffer,     // Target buffer
    IN ULONG Length,
    IN ULONG LBA,
    OUT PULONG WrittenBytes,
    IN BOOLEAN FreeBuffer);

// SGL-enabled IO functions for improved performance
extern NTSTATUS NTAPI UDFPhReadSGL(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT DeviceObject,
    PMDL Mdl,
    LONGLONG Offset,
    PSIZE_T ReadBytes,
    ULONG Flags);

extern NTSTATUS NTAPI UDFPhWriteSGL(
    PDEVICE_OBJECT DeviceObject,
    PMDL Mdl,
    LONGLONG Offset,
    PSIZE_T WrittenBytes,
    ULONG Flags);

// Helper function to check if device supports SGL
extern BOOLEAN NTAPI UDFDeviceSupportsScatterGather(
    PDEVICE_OBJECT DeviceObject);

// Enhanced read function that automatically chooses between SGL and synchronous IO
extern NTSTATUS NTAPI UDFPhReadEnhanced(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT DeviceObject,
    PVOID Buffer,
    SIZE_T Length,
    LONGLONG Offset,
    PSIZE_T ReadBytes,
    ULONG Flags);

// Enhanced write function that automatically chooses between SGL and synchronous IO
extern NTSTATUS NTAPI UDFPhWriteEnhanced(
    PDEVICE_OBJECT DeviceObject,
    PVOID Buffer,
    SIZE_T Length,
    LONGLONG Offset,
    PSIZE_T WrittenBytes,
    ULONG Flags);

VOID
UDFNotifyFullReportChange(
    PVCB Vcb,
    PFCB Fcb,
    ULONG Filter,
    ULONG Action
    );

NTSTATUS NTAPI UDFAsyncCompletionRoutine(IN PDEVICE_OBJECT DeviceObject,
                                         IN PIRP Irp,
                                         IN PVOID Contxt);

NTSTATUS NTAPI UDFSyncCompletionRoutine(IN PDEVICE_OBJECT DeviceObject,
                                        IN PIRP Irp,
                                        IN PVOID Contxt);

NTSTATUS NTAPI UDFSyncCompletionRoutine2(IN PDEVICE_OBJECT DeviceObject,
                                         IN PIRP Irp,
                                         IN PVOID Contxt);

#define UDFGetDevType(DevObj)    (DevObj->DeviceType)


#endif  // _UDF_ENV_SPEC_H_
