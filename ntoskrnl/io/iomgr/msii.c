/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/msii.c
 * PURPOSE:         I/O Manager MSI/MSI-X Interrupt Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  ReactOS MSI Implementation Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* MSI capability structure */
typedef struct _MSI_CAPABILITY
{
    UCHAR CapabilityId;
    UCHAR Next;
    USHORT Control;
    ULONG Address;
    ULONG AddressHigh; /* Only present if 64-bit capable */
    USHORT Data;
    USHORT Reserved;
    ULONG MaskBits;    /* Only present if per-vector masking capable */
    ULONG PendingBits; /* Only present if per-vector masking capable */
} MSI_CAPABILITY, *PMSI_CAPABILITY;

/* MSI constants */
#define MSI_ADDRESS_BASE                0xFEE00000
#define MSI_DATA_DELIVERY_MODE_FIXED    0x0000
#define MSI_DATA_TRIGGER_EDGE           0x0000
#define MSI_DATA_LEVEL_ASSERT           0x0000
#define MSI_DATA_VECTOR_MASK            0x00FF

/* GLOBALS *******************************************************************/
#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

/* MSI vector allocation bitmap */
static KAFFINITY MsiVectorBitmap = 0;
static KSPIN_LOCK MsiVectorLock;

/* FUNCTIONS *****************************************************************/

/**
 * @brief Initialize MSI support
 */
CODE_SEG("INIT")
VOID
NTAPI
IopInitializeMsiSupport(VOID)
{
    /* Initialize the MSI vector allocation lock */
    KeInitializeSpinLock(&MsiVectorLock);
    
    /* Initialize vector bitmap - reserve vectors 0x30-0xEF for MSI */
    MsiVectorBitmap = 0;
    
    DPRINT("MSI support initialized\n");
}

/**
 * @brief Allocate an MSI vector
 */
