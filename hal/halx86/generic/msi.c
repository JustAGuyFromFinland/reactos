/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/generic/msi.c
 * PURPOSE:         MSI/MSI-X Interrupt Support
 * PROGRAMMERS:     ReactOS MSI Implementation Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* MSI interrupt tracking */
typedef struct _MSI_INTERRUPT_ENTRY
{
    LIST_ENTRY ListEntry;
    ULONG Vector;
    ULONG BusNumber;
    ULONG SlotNumber;
    PKINTERRUPT InterruptObject;
    PKSERVICE_ROUTINE ServiceRoutine;
    PVOID ServiceContext;
    BOOLEAN Connected;
} MSI_INTERRUPT_ENTRY, *PMSI_INTERRUPT_ENTRY;

static LIST_ENTRY MsiInterruptList;
static KSPIN_LOCK MsiInterruptLock;
static BOOLEAN MsiInitialized = FALSE;

/* FUNCTIONS *****************************************************************/

/**
 * @brief Initialize MSI support in HAL
 */
CODE_SEG("INIT")
VOID
NTAPI
HalpInitializeMsiSupport(VOID)
{
    /* Initialize MSI interrupt tracking */
    InitializeListHead(&MsiInterruptList);
    KeInitializeSpinLock(&MsiInterruptLock);
    
    MsiInitialized = TRUE;
    
    DPRINT("HAL MSI support initialized\n");
}

/**
 * @brief MSI interrupt service routine wrapper
 */
BOOLEAN
NTAPI
HalpMsiInterruptRoutine(
    IN PKINTERRUPT Interrupt,
    IN PVOID ServiceContext)
{
    PMSI_INTERRUPT_ENTRY MsiEntry = (PMSI_INTERRUPT_ENTRY)ServiceContext;
    
    /* Call the actual service routine */
    if (MsiEntry && MsiEntry->ServiceRoutine)
    {
        return MsiEntry->ServiceRoutine(Interrupt, MsiEntry->ServiceContext);
    }
    
    return FALSE;
}

/**
 * @brief Connect an MSI interrupt
 */
NTSTATUS
NTAPI
HalpConnectMsiInterrupt(
    IN ULONG Vector,
    IN PKSERVICE_ROUTINE ServiceRoutine,
    IN PVOID ServiceContext,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    OUT PKINTERRUPT *InterruptObject)
{
    PMSI_INTERRUPT_ENTRY MsiEntry;
    NTSTATUS Status;
    KIRQL OldIrql;
    
    if (!MsiInitialized)
        return STATUS_NOT_SUPPORTED;
    
    /* Allocate MSI entry */
    MsiEntry = ExAllocatePoolZero(NonPagedPool, sizeof(MSI_INTERRUPT_ENTRY), 'ISMh');
    if (!MsiEntry)
        return STATUS_INSUFFICIENT_RESOURCES;
    
    /* Initialize MSI entry */
    MsiEntry->Vector = Vector;
    MsiEntry->BusNumber = BusNumber;
    MsiEntry->SlotNumber = SlotNumber;
    MsiEntry->ServiceRoutine = ServiceRoutine;
    MsiEntry->ServiceContext = ServiceContext;
    MsiEntry->Connected = FALSE;
    
    /* Connect the interrupt using standard HAL functions */
    Status = IoConnectInterrupt(&MsiEntry->InterruptObject,
                                HalpMsiInterruptRoutine,
                                MsiEntry,
                                NULL,
                                Vector,
                                DISPATCH_LEVEL, /* IRQL */
                                DISPATCH_LEVEL, /* SynchronizeIrql */
                                LevelSensitive,
                                FALSE, /* Not shared */
                                HalpActiveProcessors,
                                FALSE); /* No floating point save */
    
    if (NT_SUCCESS(Status))
    {
        MsiEntry->Connected = TRUE;
        *InterruptObject = MsiEntry->InterruptObject;
        
        /* Add to tracking list */
        KeAcquireSpinLock(&MsiInterruptLock, &OldIrql);
        InsertTailList(&MsiInterruptList, &MsiEntry->ListEntry);
        KeReleaseSpinLock(&MsiInterruptLock, OldIrql);
        
        DPRINT("Connected MSI interrupt vector %lu\n", Vector);
    }
    else
    {
        ExFreePoolWithTag(MsiEntry, 'ISMh');
        DPRINT1("Failed to connect MSI interrupt vector %lu: 0x%08lx\n", Vector, Status);
    }
    
    return Status;
}

