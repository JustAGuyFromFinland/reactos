/*
 * PROJECT:     ReactOS Kernel&Driver SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware Resources Arbiter Library
 * COPYRIGHT:   Copyright 2020 Vadim Galyant <vgal@rambler.ru>
 */

/* INCLUDES *******************************************************************/

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include "arbiter.h"

#include <reactos/debug.h>

/* GLOBALS ********************************************************************/

/* DATA **********************************************************************/

/* FUNCTIONS ******************************************************************/

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbTestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRetestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddReserved(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPreprocessEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAllocateEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbGetNextAllocationRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return FALSE;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbFindSuitableRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return FALSE;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbAddAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbBacktrackAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();

    UNIMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* FIXME: the prototype is not correct yet. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbStartArbiter(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    PARBITER_ORDERING NewOrderings;
    UINT16 NewMaximum;

    PAGED_CODE();

    ASSERT(OrderList != NULL);
    ASSERT(MinimumAddress <= MaximumAddress);

    DPRINT("ArbAddOrdering: Adding range 0x%I64x - 0x%I64x\n", MinimumAddress, MaximumAddress);

    /* Check if we need to expand the array */
    if (OrderList->Count >= OrderList->Maximum)
    {
        /* Calculate new size (double the current size or start with 4) */
        NewMaximum = (OrderList->Maximum == 0) ? 4 : (OrderList->Maximum * 2);

        /* Allocate new array */
        NewOrderings = ExAllocatePoolWithTag(PagedPool,
                                              NewMaximum * sizeof(ARBITER_ORDERING),
                                              TAG_ARB_RANGE);
        if (!NewOrderings)
        {
            DPRINT1("ArbAddOrdering: Failed to allocate ordering array\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Copy existing entries */
        if (OrderList->Orderings != NULL)
        {
            RtlCopyMemory(NewOrderings,
                          OrderList->Orderings,
                          OrderList->Count * sizeof(ARBITER_ORDERING));
            ExFreePoolWithTag(OrderList->Orderings, TAG_ARB_RANGE);
        }

        /* Update the list */
        OrderList->Orderings = NewOrderings;
        OrderList->Maximum = NewMaximum;
    }

    /* Add the new ordering */
    OrderList->Orderings[OrderList->Count].Start = MinimumAddress;
    OrderList->Orderings[OrderList->Count].End = MaximumAddress;
    OrderList->Count++;

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    UINT16 i, WriteIndex;

    PAGED_CODE();

    ASSERT(OrderingList != NULL);
    ASSERT(MinimumAddress <= MaximumAddress);

    DPRINT("ArbPruneOrdering: Pruning range 0x%I64x - 0x%I64x\n", MinimumAddress, MaximumAddress);

    WriteIndex = 0;

    /* Remove any orderings that overlap with the specified range */
    for (i = 0; i < OrderingList->Count; i++)
    {
        PARBITER_ORDERING Current = &OrderingList->Orderings[i];

        /* Check if the current ordering overlaps with the range to prune */
        if (Current->End < MinimumAddress || Current->Start > MaximumAddress)
        {
            /* No overlap - keep this ordering */
            if (WriteIndex != i)
            {
                OrderingList->Orderings[WriteIndex] = *Current;
            }
            WriteIndex++;
        }
        /* Otherwise, skip this ordering (effectively removing it) */
    }

    /* Update the count */
    OrderingList->Count = WriteIndex;

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    ASSERT(OrderList != NULL);

    /* Initialize the ordering list structure */
    OrderList->Count = 0;
    OrderList->Maximum = 0;
    OrderList->Orderings = NULL;

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    ASSERT(OrderList != NULL);

    /* Free the orderings array if allocated */
    if (OrderList->Orderings != NULL)
    {
        ExFreePoolWithTag(OrderList->Orderings, TAG_ARB_RANGE);
        OrderList->Orderings = NULL;
    }

    /* Reset the list */
    OrderList->Count = 0;
    OrderList->Maximum = 0;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE ArbInstance,
    _In_ PCWSTR OrderName,
    _In_ PCWSTR ReservedOrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE KeyHandle = NULL;
    HANDLE OrderKeyHandle = NULL;
    HANDLE ReservedKeyHandle = NULL;
    PKEY_VALUE_FULL_INFORMATION ValueInfo = NULL;
    ULONG ResultLength;
    ULONG i;

    PAGED_CODE();

    DPRINT("ArbBuildAssignmentOrdering: OrderName '%S', ReservedOrderName '%S'\n", 
           OrderName, ReservedOrderName);

    /* Initialize the ordering lists */
    Status = ArbInitializeOrderingList(&ArbInstance->OrderingList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ArbBuildAssignmentOrdering: Failed to initialize ordering list (0x%08X)\n", Status);
        return Status;
    }

    Status = ArbInitializeOrderingList(&ArbInstance->ReservedList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ArbBuildAssignmentOrdering: Failed to initialize reserved list (0x%08X)\n", Status);
        ArbFreeOrderingList(&ArbInstance->OrderingList);
        return Status;
    }

    /* Try to open the registry key for ordering information */
    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\SystemResources\\AssignmentOrdering");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ArbBuildAssignmentOrdering: No assignment ordering key found (0x%08X)\n", Status);
        /* This is not an error - we'll use default ordering */
        return STATUS_SUCCESS;
    }

    /* Try to open the specific ordering subkey */
    RtlInitUnicodeString(&KeyName, OrderName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               KeyHandle,
                               NULL);

    Status = ZwOpenKey(&OrderKeyHandle, KEY_READ, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        /* Enumerate values in the ordering key */
        for (i = 0; ; i++)
        {
            Status = ZwEnumerateValueKey(OrderKeyHandle,
                                         i,
                                         KeyValueFullInformation,
                                         NULL,
                                         0,
                                         &ResultLength);
            
            if (Status == STATUS_NO_MORE_ENTRIES)
            {
                break;
            }
            
            if (Status != STATUS_BUFFER_TOO_SMALL)
            {
                continue;
            }

            ValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_ARBITER);
            if (!ValueInfo)
            {
                continue;
            }

            Status = ZwEnumerateValueKey(OrderKeyHandle,
                                         i,
                                         KeyValueFullInformation,
                                         ValueInfo,
                                         ResultLength,
                                         &ResultLength);
            
            if (NT_SUCCESS(Status) && 
                ValueInfo->Type == REG_RESOURCE_LIST &&
                ValueInfo->DataLength >= sizeof(CM_RESOURCE_LIST))
            {
                PCM_RESOURCE_LIST ResourceList = (PCM_RESOURCE_LIST)((PUCHAR)ValueInfo + ValueInfo->DataOffset);
                ULONG j;

                /* Process each resource descriptor */
                if (ResourceList->Count > 0)
                {
                    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor = &ResourceList->List[0];
                    
                    for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
                    {
                        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[j];
                        
                        if (Descriptor->Type == ArbInstance->ResourceType)
                        {
                            UINT64 Start, End;
                            
                            /* Extract resource range */
                            if (ArbInstance->ResourceType == CmResourceTypePort ||
                                ArbInstance->ResourceType == CmResourceTypeMemory)
                            {
                                Start = Descriptor->u.Generic.Start.QuadPart;
                                End = Start + Descriptor->u.Generic.Length - 1;
                            }
                            else if (ArbInstance->ResourceType == CmResourceTypeInterrupt)
                            {
                                Start = Descriptor->u.Interrupt.Level;
                                End = Start;
                            }
                            else if (ArbInstance->ResourceType == CmResourceTypeBusNumber)
                            {
                                Start = Descriptor->u.BusNumber.Start;
                                End = Start + Descriptor->u.BusNumber.Length - 1;
                            }
                            else
                            {
                                continue;
                            }

                            /* Add to ordering list */
                            ArbAddOrdering(&ArbInstance->OrderingList, Start, End);
                        }
                    }
                }
            }

            ExFreePoolWithTag(ValueInfo, TAG_ARBITER);
            ValueInfo = NULL;
        }

        ZwClose(OrderKeyHandle);
    }

    /* Try to open the reserved ordering subkey if it's different */
    if (wcscmp(OrderName, ReservedOrderName) != 0)
    {
        RtlInitUnicodeString(&KeyName, ReservedOrderName);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   KeyHandle,
                                   NULL);

        Status = ZwOpenKey(&ReservedKeyHandle, KEY_READ, &ObjectAttributes);
        if (NT_SUCCESS(Status))
        {
            /* Enumerate values in the reserved ordering key */
            for (i = 0; ; i++)
            {
                Status = ZwEnumerateValueKey(ReservedKeyHandle,
                                             i,
                                             KeyValueFullInformation,
                                             NULL,
                                             0,
                                             &ResultLength);
                
                if (Status == STATUS_NO_MORE_ENTRIES)
                {
                    break;
                }
                
                if (Status != STATUS_BUFFER_TOO_SMALL)
                {
                    continue;
                }

                ValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_ARBITER);
                if (!ValueInfo)
                {
                    continue;
                }

                Status = ZwEnumerateValueKey(ReservedKeyHandle,
                                             i,
                                             KeyValueFullInformation,
                                             ValueInfo,
                                             ResultLength,
                                             &ResultLength);
                
                if (NT_SUCCESS(Status) && 
                    ValueInfo->Type == REG_RESOURCE_LIST &&
                    ValueInfo->DataLength >= sizeof(CM_RESOURCE_LIST))
                {
                    PCM_RESOURCE_LIST ResourceList = (PCM_RESOURCE_LIST)((PUCHAR)ValueInfo + ValueInfo->DataOffset);
                    ULONG j;

                    /* Process each resource descriptor */
                    if (ResourceList->Count > 0)
                    {
                        PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor = &ResourceList->List[0];
                        
                        for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
                        {
                            PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[j];
                            
                            if (Descriptor->Type == ArbInstance->ResourceType)
                            {
                                UINT64 Start, End;
                                
                                /* Extract resource range */
                                if (ArbInstance->ResourceType == CmResourceTypePort ||
                                    ArbInstance->ResourceType == CmResourceTypeMemory)
                                {
                                    Start = Descriptor->u.Generic.Start.QuadPart;
                                    End = Start + Descriptor->u.Generic.Length - 1;
                                }
                                else if (ArbInstance->ResourceType == CmResourceTypeInterrupt)
                                {
                                    Start = Descriptor->u.Interrupt.Level;
                                    End = Start;
                                }
                                else if (ArbInstance->ResourceType == CmResourceTypeBusNumber)
                                {
                                    Start = Descriptor->u.BusNumber.Start;
                                    End = Start + Descriptor->u.BusNumber.Length - 1;
                                }
                                else
                                {
                                    continue;
                                }

                                /* Add to reserved list */
                                ArbAddOrdering(&ArbInstance->ReservedList, Start, End);
                            }
                        }
                    }
                }

                ExFreePoolWithTag(ValueInfo, TAG_ARBITER);
                ValueInfo = NULL;
            }

            ZwClose(ReservedKeyHandle);
        }
    }

    if (KeyHandle)
    {
        ZwClose(KeyHandle);
    }

    DPRINT("ArbBuildAssignmentOrdering: Built ordering lists successfully\n");
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("ArbInitializeArbiterInstance: '%S'\n", ArbiterName);

    ASSERT(Arbiter->UnpackRequirement != NULL);
    ASSERT(Arbiter->PackResource != NULL);
    ASSERT(Arbiter->UnpackResource != NULL);
    ASSERT(Arbiter->MutexEvent == NULL);
    ASSERT(Arbiter->Allocation == NULL);
    ASSERT(Arbiter->PossibleAllocation == NULL);
    ASSERT(Arbiter->AllocationStack == NULL);

    Arbiter->Signature = ARBITER_SIGNATURE;
    Arbiter->BusDeviceObject = BusDeviceObject;

    Arbiter->MutexEvent = ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT), TAG_ARBITER);
    if (!Arbiter->MutexEvent)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(Arbiter->MutexEvent, SynchronizationEvent, TRUE);

    Arbiter->AllocationStack = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_ARB_ALLOCATION);
    if (!Arbiter->AllocationStack)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->AllocationStackMaxSize = PAGE_SIZE;

    Arbiter->Allocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->Allocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->PossibleAllocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->PossibleAllocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->Allocation, TAG_ARB_RANGE);
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitializeRangeList(Arbiter->Allocation);
    RtlInitializeRangeList(Arbiter->PossibleAllocation);

    Arbiter->Name = ArbiterName;
    Arbiter->ResourceType = ResourceType;
    Arbiter->TransactionInProgress = FALSE;

    if (!Arbiter->TestAllocation)
        Arbiter->TestAllocation = ArbTestAllocation;

    if (!Arbiter->RetestAllocation)
        Arbiter->RetestAllocation = ArbRetestAllocation;

    if (!Arbiter->CommitAllocation)
        Arbiter->CommitAllocation = ArbCommitAllocation;

    if (!Arbiter->RollbackAllocation)
        Arbiter->RollbackAllocation = ArbRollbackAllocation;

    if (!Arbiter->AddReserved)
        Arbiter->AddReserved = ArbAddReserved;

    if (!Arbiter->PreprocessEntry)
        Arbiter->PreprocessEntry = ArbPreprocessEntry;

    if (!Arbiter->AllocateEntry)
        Arbiter->AllocateEntry = ArbAllocateEntry;

    if (!Arbiter->GetNextAllocationRange)
        Arbiter->GetNextAllocationRange = ArbGetNextAllocationRange;

    if (!Arbiter->FindSuitableRange)
        Arbiter->FindSuitableRange = ArbFindSuitableRange;

    if (!Arbiter->AddAllocation)
        Arbiter->AddAllocation = ArbAddAllocation;

    if (!Arbiter->BacktrackAllocation)
        Arbiter->BacktrackAllocation = ArbBacktrackAllocation;

    if (!Arbiter->OverrideConflict)
        Arbiter->OverrideConflict = ArbOverrideConflict;

    if (!Arbiter->BootAllocation)
        Arbiter->BootAllocation = ArbBootAllocation;

    if (!Arbiter->QueryConflict)
        Arbiter->QueryConflict = ArbQueryConflict;

    if (!Arbiter->StartArbiter)
        Arbiter->StartArbiter = ArbStartArbiter;

    Status = ArbBuildAssignmentOrdering(Arbiter, OrderName, OrderName, TranslateOrderingFunction);
    if (NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

    DPRINT1("ArbInitializeArbiterInstance: Status %X\n", Status);

    return Status;
}