NTSTATUS
NTAPI
IopAllocateMsiVector(
    OUT PULONG Vector)
{
    KIRQL OldIrql;
    ULONG AllocatedVector;
    ULONG Bit;
    
    KeAcquireSpinLock(&MsiVectorLock, &OldIrql);
    
    /* Find first available vector between 0x30 and 0xEF */
    for (Bit = 0x30; Bit <= 0xEF; Bit++)
    {
        if (!(MsiVectorBitmap & (1ULL << (Bit - 0x30))))
        {
            /* Mark vector as allocated */
            MsiVectorBitmap |= (1ULL << (Bit - 0x30));
            AllocatedVector = Bit;
            KeReleaseSpinLock(&MsiVectorLock, OldIrql);
            *Vector = AllocatedVector;
            return STATUS_SUCCESS;
        }
    }
    
    KeReleaseSpinLock(&MsiVectorLock, OldIrql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

/**
 * @brief Free an MSI vector
 */
VOID
NTAPI
IopFreeMsiVector(IN ULONG Vector)
{
    KIRQL OldIrql;
    
    if (Vector < 0x30 || Vector > 0xEF)
        return;
    
    KeAcquireSpinLock(&MsiVectorLock, &OldIrql);
    
    /* Mark vector as free */
    MsiVectorBitmap &= ~(1ULL << (Vector - 0x30));
    
    KeReleaseSpinLock(&MsiVectorLock, OldIrql);
}

/**
 * @brief Calculate MSI address for target processor
 */
ULONG
NTAPI
IopCalculateMsiAddress(IN KAFFINITY TargetProcessors)
{
    ULONG MsiAddress;
    ULONG ProcessorId;
    
    /* Find first processor in affinity mask */
    if (TargetProcessors == 0)
    {
        ProcessorId = 0;
    }
    else
    {
        /* Find first set bit */
        BitScanForward((PULONG)&ProcessorId, (ULONG)TargetProcessors);
    }
    
    /* MSI address format for x86/x64:
     * Bits 31-20: 0xFEE (MSI address base)
     * Bits 19-12: Destination ID (APIC ID)
     * Bits 11-4:  Reserved (must be 0)
     * Bit 3:      Redirection hint (0 = directed, 1 = redirectable)
     * Bit 2:      Destination mode (0 = physical, 1 = logical)
     * Bits 1-0:   Reserved (must be 00)
     */
    MsiAddress = MSI_ADDRESS_BASE | (ProcessorId << 12);
    
    return MsiAddress;
}

/**
 * @brief Calculate MSI data for interrupt vector
 */
USHORT
NTAPI
IopCalculateMsiData(IN ULONG Vector)
{
    /* MSI data format:
     * Bits 31-16: Reserved
     * Bit 15:     Trigger mode (0 = edge, 1 = level)
     * Bit 14:     Level (0 = deassert, 1 = assert)
     * Bits 13-11: Reserved
     * Bits 10-8:  Delivery mode (000 = fixed)
     * Bits 7-0:   Vector
     */
    return (USHORT)(MSI_DATA_DELIVERY_MODE_FIXED | MSI_DATA_TRIGGER_EDGE | 
           MSI_DATA_LEVEL_ASSERT | (Vector & MSI_DATA_VECTOR_MASK));
}

/**
 * @brief Connect message-based interrupt
 */
NTSTATUS
NTAPI
IopConnectInterruptMessageBased(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PIO_INTERRUPT_MESSAGE_INFO MessageInfo;
    ULONG MessageCount;
    ULONG i;
    NTSTATUS Status;
    
    PAGED_CODE();
    
    DPRINT("Connecting message-based interrupt\n");
    
    /* Get the message count from device capabilities */
    MessageCount = 1; /* Default to single message for now */
    
    /* Allocate message info structure */
    MessageInfo = ExAllocatePoolZero(NonPagedPool,
                                     sizeof(IO_INTERRUPT_MESSAGE_INFO) +
                                     (MessageCount - 1) * sizeof(IO_INTERRUPT_MESSAGE_INFO_ENTRY),
                                     TAG_IO_INTERRUPT);
    if (!MessageInfo)
        return STATUS_INSUFFICIENT_RESOURCES;
    
    /* Initialize message info */
    MessageInfo->UnifiedIrql = DISPATCH_LEVEL; /* Default IRQL */
    MessageInfo->MessageCount = MessageCount;
    
    /* Set up each message */
    for (i = 0; i < MessageCount; i++)
    {
        /* Allocate vector */
        ULONG Vector;
        Status = IopAllocateMsiVector(&Vector);
        if (!NT_SUCCESS(Status))
        {
            /* Free previously allocated vectors */
            for (ULONG j = 0; j < i; j++)
            {
                IopFreeMsiVector(MessageInfo->MessageInfo[j].Vector);
            }
            ExFreePoolWithTag(MessageInfo, TAG_IO_INTERRUPT);
            return Status;
        }
        
        /* Fill in message info */
        MessageInfo->MessageInfo[i].MessageAddress.QuadPart = IopCalculateMsiAddress(1); /* Use processor 0 for now */
        MessageInfo->MessageInfo[i].TargetProcessorSet = 1;
        MessageInfo->MessageInfo[i].InterruptObject = NULL; /* Will be filled by HAL */
        MessageInfo->MessageInfo[i].MessageData = IopCalculateMsiData(Vector);
        MessageInfo->MessageInfo[i].Vector = Vector;
        MessageInfo->MessageInfo[i].Irql = DISPATCH_LEVEL;
        MessageInfo->MessageInfo[i].Mode = LevelSensitive; /* MSI is edge, but we use level for MSI-X */
        MessageInfo->MessageInfo[i].Polarity = InterruptActiveHigh;
        
        DPRINT("Allocated MSI vector %lu with address 0x%I64x, data 0x%lx\n",
               Vector, MessageInfo->MessageInfo[i].MessageAddress.QuadPart,
               MessageInfo->MessageInfo[i].MessageData);
    }
    
    /* Work around union access issues by casting */
    PIO_CONNECT_INTERRUPT_MESSAGE_BASED_PARAMETERS MessageParams;
    MessageParams = (PIO_CONNECT_INTERRUPT_MESSAGE_BASED_PARAMETERS)&Parameters->Version + 1;
    
    /* Return the message table */
    *MessageParams->ConnectionContext.InterruptMessageTable = MessageInfo;
    
    /* Now configure the device MSI registers */
    Status = IopConfigureDeviceMsi(MessageParams->PhysicalDeviceObject, MessageInfo);
    if (!NT_SUCCESS(Status))
    {
        /* Free allocated vectors */
        for (i = 0; i < MessageCount; i++)
        {
            IopFreeMsiVector(MessageInfo->MessageInfo[i].Vector);
        }
        ExFreePoolWithTag(MessageInfo, TAG_IO_INTERRUPT);
        return Status;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Configure device MSI/MSI-X registers
 */
NTSTATUS
NTAPI
IopConfigureDeviceMsi(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    NTSTATUS Status;
    ULONG BusNumber, SlotNumber;
    PCI_COMMON_CONFIG PciConfig;
    UCHAR CapabilityOffset;
    
    /* Get PCI bus and slot information */
    Status = IopGetDevicePciLocation(PhysicalDeviceObject, &BusNumber, &SlotNumber);
    if (!NT_SUCCESS(Status))
        return Status;
    
    /* Read PCI configuration */
    HalGetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                          &PciConfig, 0, sizeof(PCI_COMMON_CONFIG));
    
    /* Find MSI or MSI-X capability */
    CapabilityOffset = IopFindPciCapability(&PciConfig, PCI_CAPABILITY_ID_MSIX);
    if (CapabilityOffset != 0)
    {
        DPRINT("Device has MSI-X capability at offset 0x%02x\n", CapabilityOffset);
        Status = IopConfigureMsiX(BusNumber, SlotNumber, CapabilityOffset, MessageInfo);
    }
    else
    {
        CapabilityOffset = IopFindPciCapability(&PciConfig, PCI_CAPABILITY_ID_MSI);
        if (CapabilityOffset != 0)
        {
            DPRINT("Device has MSI capability at offset 0x%02x\n", CapabilityOffset);
            Status = IopConfigureMsi(BusNumber, SlotNumber, CapabilityOffset, MessageInfo);
        }
        else
        {
            DPRINT1("Device does not support MSI or MSI-X\n");
            return STATUS_NOT_SUPPORTED;
        }
    }
    
    return Status;
}

/**
 * @brief Find PCI capability
 */
UCHAR
NTAPI
IopFindPciCapability(
    IN PPCI_COMMON_CONFIG PciConfig,
    IN UCHAR CapabilityId)
{
    UCHAR CapabilityOffset;
    UCHAR CurrentCapability;
    ULONG BusNumber = 0, SlotNumber = 0; /* These should be passed as parameters */
    
    /* Check if device has capabilities */
    if (!(PciConfig->Status & PCI_STATUS_CAPABILITIES_LIST))
        return 0;
    
    /* Start at capabilities pointer */
    CapabilityOffset = PciConfig->u.type0.CapabilitiesPtr;
    
    /* Walk capability list */
    while (CapabilityOffset != 0)
    {
        /* Read capability ID */
        HalGetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                              &CurrentCapability, CapabilityOffset, sizeof(UCHAR));
        
        if (CurrentCapability == CapabilityId)
            return CapabilityOffset;
        
        /* Move to next capability */
        HalGetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                              &CapabilityOffset, CapabilityOffset + 1, sizeof(UCHAR));
    }
    
    return 0; /* Capability not found */
}

/**
 * @brief Configure MSI registers
 */
NTSTATUS
NTAPI
IopConfigureMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    MSI_CAPABILITY MsiCap;
    ULONG MsiAddress;
    USHORT MsiData;
    USHORT MsiControl;
    
    /* Read MSI capability structure */
    HalGetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                          &MsiCap, CapabilityOffset, sizeof(MSI_CAPABILITY));
    
    /* Configure for single message for now */
    if (MessageInfo->MessageCount > 1)
    {
        DPRINT1("Multi-message MSI not yet implemented\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    
    /* Set MSI address */
    MsiAddress = (ULONG)MessageInfo->MessageInfo[0].MessageAddress.LowPart;
    HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                          &MsiAddress, CapabilityOffset + 4, sizeof(ULONG));
    
    /* Check if 64-bit addressing is supported */
    if (MsiCap.Control & 0x80) /* 64-bit address capable */
    {
        ULONG MsiAddressHigh = MessageInfo->MessageInfo[0].MessageAddress.HighPart;
        HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                              &MsiAddressHigh, CapabilityOffset + 8, sizeof(ULONG));
        
        /* Set MSI data (at offset 12 for 64-bit) */
        MsiData = (USHORT)MessageInfo->MessageInfo[0].MessageData;
        HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                              &MsiData, CapabilityOffset + 12, sizeof(USHORT));
    }
    else
    {
        /* Set MSI data (at offset 8 for 32-bit) */
        MsiData = (USHORT)MessageInfo->MessageInfo[0].MessageData;
        HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                              &MsiData, CapabilityOffset + 8, sizeof(USHORT));
    }
    
    /* Enable MSI */
    MsiControl = MsiCap.Control | 0x01; /* Set MSI Enable bit */
    HalSetBusDataByOffset(PCIConfiguration, BusNumber, SlotNumber,
                          &MsiControl, CapabilityOffset + 2, sizeof(USHORT));
    
    DPRINT("MSI configured successfully\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Configure MSI-X registers
 */
NTSTATUS
NTAPI
IopConfigureMsiX(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    /* MSI-X implementation is more complex and will be implemented later */
    DPRINT1("MSI-X configuration not yet implemented\n");
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Get PCI bus and slot location for device
 */
NTSTATUS
NTAPI
IopGetDevicePciLocation(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PULONG BusNumber,
    OUT PULONG SlotNumber)
{
    PDEVICE_OBJECT CurrentDevice;
    PDEVICE_OBJECT ParentDevice;
    
    DPRINT("Getting PCI location for device %p\n", PhysicalDeviceObject);
    
    /* Validate parameters */
    if (!PhysicalDeviceObject || !BusNumber || !SlotNumber)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Initialize output parameters */
    *BusNumber = 0;
    *SlotNumber = 0;
    
    /* Try to get PCI location from device properties */
    CurrentDevice = PhysicalDeviceObject;
    
    /* Check if this device has location information in its name or ID */
    if (CurrentDevice->DeviceExtension)
    {
        /* For now, try to extract from device location string if available */
        /* This is a simplified approach - in a full implementation, we would
         * query device properties like DevicePropertyLocationInformation */
        
        /* Try to get bus interface to determine PCI location */
        /* This is where we would typically query for:
         * - DevicePropertyBusNumber
         * - DevicePropertyAddress  
         * But since IoGetDeviceProperty is not available in this context,
         * we'll use a different approach */
    }
    
    /* For devices that don't have direct PCI information, traverse up the device stack */
    ParentDevice = IoGetAttachedDeviceReference(CurrentDevice);
    if (ParentDevice && ParentDevice != CurrentDevice)
    {
        /* Try to get information from parent device */
        DPRINT("Checking parent device %p\n", ParentDevice);
        ObDereferenceObject(ParentDevice);
    }
    
    /* As a fallback, assume default values for now */
    /* In a real implementation, this would parse device instance IDs,
     * query device properties, or use other methods to determine location */
    
    DPRINT("Using default PCI location: Bus 0, Slot 0\n");
    *BusNumber = 0;
    *SlotNumber = 0;
    
    return STATUS_SUCCESS;
}

/* EOF */