/**
 * @brief Disconnect an MSI interrupt
 */
VOID
NTAPI
HalpDisconnectMsiInterrupt(
    IN PKINTERRUPT InterruptObject)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PMSI_INTERRUPT_ENTRY MsiEntry = NULL;
    
    if (!MsiInitialized)
        return;
    
    /* Find the MSI entry */
    KeAcquireSpinLock(&MsiInterruptLock, &OldIrql);
    
    for (Entry = MsiInterruptList.Flink; Entry != &MsiInterruptList; Entry = Entry->Flink)
    {
        PMSI_INTERRUPT_ENTRY CurrentEntry = CONTAINING_RECORD(Entry, MSI_INTERRUPT_ENTRY, ListEntry);
        
        if (CurrentEntry->InterruptObject == InterruptObject)
        {
            MsiEntry = CurrentEntry;
            RemoveEntryList(&CurrentEntry->ListEntry);
            break;
        }
    }
    
    KeReleaseSpinLock(&MsiInterruptLock, OldIrql);
    
    if (MsiEntry)
    {
        /* Disconnect the interrupt */
        if (MsiEntry->Connected)
        {
            IoDisconnectInterrupt(MsiEntry->InterruptObject);
        }
        
        DPRINT("Disconnected MSI interrupt vector %lu\n", MsiEntry->Vector);
        
        /* Free the entry */
        ExFreePoolWithTag(MsiEntry, 'ISMh');
    }
}

/**
 * @brief Enable MSI/MSI-X for a PCI device
 */
NTSTATUS
NTAPI
HalpEnablePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN ULONG MessageCount,
    OUT PULONG Vectors)
{
    PCI_COMMON_CONFIG PciConfig;
    UCHAR MsiCapability, MsiXCapability;
    ULONG BytesRead;
    NTSTATUS Status;
    
    /* Read PCI configuration */
    BytesRead = HalGetBusDataByOffset(PCIConfiguration,
                                      BusNumber,
                                      SlotNumber,
                                      &PciConfig,
                                      0,
                                      sizeof(PCI_COMMON_CONFIG));
    
    if (BytesRead != sizeof(PCI_COMMON_CONFIG))
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    /* Check for MSI-X capability first (preferred) */
    MsiXCapability = HalpFindPciCapability(&PciConfig, BusNumber, SlotNumber, PCI_CAPABILITY_ID_MSIX);
    if (MsiXCapability != 0)
    {
        DPRINT("Device supports MSI-X\n");
        Status = HalpConfigurePciMsiX(BusNumber, SlotNumber, MsiXCapability, MessageCount, Vectors);
    }
    else
    {
        /* Check for MSI capability */
        MsiCapability = HalpFindPciCapability(&PciConfig, BusNumber, SlotNumber, PCI_CAPABILITY_ID_MSI);
        if (MsiCapability != 0)
        {
            DPRINT("Device supports MSI\n");
            Status = HalpConfigurePciMsi(BusNumber, SlotNumber, MsiCapability, MessageCount, Vectors);
        }
        else
        {
            DPRINT1("Device does not support MSI or MSI-X\n");
            Status = STATUS_NOT_SUPPORTED;
        }
    }
    
    return Status;
}

/**
 * @brief Find a PCI capability
 */
UCHAR
NTAPI
HalpFindPciCapability(
    IN PPCI_COMMON_CONFIG PciConfig,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityId)
{
    UCHAR CapabilityOffset;
    UCHAR CurrentCapability;
    ULONG LoopCount = 0;
    
    /* Check if device supports capabilities */
    if (!(PciConfig->Status & PCI_STATUS_CAPABILITIES_LIST))
    {
        return 0;
    }
    
    /* Get capabilities pointer */
    CapabilityOffset = PciConfig->u.type0.CapabilitiesPtr;
    
    /* Walk the capabilities list */
    while (CapabilityOffset != 0 && LoopCount < 48) /* Prevent infinite loops */
    {
        /* Read capability header */
        HalGetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &CurrentCapability,
                              CapabilityOffset,
                              sizeof(UCHAR));
        
        if (CurrentCapability == CapabilityId)
        {
            return CapabilityOffset;
        }
        
        /* Get next capability offset */
        HalGetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &CapabilityOffset,
                              CapabilityOffset + 1,
                              sizeof(UCHAR));
        
        LoopCount++;
    }
    
    return 0; /* Capability not found */
}

/**
 * @brief Configure PCI MSI
 */
NTSTATUS
NTAPI
HalpConfigurePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN ULONG MessageCount,
    OUT PULONG Vectors)
{
    USHORT MsiControl;
    ULONG MsiAddress;
    USHORT MsiData;
    ULONG Vector;
    BOOLEAN Is64Bit;
    BOOLEAN UseCompatibilityMode = FALSE;
    
    /* Check if we should use compatibility mode for VirtualBox ICH9 */
    /* Read MSI control register */
    HalGetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Check if 64-bit addressing is supported */
    Is64Bit = (MsiControl & 0x0080) != 0;
    
    /* For compatibility with VirtualBox ICH9, limit to single MSI message */
    if (MessageCount > 1)
    {
        DPRINT("Multi-message MSI requested but using compatibility mode - limiting to 1\n");
        MessageCount = 1;
        UseCompatibilityMode = TRUE;
    }
    
    /* Check device's multi-message capability */
    UCHAR MaxMessages = 1 << ((MsiControl & 0x000E) >> 1); /* Bits 3:1 */
    if (MessageCount > MaxMessages)
    {
        DPRINT("Device only supports %u messages, reducing from %lu\n", MaxMessages, MessageCount);
        MessageCount = MaxMessages;
        UseCompatibilityMode = TRUE;
    }
    
    /* Allocate vector */
    Vector = HalpAllocateVector();
    if (Vector == 0)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Calculate MSI address (points to local APIC) */
    /* Use simple, compatible addressing for VirtualBox */
    MsiAddress = 0xFEE00000; /* Base MSI address for local APIC */
    if (UseCompatibilityMode)
    {
        /* Add minimal processor targeting for better compatibility */
        MsiAddress |= (0 << 12); /* Target processor 0 */
    }
    
    /* Calculate MSI data */
    MsiData = (USHORT)Vector; /* Simple vector assignment */
    if (UseCompatibilityMode)
    {
        /* Use edge-triggered delivery for better compatibility */
        MsiData |= 0x4000; /* Edge triggered bit */
    }
    
    DPRINT("MSI Config: Vector=%lu, Address=0x%08lx, Data=0x%04x, Compat=%s\n",
           Vector, MsiAddress, MsiData, UseCompatibilityMode ? "Yes" : "No");
    
    /* Write MSI address (32-bit) */
    HalSetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &MsiAddress,
                          CapabilityOffset + 4,
                          sizeof(ULONG));
    
    if (Is64Bit)
    {
        /* Write upper 32 bits of address */
        ULONG MsiAddressUpper = 0;
        HalSetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &MsiAddressUpper,
                              CapabilityOffset + 8,
                              sizeof(ULONG));
        
        /* Write MSI data */
        HalSetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &MsiData,
                              CapabilityOffset + 12,
                              sizeof(USHORT));
    }
    else
    {
        /* Write MSI data */
        HalSetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &MsiData,
                              CapabilityOffset + 8,
                              sizeof(USHORT));
    }
    
    /* Configure multiple message enable if needed */
    if (MessageCount > 1 && !UseCompatibilityMode)
    {
        /* Calculate log2 of message count */
        UCHAR Log2Messages = 0;
        ULONG TempCount = MessageCount;
        while (TempCount > 1)
        {
            TempCount >>= 1;
            Log2Messages++;
        }
        
        /* Set Multiple Message Enable field (bits 6:4) */
        MsiControl &= ~0x0070;
        MsiControl |= (Log2Messages << 4) & 0x0070;
    }
    
    /* Enable MSI */
    MsiControl |= 0x0001; /* Set MSI Enable bit */
    HalSetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    Vectors[0] = Vector;
    
    DPRINT("MSI configured successfully: Vector=%lu, Address=0x%08lx, Data=0x%04x\n",
           Vector, MsiAddress, MsiData);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Configure PCI MSI-X
 */
NTSTATUS
NTAPI
HalpConfigurePciMsiX(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN ULONG MessageCount,
    OUT PULONG Vectors)
{
    USHORT MessageControl;
    USHORT TableSize;
    ULONG TableOffset;
    ULONG TableBar;
    ULONG PbaOffset;
    ULONG PbaBar;
    PHYSICAL_ADDRESS TablePhysical;
    PVOID TableVirtual = NULL;
    ULONG i;
    BOOLEAN UseCompatibilityMode = FALSE;
    
    DPRINT("Configuring MSI-X for device (Bus=%lu, Slot=%lu, Messages=%lu)\n",
           BusNumber, SlotNumber, MessageCount);
    
    /* Read MSI-X Message Control */
    HalGetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Calculate table size (Table Size field + 1) */
    TableSize = (MessageControl & 0x7FF) + 1;
    
    DPRINT("MSI-X Table Size: %u entries\n", TableSize);
    
    /* For VirtualBox ICH9 compatibility, limit to conservative message count */
    if (MessageCount > 4)
    {
        DPRINT("Limiting MSI-X messages from %lu to 4 for compatibility\n", MessageCount);
        MessageCount = 4;
        UseCompatibilityMode = TRUE;
    }
    
    DPRINT("MSI-X compatibility mode: %s\n", UseCompatibilityMode ? "Yes" : "No");
    
    /* Check if we have enough table entries */
    if (MessageCount > TableSize)
    {
        DPRINT1("Requested %lu messages but only %u table entries available\n", 
                MessageCount, TableSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Read Table BIR and Offset */
    HalGetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &TableOffset,
                          CapabilityOffset + 4,
                          sizeof(ULONG));
    TableBar = TableOffset & 0x7;  /* Lower 3 bits = BAR index */
    TableOffset &= ~0x7;           /* Upper bits = offset within BAR */
    
    /* Read PBA BIR and Offset */
    HalGetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &PbaOffset,
                          CapabilityOffset + 8,
                          sizeof(ULONG));
    PbaBar = PbaOffset & 0x7;      /* Lower 3 bits = BAR index */
    PbaOffset &= ~0x7;             /* Upper bits = offset within BAR */
    
    DPRINT("MSI-X Table: BAR %lu, Offset 0x%lx\n", TableBar, TableOffset);
    DPRINT("MSI-X PBA: BAR %lu, Offset 0x%lx\n", PbaBar, PbaOffset);
    
    /* Get BAR address for table */
    if (TableBar < 6)  /* Valid BAR index */
    {
        ULONG BarValue;
        HalGetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &BarValue,
                              0x10 + (TableBar * 4),
                              sizeof(ULONG));
        
        if (BarValue & 0x1)
        {
            /* I/O BAR - not supported for MSI-X */
            DPRINT1("MSI-X Table in I/O BAR not supported\n");
            return STATUS_NOT_SUPPORTED;
        }
        
        TablePhysical.QuadPart = (BarValue & ~0xF) + TableOffset;
        
        /* Map the MSI-X table for configuration */
        TableVirtual = MmMapIoSpace(TablePhysical, 
                                   TableSize * 16,  /* Each entry is 16 bytes */
                                   MmNonCached);
        if (!TableVirtual)
        {
            DPRINT1("Failed to map MSI-X table\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        DPRINT1("Invalid MSI-X Table BAR index: %lu\n", TableBar);
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Initialize MSI-X table entries */
    if (TableVirtual)
    {
        PULONG TableEntry = (PULONG)TableVirtual;
        
        /* Initialize requested number of entries */
        for (i = 0; i < MessageCount; i++)
        {
            ULONG Vector = HalpAllocateVector();
            if (Vector == 0)
            {
                /* Clean up and return error */
                MmUnmapIoSpace(TableVirtual, TableSize * 16);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            
            /* Each MSI-X table entry is 16 bytes:
             * Offset 0: Message Address (lower 32 bits)
             * Offset 4: Message Upper Address (upper 32 bits)
             * Offset 8: Message Data
             * Offset 12: Vector Control
             */
            
            /* Message Address - point to local APIC */
            TableEntry[i * 4 + 0] = 0xFEE00000;
            
            /* Message Upper Address */
            TableEntry[i * 4 + 1] = 0;
            
            /* Message Data - contains the vector */
            TableEntry[i * 4 + 2] = Vector;
            
            /* Vector Control - initially masked */
            TableEntry[i * 4 + 3] = 1;  /* Mask bit set */
            
            Vectors[i] = Vector;
            
            DPRINT("MSI-X entry %lu: Vector=%lu, Address=0x%08lx\n", 
                   i, Vector, 0xFEE00000);
        }
        
        /* Mask remaining entries */
        for (i = MessageCount; i < TableSize; i++)
        {
            TableEntry[i * 4 + 0] = 0;
            TableEntry[i * 4 + 1] = 0;
            TableEntry[i * 4 + 2] = 0;
            TableEntry[i * 4 + 3] = 1;  /* Mask bit set */
        }
        
        DPRINT("Initialized %lu MSI-X table entries\n", MessageCount);
    }
    
    /* Enable MSI-X but keep function masked initially */
    MessageControl |= 0x8000;  /* MSI-X Enable */
    MessageControl |= 0x4000;  /* Function Mask */
    
    HalSetBusDataByOffset(PCIConfiguration,
                          BusNumber,
                          SlotNumber,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    DPRINT("MSI-X enabled with %lu vectors\n", MessageCount);
    
    /* Clean up mapping */
    if (TableVirtual)
    {
        MmUnmapIoSpace(TableVirtual, TableSize * 16);
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Allocate an interrupt vector
 */
ULONG
NTAPI
HalpAllocateVector(VOID)
{
    static ULONG NextVector = 0x30; /* Start from vector 0x30 */
    
    /* Simple allocation scheme - should be improved */
    if (NextVector <= 0xFE)
    {
        return NextVector++;
    }
    
    return 0; /* No vectors available */
}

/**
 * @brief Disable MSI/MSI-X for a PCI device
 */
VOID
NTAPI
HalpDisablePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber)
{
    PCI_COMMON_CONFIG PciConfig;
    UCHAR MsiCapability, MsiXCapability;
    USHORT Control;
    ULONG BytesRead;
    
    /* Read PCI configuration */
    BytesRead = HalGetBusDataByOffset(PCIConfiguration,
                                      BusNumber,
                                      SlotNumber,
                                      &PciConfig,
                                      0,
                                      sizeof(PCI_COMMON_CONFIG));
    
    if (BytesRead != sizeof(PCI_COMMON_CONFIG))
    {
        return;
    }
    
    /* Check for MSI-X capability first */
    MsiXCapability = HalpFindPciCapability(&PciConfig, BusNumber, SlotNumber, PCI_CAPABILITY_ID_MSIX);
    if (MsiXCapability != 0)
    {
        /* Disable MSI-X */
        HalGetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &Control,
                              MsiXCapability + 2,
                              sizeof(USHORT));
        
        Control &= ~0x8000; /* Clear MSI-X Enable bit */
        
        HalSetBusDataByOffset(PCIConfiguration,
                              BusNumber,
                              SlotNumber,
                              &Control,
                              MsiXCapability + 2,
                              sizeof(USHORT));
        
        DPRINT("MSI-X disabled\n");
    }
    else
    {
        /* Check for MSI capability */
        MsiCapability = HalpFindPciCapability(&PciConfig, BusNumber, SlotNumber, PCI_CAPABILITY_ID_MSI);
        if (MsiCapability != 0)
        {
            /* Disable MSI */
            HalGetBusDataByOffset(PCIConfiguration,
                                  BusNumber,
                                  SlotNumber,
                                  &Control,
                                  MsiCapability + 2,
                                  sizeof(USHORT));
            
            Control &= ~0x0001; /* Clear MSI Enable bit */
            
            HalSetBusDataByOffset(PCIConfiguration,
                                  BusNumber,
                                  SlotNumber,
                                  &Control,
                                  MsiCapability + 2,
                                  sizeof(USHORT));
            
            DPRINT("MSI disabled\n");
        }
    }
}

/* EOF */
