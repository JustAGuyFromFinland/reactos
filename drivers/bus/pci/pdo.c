/*
 * PROJECT:         ReactOS PCI bus driver
 * FILE:            pdo.c
 * PURPOSE:         Child device object dispatch routines
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      10-09-2001  CSH  Created
 */

#include "pci.h"

#include <initguid.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

#if 0
#define DBGPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define DBGPRINT(...)
#endif

//
// Forward declarations for PCIe support functions
//
static VOID
PdoConfigurePciExpressSupport(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION DeviceExtension);

static VOID
PdoDetectAdvancedPciExpressCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION DeviceExtension);

static VOID
PdoConfigurePowerManagement(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN UCHAR CapabilityOffset);

static VOID
PdoDetectPciExpressExtendedCapabilities(
    IN PPDO_DEVICE_EXTENSION DeviceExtension);

static VOID
PdoConfigureAdvancedErrorReporting(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN ULONG CapabilityOffset);

#define PCI_ADDRESS_MEMORY_ADDRESS_MASK_64     0xfffffffffffffff0ull
#define PCI_ADDRESS_IO_ADDRESS_MASK_64         0xfffffffffffffffcull

/*** PRIVATE *****************************************************************/

static NTSTATUS
PdoQueryDeviceText(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING String;
    NTSTATUS Status;

    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    switch (IrpSp->Parameters.QueryDeviceText.DeviceTextType)
    {
        case DeviceTextDescription:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceDescription,
                                               &String);

            DPRINT("DeviceTextDescription\n");
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case DeviceTextLocationInformation:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceLocation,
                                               &String);

            DPRINT("DeviceTextLocationInformation\n");
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        default:
            Irp->IoStatus.Information = 0;
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    return Status;
}


static NTSTATUS
PdoQueryId(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING String;
    NTSTATUS Status;

    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

//    Irp->IoStatus.Information = 0;

    Status = STATUS_SUCCESS;

    RtlInitUnicodeString(&String, NULL);

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceID,
                                               &String);

            DPRINT("DeviceID: %S\n", String.Buffer);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryHardwareIDs:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->HardwareIDs,
                                               &String);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryCompatibleIDs:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->CompatibleIDs,
                                               &String);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryInstanceID:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->InstanceID,
                                               &String);

            DPRINT("InstanceID: %S\n", String.Buffer);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryDeviceSerialNumber:
        default:
            Status = STATUS_NOT_IMPLEMENTED;
    }

    return Status;
}


static NTSTATUS
PdoQueryBusInformation(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PPNP_BUS_INFORMATION BusInformation;

    UNREFERENCED_PARAMETER(IrpSp);
    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    BusInformation = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), TAG_PCI);
    Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
    if (BusInformation != NULL)
    {
        BusInformation->BusTypeGuid = GUID_BUS_TYPE_PCI;
        BusInformation->LegacyBusType = PCIBus;
        BusInformation->BusNumber = DeviceExtension->PciDevice->BusNumber;

        return STATUS_SUCCESS;
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}


static NTSTATUS
PdoQueryCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_CAPABILITIES DeviceCapabilities;
    ULONG DeviceNumber, FunctionNumber;

    UNREFERENCED_PARAMETER(Irp);
    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;

    if (DeviceCapabilities->Version != 1)
        return STATUS_UNSUCCESSFUL;

    DeviceNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber;
    FunctionNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber;

    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->Address = ((DeviceNumber << 16) & 0xFFFF0000) + (FunctionNumber & 0xFFFF);
    DeviceCapabilities->UINumber = MAXULONG; /* FIXME */

    return STATUS_SUCCESS;
}

static BOOLEAN
PdoReadPciBar(PPDO_DEVICE_EXTENSION DeviceExtension,
              ULONG Offset,
              PULONG OriginalValue,
              PULONG NewValue)
{
    ULONG Size;
    ULONG AllOnes;

    /* Read the original value */
    Size = HalGetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 OriginalValue,
                                 Offset,
                                 sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu\n", Size);
        return FALSE;
    }

    /* Write all ones to determine which bits are held to zero */
    AllOnes = MAXULONG;
    Size = HalSetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 &AllOnes,
                                 Offset,
                                 sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu\n", Size);
        return FALSE;
    }

    /* Get the range length */
    Size = HalGetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 NewValue,
                                 Offset,
                                 sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu\n", Size);
        return FALSE;
    }

    /* Restore original value */
    Size = HalSetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 OriginalValue,
                                 Offset,
                                 sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu\n", Size);
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
PdoGetRangeLength(PPDO_DEVICE_EXTENSION DeviceExtension,
                  UCHAR Bar,
                  PULONGLONG Base,
                  PULONGLONG Length,
                  PULONG Flags,
                  PUCHAR NextBar,
                  PULONGLONG MaximumAddress)
{
    union {
        struct {
            ULONG Bar0;
            ULONG Bar1;
        } Bars;
        ULONGLONG Bar;
    } OriginalValue;
    union {
        struct {
            ULONG Bar0;
            ULONG Bar1;
        } Bars;
        ULONGLONG Bar;
    } NewValue;
    ULONG Offset;
    ULONGLONG Size;

    /* Compute the offset of this BAR in PCI config space */
    Offset = 0x10 + Bar * 4;

    /* Assume this is a 32-bit BAR until we find wrong */
    *NextBar = Bar + 1;

    /* Initialize BAR values to zero */
    OriginalValue.Bar = 0ULL;
    NewValue.Bar = 0ULL;

    /* Read the first BAR */
    if (!PdoReadPciBar(DeviceExtension, Offset,
                       &OriginalValue.Bars.Bar0,
                       &NewValue.Bars.Bar0))
    {
        return FALSE;
    }

    /* Check if this is a memory BAR */
    if (!(OriginalValue.Bars.Bar0 & PCI_ADDRESS_IO_SPACE))
    {
        /* Write the maximum address if the caller asked for it */
        if (MaximumAddress != NULL)
        {
            if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_32BIT)
            {
                *MaximumAddress = 0x00000000FFFFFFFFULL;
            }
            else if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_20BIT)
            {
                *MaximumAddress = 0x00000000000FFFFFULL;
            }
            else if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
            {
                *MaximumAddress = 0xFFFFFFFFFFFFFFFFULL;
            }
        }

        /* Check if this is a 64-bit BAR */
        if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
        {
            /* We've now consumed the next BAR too */
            *NextBar = Bar + 2;

            /* Read the next BAR */
            if (!PdoReadPciBar(DeviceExtension, Offset + 4,
                               &OriginalValue.Bars.Bar1,
                               &NewValue.Bars.Bar1))
            {
                return FALSE;
            }
        }
    }
    else
    {
        /* Write the maximum I/O port address */
        if (MaximumAddress != NULL)
        {
            *MaximumAddress = 0x00000000FFFFFFFFULL;
        }
    }

    if (NewValue.Bar == 0)
    {
        DPRINT("Unused address register\n");
        *Base = 0;
        *Length = 0;
        *Flags = 0;
        return TRUE;
    }

    *Base = ((OriginalValue.Bar & PCI_ADDRESS_IO_SPACE)
             ? (OriginalValue.Bar & PCI_ADDRESS_IO_ADDRESS_MASK_64)
             : (OriginalValue.Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64));

    Size = (NewValue.Bar & PCI_ADDRESS_IO_SPACE)
           ? (NewValue.Bar & PCI_ADDRESS_IO_ADDRESS_MASK_64)
           : (NewValue.Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);
    *Length = Size & ~(Size - 1);

    *Flags = (NewValue.Bar & PCI_ADDRESS_IO_SPACE)
             ? (NewValue.Bar & ~PCI_ADDRESS_IO_ADDRESS_MASK_64)
             : (NewValue.Bar & ~PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);

    return TRUE;
}


static NTSTATUS
PdoQueryResourceRequirements(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PCI_COMMON_CONFIG PciConfig;
    PIO_RESOURCE_REQUIREMENTS_LIST ResourceList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Size;
    ULONG ResCount = 0;
    ULONG ListSize;
    UCHAR Bar;
    ULONGLONG Base;
    ULONGLONG Length;
    ULONG Flags;
    ULONGLONG MaximumAddress;

    UNREFERENCED_PARAMETER(IrpSp);
    DPRINT("PdoQueryResourceRequirements() called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Get PCI configuration space */
    Size= HalGetBusData(PCIConfiguration,
                        DeviceExtension->PciDevice->BusNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                        &PciConfig,
                        PCI_COMMON_HDR_LENGTH);
    DPRINT("Size %lu\n", Size);
    if (Size < PCI_COMMON_HDR_LENGTH)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("Command register: 0x%04hx\n", PciConfig.Command);

    /* Count required resource descriptors */
    ResCount = 0;
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length != 0)
                ResCount += 2;
        }

        /* FIXME: Check ROM address */

        if (PciConfig.u.type0.InterruptPin != 0)
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length != 0)
                ResCount += 2;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Count Cardbus bridge resources */
    }
    else
    {
        DPRINT1("Unsupported header type %d\n", PCI_CONFIGURATION_TYPE(&PciConfig));
    }

    if (ResCount == 0)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    /* Calculate the resource list size */
    ListSize = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
               ResCount * sizeof(IO_RESOURCE_DESCRIPTOR);

    DPRINT("ListSize %lu (0x%lx)\n", ListSize, ListSize);

    /* Allocate the resource requirements list */
    ResourceList = ExAllocatePoolWithTag(PagedPool,
                                         ListSize,
                                         TAG_PCI);
    if (ResourceList == NULL)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ResourceList, ListSize);
    ResourceList->ListSize = ListSize;
    ResourceList->InterfaceType = PCIBus;
    ResourceList->BusNumber = DeviceExtension->PciDevice->BusNumber;
    ResourceList->SlotNumber = DeviceExtension->PciDevice->SlotNumber.u.AsULONG;
    ResourceList->AlternativeLists = 1;

    ResourceList->List[0].Version = 1;
    ResourceList->List[0].Revision = 1;
    ResourceList->List[0].Count = ResCount;

    Descriptor = &ResourceList->List[0].Descriptors[0];
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   &MaximumAddress))
            {
                DPRINT1("PdoGetRangeLength() failed\n");
                break;
            }

            if (Length == 0)
            {
                DPRINT("Unused address register\n");
                continue;
            }

            /* Set preferred descriptor */
            Descriptor->Option = IO_RESOURCE_PREFERRED;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = 1;
                Descriptor->u.Port.MinimumAddress.QuadPart = Base;
                Descriptor->u.Port.MaximumAddress.QuadPart = Base + Length - 1;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = Base;
                Descriptor->u.Memory.MaximumAddress.QuadPart = Base + Length - 1;
            }
            Descriptor++;

            /* Set alternative descriptor */
            Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            Descriptor++;
        }

        /* FIXME: Check ROM address */

        if (PciConfig.u.type0.InterruptPin != 0)
        {
            Descriptor->Option = 0; /* Required */
            Descriptor->Type = CmResourceTypeInterrupt;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;

            Descriptor->u.Interrupt.MinimumVector = 0;
            Descriptor->u.Interrupt.MaximumVector = 0xFF;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   &MaximumAddress))
            {
                DPRINT1("PdoGetRangeLength() failed\n");
                break;
            }

            if (Length == 0)
            {
                DPRINT("Unused address register\n");
                continue;
            }

            /* Set preferred descriptor */
            Descriptor->Option = IO_RESOURCE_PREFERRED;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = 1;
                Descriptor->u.Port.MinimumAddress.QuadPart = Base;
                Descriptor->u.Port.MaximumAddress.QuadPart = Base + Length - 1;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = Base;
                Descriptor->u.Memory.MaximumAddress.QuadPart = Base + Length - 1;
            }
            Descriptor++;

            /* Set alternative descriptor */
            Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            Descriptor++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
        {
            Descriptor->Option = 0; /* Required */
            Descriptor->Type = CmResourceTypeBusNumber;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;

            ResourceList->BusNumber =
            Descriptor->u.BusNumber.MinBusNumber =
            Descriptor->u.BusNumber.MaxBusNumber = DeviceExtension->PciDevice->PciConfig.u.type1.SecondaryBus;
            Descriptor->u.BusNumber.Length = 1;
            Descriptor->u.BusNumber.Reserved = 0;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Add Cardbus bridge resources */
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    return STATUS_SUCCESS;
}


static NTSTATUS
PdoQueryResources(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PCI_COMMON_CONFIG PciConfig;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Size;
    ULONG ResCount = 0;
    ULONG ListSize;
    UCHAR Bar;
    ULONGLONG Base;
    ULONGLONG Length;
    ULONG Flags;

    DPRINT("PdoQueryResources() called\n");

    UNREFERENCED_PARAMETER(IrpSp);
    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Get PCI configuration space */
    Size= HalGetBusData(PCIConfiguration,
                        DeviceExtension->PciDevice->BusNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                        &PciConfig,
                        PCI_COMMON_HDR_LENGTH);
    DPRINT("Size %lu\n", Size);
    if (Size < PCI_COMMON_HDR_LENGTH)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("Command register: 0x%04hx\n", PciConfig.Command);

    /* Count required resource descriptors */
    ResCount = 0;
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length)
                ResCount++;
        }

        if ((PciConfig.u.type0.InterruptPin != 0) &&
            (PciConfig.u.type0.InterruptLine != 0) &&
            (PciConfig.u.type0.InterruptLine != 0xFF))
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length != 0)
                ResCount++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Count Cardbus bridge resources */
    }
    else
    {
        DPRINT1("Unsupported header type %d\n", PCI_CONFIGURATION_TYPE(&PciConfig));
    }

    if (ResCount == 0)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    /* Calculate the resource list size */
    ListSize = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
               ResCount * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    /* Allocate the resource list */
    ResourceList = ExAllocatePoolWithTag(PagedPool,
                                         ListSize,
                                         TAG_PCI);
    if (ResourceList == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ResourceList, ListSize);
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = PCIBus;
    ResourceList->List[0].BusNumber = DeviceExtension->PciDevice->BusNumber;

    PartialList = &ResourceList->List[0].PartialResourceList;
    PartialList->Version = 1;
    PartialList->Revision = 1;
    PartialList->Count = ResCount;

    Descriptor = &PartialList->PartialDescriptors[0];
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register\n");
                continue;
            }

            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;
                Descriptor->u.Port.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Port.Length = Length;

                /* Enable IO space access */
                DeviceExtension->PciDevice->EnableIoSpace = TRUE;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;
                Descriptor->u.Memory.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Memory.Length = Length;

                /* Enable memory space access */
                DeviceExtension->PciDevice->EnableMemorySpace = TRUE;
            }

            Descriptor++;
        }

        /* Add interrupt resource */
        if ((PciConfig.u.type0.InterruptPin != 0) &&
            (PciConfig.u.type0.InterruptLine != 0) &&
            (PciConfig.u.type0.InterruptLine != 0xFF))
        {
            Descriptor->Type = CmResourceTypeInterrupt;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptor->u.Interrupt.Level = PciConfig.u.type0.InterruptLine;
            Descriptor->u.Interrupt.Vector = PciConfig.u.type0.InterruptLine;
            Descriptor->u.Interrupt.Affinity = 0xFFFFFFFF;
        }

        /* Allow bus master mode */
       DeviceExtension->PciDevice->EnableBusMaster = TRUE;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register\n");
                continue;
            }

            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;
                Descriptor->u.Port.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Port.Length = Length;

                /* Enable IO space access */
                DeviceExtension->PciDevice->EnableIoSpace = TRUE;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;
                Descriptor->u.Memory.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Memory.Length = Length;

                /* Enable memory space access */
                DeviceExtension->PciDevice->EnableMemorySpace = TRUE;
            }

            Descriptor++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
        {
            Descriptor->Type = CmResourceTypeBusNumber;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;

            ResourceList->List[0].BusNumber =
            Descriptor->u.BusNumber.Start = DeviceExtension->PciDevice->PciConfig.u.type1.SecondaryBus;
            Descriptor->u.BusNumber.Length = 1;
            Descriptor->u.BusNumber.Reserved = 0;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Add Cardbus bridge resources */
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    return STATUS_SUCCESS;
}


static VOID NTAPI
InterfaceReference(
    IN PVOID Context)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceReference(%p)\n", Context);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    InterlockedIncrement(&DeviceExtension->References);
}


static VOID NTAPI
InterfaceDereference(
    IN PVOID Context)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceDereference(%p)\n", Context);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    InterlockedDecrement(&DeviceExtension->References);
}

static TRANSLATE_BUS_ADDRESS InterfaceBusTranslateBusAddress;

static
BOOLEAN
NTAPI
InterfaceBusTranslateBusAddress(
    IN PVOID Context,
    IN PHYSICAL_ADDRESS BusAddress,
    IN ULONG Length,
    IN OUT PULONG AddressSpace,
    OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceBusTranslateBusAddress(%p %p 0x%lx %p %p)\n",
           Context, BusAddress, Length, AddressSpace, TranslatedAddress);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    return HalTranslateBusAddress(PCIBus,
                                  DeviceExtension->PciDevice->BusNumber,
                                  BusAddress,
                                  AddressSpace,
                                  TranslatedAddress);
}

static GET_DMA_ADAPTER InterfaceBusGetDmaAdapter;

static
PDMA_ADAPTER
NTAPI
InterfaceBusGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters)
{
    DPRINT("InterfaceBusGetDmaAdapter(%p %p %p)\n",
           Context, DeviceDescription, NumberOfMapRegisters);
    return (PDMA_ADAPTER)HalGetAdapter(DeviceDescription, NumberOfMapRegisters);
}

static GET_SET_DEVICE_DATA InterfaceBusSetBusData;

static
ULONG
NTAPI
InterfaceBusSetBusData(
    IN PVOID Context,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG Size;

    DPRINT("InterfaceBusSetBusData(%p 0x%lx %p 0x%lx 0x%lx)\n",
           Context, DataType, Buffer, Offset, Length);

    if (DataType != PCI_WHICHSPACE_CONFIG)
    {
        DPRINT("Unknown DataType %lu\n", DataType);
        return 0;
    }

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    /* Get PCI configuration space */
    Size = HalSetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 Buffer,
                                 Offset,
                                 Length);
    return Size;
}

static GET_SET_DEVICE_DATA InterfaceBusGetBusData;

static
ULONG
NTAPI
InterfaceBusGetBusData(
    IN PVOID Context,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG Size;

    DPRINT("InterfaceBusGetBusData(%p 0x%lx %p 0x%lx 0x%lx) called\n",
           Context, DataType, Buffer, Offset, Length);

    if (DataType != PCI_WHICHSPACE_CONFIG)
    {
        DPRINT("Unknown DataType %lu\n", DataType);
        return 0;
    }

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    /* Get PCI configuration space */
    Size = HalGetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 Buffer,
                                 Offset,
                                 Length);
    return Size;
}


static BOOLEAN NTAPI
InterfacePciDevicePresent(
    IN USHORT VendorID,
    IN USHORT DeviceID,
    IN UCHAR RevisionID,
    IN USHORT SubVendorID,
    IN USHORT SubSystemID,
    IN ULONG Flags)
{
    PFDO_DEVICE_EXTENSION FdoDeviceExtension;
    PPCI_DEVICE PciDevice;
    PLIST_ENTRY CurrentBus, CurrentEntry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&DriverExtension->BusListLock, &OldIrql);
    CurrentBus = DriverExtension->BusListHead.Flink;
    while (!Found && CurrentBus != &DriverExtension->BusListHead)
    {
        FdoDeviceExtension = CONTAINING_RECORD(CurrentBus, FDO_DEVICE_EXTENSION, ListEntry);

        KeAcquireSpinLockAtDpcLevel(&FdoDeviceExtension->DeviceListLock);
        CurrentEntry = FdoDeviceExtension->DeviceListHead.Flink;
        while (!Found && CurrentEntry != &FdoDeviceExtension->DeviceListHead)
        {
            PciDevice = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);
            if (PciDevice->PciConfig.VendorID == VendorID &&
                PciDevice->PciConfig.DeviceID == DeviceID)
            {
                if (!(Flags & PCI_USE_SUBSYSTEM_IDS) ||
                    (PciDevice->PciConfig.u.type0.SubVendorID == SubVendorID &&
                     PciDevice->PciConfig.u.type0.SubSystemID == SubSystemID))
                {
                    if (!(Flags & PCI_USE_REVISION) ||
                        PciDevice->PciConfig.RevisionID == RevisionID)
                    {
                        DPRINT("Found the PCI device\n");
                        Found = TRUE;
                    }
                }
            }

            CurrentEntry = CurrentEntry->Flink;
        }

        KeReleaseSpinLockFromDpcLevel(&FdoDeviceExtension->DeviceListLock);
        CurrentBus = CurrentBus->Flink;
    }
    KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

    return Found;
}


static BOOLEAN
CheckPciDevice(
    IN PPCI_COMMON_CONFIG PciConfig,
    IN PPCI_DEVICE_PRESENCE_PARAMETERS Parameters)
{
    if ((Parameters->Flags & PCI_USE_VENDEV_IDS) &&
        (PciConfig->VendorID != Parameters->VendorID ||
         PciConfig->DeviceID != Parameters->DeviceID))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_CLASS_SUBCLASS) &&
        (PciConfig->BaseClass != Parameters->BaseClass ||
         PciConfig->SubClass != Parameters->SubClass))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_PROGIF) &&
         PciConfig->ProgIf != Parameters->ProgIf)
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_SUBSYSTEM_IDS) &&
        (PciConfig->u.type0.SubVendorID != Parameters->SubVendorID ||
         PciConfig->u.type0.SubSystemID != Parameters->SubSystemID))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_REVISION) &&
        PciConfig->RevisionID != Parameters->RevisionID)
    {
        return FALSE;
    }

    return TRUE;
}


static BOOLEAN NTAPI
InterfacePciDevicePresentEx(
    IN PVOID Context,
    IN PPCI_DEVICE_PRESENCE_PARAMETERS Parameters)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PFDO_DEVICE_EXTENSION MyFdoDeviceExtension;
    PFDO_DEVICE_EXTENSION FdoDeviceExtension;
    PPCI_DEVICE PciDevice;
    PLIST_ENTRY CurrentBus, CurrentEntry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    DPRINT("InterfacePciDevicePresentEx(%p %p) called\n",
           Context, Parameters);

    if (!Parameters || Parameters->Size != sizeof(PCI_DEVICE_PRESENCE_PARAMETERS))
        return FALSE;

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    MyFdoDeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;

    if (Parameters->Flags & PCI_USE_LOCAL_DEVICE)
    {
        return CheckPciDevice(&DeviceExtension->PciDevice->PciConfig, Parameters);
    }

    KeAcquireSpinLock(&DriverExtension->BusListLock, &OldIrql);
    CurrentBus = DriverExtension->BusListHead.Flink;
    while (!Found && CurrentBus != &DriverExtension->BusListHead)
    {
        FdoDeviceExtension = CONTAINING_RECORD(CurrentBus, FDO_DEVICE_EXTENSION, ListEntry);
        if (!(Parameters->Flags & PCI_USE_LOCAL_BUS) || FdoDeviceExtension == MyFdoDeviceExtension)
        {
            KeAcquireSpinLockAtDpcLevel(&FdoDeviceExtension->DeviceListLock);
            CurrentEntry = FdoDeviceExtension->DeviceListHead.Flink;
            while (!Found && CurrentEntry != &FdoDeviceExtension->DeviceListHead)
            {
                PciDevice = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);

                if (CheckPciDevice(&PciDevice->PciConfig, Parameters))
                {
                    DPRINT("Found the PCI device\n");
                    Found = TRUE;
                }

                CurrentEntry = CurrentEntry->Flink;
            }

            KeReleaseSpinLockFromDpcLevel(&FdoDeviceExtension->DeviceListLock);
        }
        CurrentBus = CurrentBus->Flink;
    }
    KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

    return Found;
}


static NTSTATUS
PdoQueryInterface(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                         &GUID_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        /* BUS_INTERFACE_STANDARD */
        if (IrpSp->Parameters.QueryInterface.Version < 1)
            Status = STATUS_NOT_SUPPORTED;
        else if (IrpSp->Parameters.QueryInterface.Size < sizeof(BUS_INTERFACE_STANDARD))
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            PBUS_INTERFACE_STANDARD BusInterface;
            BusInterface = (PBUS_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;
            BusInterface->Size = sizeof(BUS_INTERFACE_STANDARD);
            BusInterface->Version = 1;
            BusInterface->TranslateBusAddress = InterfaceBusTranslateBusAddress;
            BusInterface->GetDmaAdapter = InterfaceBusGetDmaAdapter;
            BusInterface->SetBusData = InterfaceBusSetBusData;
            BusInterface->GetBusData = InterfaceBusGetBusData;
            Status = STATUS_SUCCESS;
        }
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_DEVICE_PRESENT_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        /* PCI_DEVICE_PRESENT_INTERFACE */
        if (IrpSp->Parameters.QueryInterface.Version < 1)
            Status = STATUS_NOT_SUPPORTED;
        else if (IrpSp->Parameters.QueryInterface.Size < sizeof(PCI_DEVICE_PRESENT_INTERFACE))
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            PPCI_DEVICE_PRESENT_INTERFACE PciDevicePresentInterface;
            PciDevicePresentInterface = (PPCI_DEVICE_PRESENT_INTERFACE)IrpSp->Parameters.QueryInterface.Interface;
            PciDevicePresentInterface->Size = sizeof(PCI_DEVICE_PRESENT_INTERFACE);
            PciDevicePresentInterface->Version = 1;
            PciDevicePresentInterface->IsDevicePresent = InterfacePciDevicePresent;
            PciDevicePresentInterface->IsDevicePresentEx = InterfacePciDevicePresentEx;
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        /* Not a supported interface */
        return STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(Status))
    {
        /* Add a reference for the returned interface */
        PINTERFACE Interface;
        Interface = (PINTERFACE)IrpSp->Parameters.QueryInterface.Interface;
        Interface->Context = DeviceObject;
        Interface->InterfaceReference = InterfaceReference;
        Interface->InterfaceDereference = InterfaceDereference;
        Interface->InterfaceReference(Interface->Context);
    }

    return Status;
}

static BOOLEAN
PdoDetectAcpiPcieSupport(VOID)
{
    BOOLEAN HasEcam = FALSE;
    BOOLEAN HasAcpiInterruptRouting = FALSE;
    
    /* Check if we have ACPI Enhanced Configuration Access Method (ECAM) support */
    /* This is critical for PCIe extended configuration space access */
    /* For now, we do a simple check - more sophisticated detection could be added */
    
    /* Try to detect ACPI subsystem availability */
    /* In a more complete implementation, we would check for:
     * - ACPI MCFG table (Memory Mapped Configuration space)
     * - ACPI _CRS methods for PCIe root complex
     * - ACPI interrupt routing tables
     */
    
    /* Simple heuristic: assume ACPI PCIe support is available if we're on
     * a system that appears to have modern ACPI (this is conservative) */
    
    /* For VirtualBox ICH9 compatibility, we need to be more conservative
     * and assume limited ACPI PCIe support until proven otherwise */
    
    DPRINT("Detecting ACPI PCIe support...\n");
    
    /* Check for basic ACPI availability */
    /* This is a simplified check - in reality we'd examine ACPI tables */
    if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
    {
        /* We have basic ACPI, but may not have full PCIe ECAM support */
        /* For VirtualBox ICH9, be conservative and assume limited support */
        HasEcam = FALSE;  /* Conservative assumption for compatibility */
        HasAcpiInterruptRouting = TRUE;  /* Basic interrupt routing should work */
    }
    
    DPRINT("ACPI PCIe support: ECAM=%s, Interrupt Routing=%s\n",
           HasEcam ? "Yes" : "No",
           HasAcpiInterruptRouting ? "Yes" : "No");
    
    /* Return TRUE only if we have both ECAM and interrupt routing */
    /* This conservative approach should help with VirtualBox ICH9 */
    return (HasEcam && HasAcpiInterruptRouting);
}

static VOID
PdoConfigureMsiSupport(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR MsiCapability, MsiXCapability, PciExpressCapability;
    PCI_COMMON_CONFIG *PciConfig;
    UCHAR CapabilityOffset;
    UCHAR CurrentCapability;
    ULONG LoopCount = 0;
    BOOLEAN AcpiPcieSupported = FALSE;
    
    DPRINT("Checking MSI/MSI-X and PCIe support for device 0x%x on bus 0x%x\n",
           DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
           DeviceExtension->PciDevice->BusNumber);
    
    PciConfig = &DeviceExtension->PciDevice->PciConfig;
    
    /* Check if device supports capabilities */
    if (!(PciConfig->Status & PCI_STATUS_CAPABILITIES_LIST))
    {
        DPRINT("Device does not support capabilities list\n");
        return;
    }
    
    /* Detect if we have proper ACPI PCIe support */
    AcpiPcieSupported = PdoDetectAcpiPcieSupport();
    if (!AcpiPcieSupported)
    {
        DPRINT("ACPI PCIe support not available, using legacy PCI mode\n");
    }
    
    /* Get capabilities pointer */
    CapabilityOffset = PciConfig->u.type0.CapabilitiesPtr;
    
    MsiCapability = 0;
    MsiXCapability = 0;
    PciExpressCapability = 0;
    
    /* Walk the capabilities list to find MSI/MSI-X and PCIe */
    while (CapabilityOffset != 0 && LoopCount < 48) /* Prevent infinite loops */
    {
        /* Read capability header */
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &CurrentCapability,
                              CapabilityOffset,
                              sizeof(UCHAR));
        
        if (CurrentCapability == PCI_CAPABILITY_ID_MSI)
        {
            MsiCapability = CapabilityOffset;
            DPRINT("Found MSI capability at offset 0x%02x\n", CapabilityOffset);
        }
        else if (CurrentCapability == PCI_CAPABILITY_ID_MSIX)
        {
            MsiXCapability = CapabilityOffset;
            DPRINT("Found MSI-X capability at offset 0x%02x\n", CapabilityOffset);
        }
        else if (CurrentCapability == PCI_CAPABILITY_ID_PCI_EXPRESS && AcpiPcieSupported)
        {
            PciExpressCapability = CapabilityOffset;
            DPRINT("Found PCIe capability at offset 0x%02x\n", CapabilityOffset);
        }
        
        /* Get next capability offset */
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &CapabilityOffset,
                              CapabilityOffset + 1,
                              sizeof(UCHAR));
        
        LoopCount++;
    }
    
    /* Store capability information in device extension for later use */
    if (MsiXCapability != 0)
    {
        DeviceExtension->PciDevice->MsiXCapabilityOffset = MsiXCapability;
        DeviceExtension->PciDevice->SupportsMsiX = TRUE;
        DPRINT("Device supports MSI-X\n");
    }
    else if (MsiCapability != 0)
    {
        DeviceExtension->PciDevice->MsiCapabilityOffset = MsiCapability;
        DeviceExtension->PciDevice->SupportsMsi = TRUE;
        DPRINT("Device supports MSI\n");
    }
    else
    {
        DPRINT("Device does not support MSI or MSI-X\n");
    }
    
    /* Configure PCIe capabilities only if ACPI support is available */
    if (PciExpressCapability != 0 && AcpiPcieSupported)
    {
        DeviceExtension->PciDevice->PciExpressCapabilityOffset = PciExpressCapability;
        DeviceExtension->PciDevice->IsPciExpress = TRUE;
        DPRINT("Device is PCIe with ACPI support\n");
        
        /* Configure PCIe-specific features with basic error handling */
        PdoConfigurePciExpressSupport(DeviceObject, DeviceExtension);
        
        /* If PCIe configuration failed, fall back to treating as legacy PCI */
        if (!DeviceExtension->PciDevice->IsPciExpress)
        {
            DPRINT1("PCIe configuration failed, treating as legacy PCI\n");
        }
    }
    else if (PciExpressCapability != 0)
    {
        DPRINT("Device has PCIe capability but ACPI support insufficient, treating as legacy PCI\n");
        /* Device has PCIe capability but we don't have proper ACPI support */
        /* Treat it as legacy PCI to avoid issues */
    }
    else
    {
        DPRINT("Device is legacy PCI\n");
    }
}

/**
 * @brief Configure MSI capability for a PCI device
 */
static NTSTATUS
PciConfigureMsiCapability(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Vector)
{
    USHORT MsiControl;
    ULONG MsiAddress;
    USHORT MsiData;
    UCHAR CapabilityOffset;
    BOOLEAN Is64Bit;
    
    CapabilityOffset = DeviceExtension->PciDevice->MsiCapabilityOffset;
    
    DPRINT("Configuring MSI capability at offset 0x%02x\n", CapabilityOffset);
    
    /* Read MSI control register */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Check if 64-bit addressing is supported */
    Is64Bit = (MsiControl & 0x0080) != 0;
    
    /* Calculate MSI address (points to local APIC) */
    MsiAddress = 0xFEE00000; /* Base MSI address for local APIC */
    
    /* Calculate MSI data (contains the vector) */
    MsiData = (USHORT)Vector;
    
    /* Write MSI address (32-bit) */
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MsiAddress,
                          CapabilityOffset + 4,
                          sizeof(ULONG));
    
    if (Is64Bit)
    {
        /* Write upper 32 bits of address */
        ULONG MsiAddressUpper = 0;
        HalSetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &MsiAddressUpper,
                              CapabilityOffset + 8,
                              sizeof(ULONG));
        
        /* Write MSI data */
        HalSetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &MsiData,
                              CapabilityOffset + 12,
                              sizeof(USHORT));
    }
    else
    {
        /* Write MSI data */
        HalSetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &MsiData,
                              CapabilityOffset + 8,
                              sizeof(USHORT));
    }
    
    /* Enable MSI */
    MsiControl |= 0x0001; /* Set MSI Enable bit */
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    DPRINT("MSI configured: Vector=%lu, Address=0x%08lx, Data=0x%04x\n",
           Vector, MsiAddress, MsiData);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Configure MSI-X capability for a PCI device
 */
static NTSTATUS
PciConfigureMsiXCapability(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Vector)
{
    USHORT MessageControl;
    USHORT TableSize;
    ULONG TableOffset;
    ULONG TableBar;
    UCHAR CapabilityOffset;
    PHYSICAL_ADDRESS TablePhysical;
    PVOID TableVirtual = NULL;
    PULONG TableEntry;
    
    CapabilityOffset = DeviceExtension->PciDevice->MsiXCapabilityOffset;
    
    DPRINT("Configuring MSI-X capability at offset 0x%02x\n", CapabilityOffset);
    
    /* Read MSI-X Message Control */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Calculate table size (Table Size field + 1) */
    TableSize = (MessageControl & 0x7FF) + 1;
    
    DPRINT("MSI-X Table Size: %u entries\n", TableSize);
    
    /* Read Table BIR and Offset */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &TableOffset,
                          CapabilityOffset + 4,
                          sizeof(ULONG));
    TableBar = TableOffset & 0x7;  /* Lower 3 bits = BAR index */
    TableOffset &= ~0x7;           /* Upper bits = offset within BAR */
    
    DPRINT("MSI-X Table: BAR %lu, Offset 0x%lx\n", TableBar, TableOffset);
    
    /* Get BAR address for table */
    if (TableBar < 6)  /* Valid BAR index */
    {
        ULONG BarValue;
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &BarValue,
                              0x10 + (TableBar * 4),
                              sizeof(ULONG));
        
        if (BarValue & 0x1)
        {
            /* I/O BAR - not supported for MSI-X */
            DPRINT1("MSI-X Table in I/O BAR not supported\n");
            return STATUS_NOT_SUPPORTED;
        }
        
        /* Check if BAR is properly configured */
        if ((BarValue & ~0xF) == 0)
        {
            DPRINT1("MSI-X Table BAR not configured\n");
            return STATUS_DEVICE_NOT_READY;
        }
        
        TablePhysical.QuadPart = (BarValue & ~0xF) + TableOffset;
        
        /* Validate physical address is reasonable */
        if (TablePhysical.QuadPart == 0 || TablePhysical.QuadPart == 0xFFFFFFFF)
        {
            DPRINT1("Invalid MSI-X table physical address: 0x%I64x\n", TablePhysical.QuadPart);
            return STATUS_INVALID_ADDRESS;
        }
        
        /* Map the MSI-X table for configuration with error handling */
        TableVirtual = MmMapIoSpace(TablePhysical, 
                                   16,  /* Map only first entry (16 bytes) */
                                   MmNonCached);
        if (!TableVirtual)
        {
            DPRINT1("Failed to map MSI-X table at 0x%I64x\n", TablePhysical.QuadPart);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        DPRINT("Mapped MSI-X table at physical 0x%I64x to virtual %p\n", 
               TablePhysical.QuadPart, TableVirtual);
    }
    else
    {
        DPRINT1("Invalid MSI-X Table BAR index: %lu\n", TableBar);
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Configure first MSI-X table entry */
    if (TableVirtual)
    {
        TableEntry = (PULONG)TableVirtual;
        
        /* Message Address - point to local APIC */
        TableEntry[0] = 0xFEE00000;
        
        /* Message Upper Address */
        TableEntry[1] = 0;
        
        /* Message Data - contains the vector */
        TableEntry[2] = Vector;
        
        /* Vector Control - unmask this entry */
        TableEntry[3] = 0;  /* Clear mask bit */
        
        DPRINT("MSI-X entry 0: Vector=%lu, Address=0x%08lx\n", 
               Vector, 0xFEE00000);
    }
    
    /* Enable MSI-X but clear function mask */
    MessageControl |= 0x8000;  /* MSI-X Enable */
    MessageControl &= ~0x4000; /* Clear Function Mask */
    
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    DPRINT("MSI-X enabled with vector %lu\n", Vector);
    
    /* Clean up mapping */
    if (TableVirtual)
    {
        MmUnmapIoSpace(TableVirtual, 16);
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Disable MSI capability for a PCI device
 */
static VOID
PciDisableMsiCapability(
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    USHORT MsiControl;
    UCHAR CapabilityOffset;
    
    CapabilityOffset = DeviceExtension->PciDevice->MsiCapabilityOffset;
    
    /* Read MSI control register */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Disable MSI */
    MsiControl &= ~0x0001; /* Clear MSI Enable bit */
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MsiControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    DPRINT("MSI disabled\n");
}

/**
 * @brief Disable MSI-X capability for a PCI device
 */
static VOID
PciDisableMsiXCapability(
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    USHORT MessageControl;
    UCHAR CapabilityOffset;
    
    CapabilityOffset = DeviceExtension->PciDevice->MsiXCapabilityOffset;
    
    /* Read MSI-X Message Control */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Disable MSI-X */
    MessageControl &= ~0x8000; /* Clear MSI-X Enable bit */
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &MessageControl,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    DPRINT("MSI-X disabled\n");
}

/**
 * @brief Configure PCIe-specific support for a device
 */
static VOID
PdoConfigurePciExpressSupport(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    USHORT PcieCapabilities;
    USHORT DeviceCapabilities;
    USHORT DeviceControl;
    USHORT LinkCapabilities;
    USHORT LinkControl;
    UCHAR CapabilityOffset;
    UCHAR DeviceType;
    UCHAR CapabilityVersion;
    NTSTATUS Status;
    
    CapabilityOffset = DeviceExtension->PciDevice->PciExpressCapabilityOffset;
    
    DPRINT("Configuring PCIe support at offset 0x%02x\n", CapabilityOffset);
    
    /* Validate capability offset */
    if (CapabilityOffset == 0 || CapabilityOffset < 0x40)
    {
        DPRINT1("Invalid PCIe capability offset: 0x%02x\n", CapabilityOffset);
        return;
    }
    
    /* Read PCIe Capabilities Register with error checking */
    Status = HalGetBusDataByOffset(PCIConfiguration,
                                   DeviceExtension->PciDevice->BusNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                   &PcieCapabilities,
                                   CapabilityOffset + PCIE_CAPABILITIES_REGISTER,
                                   sizeof(USHORT));
    
    if (Status != sizeof(USHORT) || PcieCapabilities == 0xFFFF)
    {
        DPRINT1("Failed to read PCIe capabilities register\n");
        DeviceExtension->PciDevice->IsPciExpress = FALSE;
        return;
    }
    
    /* Extract capability version and device type */
    CapabilityVersion = (UCHAR)(PcieCapabilities & 0xF);
    DeviceType = (UCHAR)((PcieCapabilities >> 4) & 0xF);
    
    /* Validate PCIe version */
    if (CapabilityVersion == 0 || CapabilityVersion > 4)
    {
        DPRINT1("Unsupported PCIe version: %u\n", CapabilityVersion);
        /* Continue with basic support */
        CapabilityVersion = 1;
    }
    
    DeviceExtension->PciDevice->PciExpressVersion = CapabilityVersion;
    DeviceExtension->PciDevice->DeviceType = DeviceType;
    DeviceExtension->PciDevice->PciExpressCapabilities = PcieCapabilities;
    
    DPRINT("PCIe Version: %u, Device Type: %u\n", CapabilityVersion, DeviceType);
    
    /* Read Device Capabilities with bounds checking */
    Status = HalGetBusDataByOffset(PCIConfiguration,
                                   DeviceExtension->PciDevice->BusNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                   &DeviceCapabilities,
                                   CapabilityOffset + PCIE_DEVICE_CAPABILITIES_REGISTER,
                                   sizeof(USHORT));
    
    if (Status != sizeof(USHORT))
    {
        DPRINT1("Failed to read PCIe device capabilities\n");
        return;
    }
    
    /* Read Device Control Register */
    Status = HalGetBusDataByOffset(PCIConfiguration,
                                   DeviceExtension->PciDevice->BusNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                   &DeviceControl,
                                   CapabilityOffset + PCIE_DEVICE_CONTROL_REGISTER,
                                   sizeof(USHORT));
    
    if (Status != sizeof(USHORT))
    {
        DPRINT1("Failed to read PCIe device control register\n");
        return;
    }
    
    DeviceExtension->PciDevice->PciExpressDeviceControl = DeviceControl;
    
    /* Configure PCIe Device Control conservatively for VirtualBox compatibility */
    DeviceControl |= PCIE_DEVICE_CONTROL_CORRECTABLE_ERROR_ENABLE;
    /* Be more conservative with error reporting for virtual environments */
    if (DeviceType != PCIE_DEVICE_TYPE_ROOT_PORT)
    {
        DeviceControl |= PCIE_DEVICE_CONTROL_NON_FATAL_ERROR_ENABLE;
    }
    
    /* Only enable relaxed ordering for known compatible device types */
    if (DeviceType == PCIE_DEVICE_TYPE_ENDPOINT || DeviceType == PCIE_DEVICE_TYPE_LEGACY_ENDPOINT)
    {
        DeviceControl |= PCIE_DEVICE_CONTROL_RELAXED_ORDERING_ENABLE;
    }
    
    /* Set conservative maximum payload size (128 bytes for VirtualBox compatibility) */
    DeviceControl &= ~PCIE_DEVICE_CONTROL_MAX_PAYLOAD_SIZE_MASK;
    /* 128 bytes = 0, 256 bytes = 1, etc. Use 128 bytes for better compatibility */
    
    /* Write back Device Control with error checking */
    Status = HalSetBusDataByOffset(PCIConfiguration,
                                   DeviceExtension->PciDevice->BusNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                   &DeviceControl,
                                   CapabilityOffset + PCIE_DEVICE_CONTROL_REGISTER,
                                   sizeof(USHORT));
    
    if (Status != sizeof(USHORT))
    {
        DPRINT1("Failed to write PCIe device control register\n");
        return;
    }
    
    /* Configure Link Control only for appropriate device types and with more checks */
    if (DeviceType == PCIE_DEVICE_TYPE_ROOT_PORT ||
        DeviceType == PCIE_DEVICE_TYPE_UPSTREAM_PORT ||
        DeviceType == PCIE_DEVICE_TYPE_DOWNSTREAM_PORT)
    {
        /* Read Link Capabilities */
        Status = HalGetBusDataByOffset(PCIConfiguration,
                                       DeviceExtension->PciDevice->BusNumber,
                                       DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                       &LinkCapabilities,
                                       CapabilityOffset + PCIE_LINK_CAPABILITIES_REGISTER,
                                       sizeof(USHORT));
        
        if (Status == sizeof(USHORT))
        {
            /* Read Link Control Register */
            Status = HalGetBusDataByOffset(PCIConfiguration,
                                           DeviceExtension->PciDevice->BusNumber,
                                           DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                           &LinkControl,
                                           CapabilityOffset + PCIE_LINK_CONTROL_REGISTER,
                                           sizeof(USHORT));
            
            if (Status == sizeof(USHORT))
            {
                DeviceExtension->PciDevice->PciExpressLinkControl = LinkControl;
                
                /* Be very conservative with link control in virtual environments */
                /* Only enable common clock if it's already enabled */
                if (LinkControl & PCIE_LINK_CONTROL_COMMON_CLOCK_CONFIG)
                {
                    /* Keep it enabled */
                }
                else
                {
                    /* Don't force enable it in VirtualBox */
                }
                
                /* Write back Link Control only if we made changes */
                HalSetBusDataByOffset(PCIConfiguration,
                                      DeviceExtension->PciDevice->BusNumber,
                                      DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                      &LinkControl,
                                      CapabilityOffset + PCIE_LINK_CONTROL_REGISTER,
                                      sizeof(USHORT));
                
                DPRINT("PCIe Link Control configured conservatively\n");
            }
        }
    }
    
    /* Look for additional PCIe capabilities, but be more careful */
    PdoDetectAdvancedPciExpressCapabilities(DeviceObject, DeviceExtension);
    
    DPRINT("PCIe configuration complete\n");
}

/**
 * @brief Detect advanced PCIe capabilities (AER, Power Management, etc.)
 */
static VOID
PdoDetectAdvancedPciExpressCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR CapabilityOffset;
    UCHAR CurrentCapability;
    ULONG LoopCount = 0;
    PCI_COMMON_CONFIG *PciConfig;
    
    PciConfig = &DeviceExtension->PciDevice->PciConfig;
    CapabilityOffset = PciConfig->u.type0.CapabilitiesPtr;
    
    /* Walk capabilities list looking for advanced features */
    while (CapabilityOffset != 0 && LoopCount < 48)
    {
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &CurrentCapability,
                              CapabilityOffset,
                              sizeof(UCHAR));
        
        switch (CurrentCapability)
        {
            case PCI_CAPABILITY_ID_POWER_MANAGEMENT:
                DeviceExtension->PciDevice->SupportsPowerManagement = TRUE;
                DeviceExtension->PciDevice->PowerManagementCapabilityOffset = CapabilityOffset;
                DPRINT("Found Power Management capability at offset 0x%02x\n", CapabilityOffset);
                PdoConfigurePowerManagement(DeviceExtension, CapabilityOffset);
                break;
                
            case PCI_CAPABILITY_ID_AGP:
                DPRINT("Found AGP capability at offset 0x%02x\n", CapabilityOffset);
                break;
                
            case PCI_CAPABILITY_ID_SLOT_ID:
                DPRINT("Found Slot ID capability at offset 0x%02x\n", CapabilityOffset);
                break;
                
            case PCI_CAPABILITY_ID_CPCI_HOTSWAP:
                DeviceExtension->PciDevice->SupportsHotPlug = TRUE;
                DPRINT("Found CompactPCI Hot Swap capability at offset 0x%02x\n", CapabilityOffset);
                break;
        }
        
        /* Get next capability offset */
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &CapabilityOffset,
                              CapabilityOffset + 1,
                              sizeof(UCHAR));
        
        LoopCount++;
    }
    
    /* Check for PCIe Extended Capabilities (if PCIe 1.1+) */
    if (DeviceExtension->PciDevice->PciExpressVersion >= 1)
    {
        /* Only scan extended capabilities on real hardware or if explicitly enabled */
        /* VirtualBox ICH9 may have issues with extended config space access */
        DPRINT("Checking for PCIe extended capabilities (version %u)\n", 
               DeviceExtension->PciDevice->PciExpressVersion);
        
        /* Test if extended config space is accessible before scanning */
        ULONG TestHeader = 0;
        NTSTATUS TestStatus = HalGetBusDataByOffset(PCIConfiguration,
                                                    DeviceExtension->PciDevice->BusNumber,
                                                    DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                                    &TestHeader,
                                                    0x100,
                                                    sizeof(ULONG));
        
        if (TestStatus == sizeof(ULONG) && TestHeader != 0xFFFFFFFF && TestHeader != 0)
        {
            PdoDetectPciExpressExtendedCapabilities(DeviceExtension);
        }
        else
        {
            DPRINT("Extended config space not accessible, skipping extended capabilities\n");
        }
    }
}

/**
 * @brief Configure Power Management capability
 */
static VOID
PdoConfigurePowerManagement(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN UCHAR CapabilityOffset)
{
    USHORT PowerManagementCapabilities;
    USHORT PowerManagementControl;
    
    /* Read PM Capabilities */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &PowerManagementCapabilities,
                          CapabilityOffset + 2,
                          sizeof(USHORT));
    
    /* Read PM Control/Status */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &PowerManagementControl,
                          CapabilityOffset + 4,
                          sizeof(USHORT));
    
    /* Set device to D0 state (fully powered) */
    PowerManagementControl &= ~0x3; /* Clear power state bits */
    
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &PowerManagementControl,
                          CapabilityOffset + 4,
                          sizeof(USHORT));
    
    DPRINT("Power Management configured: D0 state\n");
}

/**
 * @brief Detect PCIe Extended Capabilities (AER, etc.)
 */
static VOID
PdoDetectPciExpressExtendedCapabilities(
    IN PPDO_DEVICE_EXTENSION DeviceExtension)
{
    ULONG ExtendedCapabilityOffset = 0x100; /* Extended capabilities start at 0x100 */
    ULONG ExtendedCapabilityHeader;
    USHORT CapabilityId;
    UCHAR CapabilityVersion;
    USHORT NextCapabilityOffset;
    ULONG LoopCount = 0;
    
    /* Check if device supports extended configuration space */
    if (!DeviceExtension->PciDevice->IsPciExpress)
    {
        return;
    }
    
    DPRINT("Scanning PCIe Extended Capabilities\n");
    
    /* Walk extended capabilities list */
    while (ExtendedCapabilityOffset != 0 && LoopCount < 64)
    {
        /* Read extended capability header */
        HalGetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &ExtendedCapabilityHeader,
                              ExtendedCapabilityOffset,
                              sizeof(ULONG));
        
        if (ExtendedCapabilityHeader == 0 || ExtendedCapabilityHeader == 0xFFFFFFFF)
        {
            break;
        }
        
        CapabilityId = (USHORT)(ExtendedCapabilityHeader & 0xFFFF);
        CapabilityVersion = (UCHAR)((ExtendedCapabilityHeader >> 16) & 0xF);
        NextCapabilityOffset = (USHORT)((ExtendedCapabilityHeader >> 20) & 0xFFF);
        
        DPRINT("Found extended capability 0x%04x version %u at offset 0x%03lx\n", 
               CapabilityId, CapabilityVersion, ExtendedCapabilityOffset);
        
        switch (CapabilityId)
        {
            case 0x0001: /* Advanced Error Reporting */
                DeviceExtension->PciDevice->SupportsAer = TRUE;
                DeviceExtension->PciDevice->AerCapabilityOffset = (UCHAR)ExtendedCapabilityOffset;
                DPRINT("Found AER capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                PdoConfigureAdvancedErrorReporting(DeviceExtension, ExtendedCapabilityOffset);
                break;
                
            case 0x0002: /* Virtual Channel */
                DPRINT("Found Virtual Channel capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            case 0x0003: /* Device Serial Number */
                DPRINT("Found Device Serial Number capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            case 0x0004: /* Power Budgeting */
                DPRINT("Found Power Budgeting capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            case 0x0005: /* Root Complex Link Declaration */
                DPRINT("Found RC Link Declaration capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            case 0x000D: /* Alternative Routing-ID Interpretation */
                DPRINT("Found ARI capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            case 0x0010: /* SR-IOV */
                DPRINT("Found SR-IOV capability at offset 0x%03lx\n", ExtendedCapabilityOffset);
                break;
                
            default:
                DPRINT("Found unknown extended capability 0x%04x at offset 0x%03lx\n", 
                       CapabilityId, ExtendedCapabilityOffset);
                break;
        }
        
        if (NextCapabilityOffset == 0)
        {
            break;
        }
        
        ExtendedCapabilityOffset = NextCapabilityOffset;
        LoopCount++;
    }
}

/**
 * @brief Configure Advanced Error Reporting (AER)
 */
static VOID
PdoConfigureAdvancedErrorReporting(
    IN PPDO_DEVICE_EXTENSION DeviceExtension,
    IN ULONG CapabilityOffset)
{
    ULONG UncorrectableErrorMask;
    ULONG CorrectableErrorMask;
    ULONG AerControl;
    
    DPRINT("Configuring Advanced Error Reporting\n");
    
    /* Read current uncorrectable error mask */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &UncorrectableErrorMask,
                          CapabilityOffset + 0x08,
                          sizeof(ULONG));
    
    /* Read current correctable error mask */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &CorrectableErrorMask,
                          CapabilityOffset + 0x14,
                          sizeof(ULONG));
    
    /* Read AER control register */
    HalGetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &AerControl,
                          CapabilityOffset + 0x18,
                          sizeof(ULONG));
    
    /* Enable error reporting */
    AerControl |= 0x1; /* Enable First Error Pointer */
    AerControl |= 0x2; /* Enable ECRC Generation */
    AerControl |= 0x4; /* Enable ECRC Check */
    
    /* Write back AER control */
    HalSetBusDataByOffset(PCIConfiguration,
                          DeviceExtension->PciDevice->BusNumber,
                          DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                          &AerControl,
                          CapabilityOffset + 0x18,
                          sizeof(ULONG));
    
    DPRINT("AER configured successfully\n");
}

static NTSTATUS
PdoStartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PCM_RESOURCE_LIST RawResList = IrpSp->Parameters.StartDevice.AllocatedResources;
    PCM_FULL_RESOURCE_DESCRIPTOR RawFullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawPartialDesc;
    ULONG i, ii;
    PPDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    UCHAR Irq;
    USHORT Command;

    UNREFERENCED_PARAMETER(Irp);

    if (!RawResList)
        return STATUS_SUCCESS;

    /* TODO: Assign the other resources we get to the card */

    RawFullDesc = &RawResList->List[0];
    for (i = 0; i < RawResList->Count; i++, RawFullDesc = CmiGetNextResourceDescriptor(RawFullDesc))
    {
        for (ii = 0; ii < RawFullDesc->PartialResourceList.Count; ii++)
        {
            /* Partial resource descriptors can be of variable size (CmResourceTypeDeviceSpecific),
               but only one is allowed and it must be the last one in the list! */
            RawPartialDesc = &RawFullDesc->PartialResourceList.PartialDescriptors[ii];

            if (RawPartialDesc->Type == CmResourceTypeInterrupt)
            {
                DPRINT("Assigning IRQ %u to PCI device 0x%x on bus 0x%x\n",
                        RawPartialDesc->u.Interrupt.Vector,
                        DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                        DeviceExtension->PciDevice->BusNumber);

                Irq = (UCHAR)RawPartialDesc->u.Interrupt.Vector;
                HalSetBusDataByOffset(PCIConfiguration,
                                      DeviceExtension->PciDevice->BusNumber,
                                      DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                      &Irq,
                                      0x3c /* PCI_INTERRUPT_LINE */,
                                      sizeof(UCHAR));
            }
        }
    }

    Command = 0;

    DBGPRINT("pci!PdoStartDevice: Enabling command flags for PCI device 0x%x on bus 0x%x: ",
            DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
            DeviceExtension->PciDevice->BusNumber);
    if (DeviceExtension->PciDevice->EnableBusMaster)
    {
        Command |= PCI_ENABLE_BUS_MASTER;
        DBGPRINT("[Bus master] ");
    }

    if (DeviceExtension->PciDevice->EnableMemorySpace)
    {
        Command |= PCI_ENABLE_MEMORY_SPACE;
        DBGPRINT("[Memory space enable] ");
    }

    if (DeviceExtension->PciDevice->EnableIoSpace)
    {
        Command |= PCI_ENABLE_IO_SPACE;
        DBGPRINT("[I/O space enable] ");
    }

    if (Command != 0)
    {
        DBGPRINT("\n");

        /* OR with the previous value */
        Command |= DeviceExtension->PciDevice->PciConfig.Command;

        HalSetBusDataByOffset(PCIConfiguration,
                              DeviceExtension->PciDevice->BusNumber,
                              DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                              &Command,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                              sizeof(USHORT));
    }
    else
    {
        DBGPRINT("None\n");
    }

    /* Check for MSI/MSI-X capability and configure if available */
    PdoConfigureMsiSupport(DeviceObject, DeviceExtension);

    return STATUS_SUCCESS;
}

static NTSTATUS
PdoReadConfig(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    ULONG Size;

    DPRINT("PdoReadConfig() called\n");

    Size = InterfaceBusGetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu\n", Size, IrpSp->Parameters.ReadWriteConfig.Length);
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    Irp->IoStatus.Information = Size;

    return STATUS_SUCCESS;
}


static NTSTATUS
PdoWriteConfig(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    ULONG Size;

    DPRINT1("PdoWriteConfig() called\n");

    /* Get PCI configuration space */
    Size = InterfaceBusSetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu\n", Size, IrpSp->Parameters.ReadWriteConfig.Length);
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    Irp->IoStatus.Information = Size;

    return STATUS_SUCCESS;
}

static NTSTATUS
PdoQueryDeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PDEVICE_RELATIONS DeviceRelations;

    /* We only support TargetDeviceRelation for child PDOs */
    if (IrpSp->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return Irp->IoStatus.Status;

    /* We can do this because we only return 1 PDO for TargetDeviceRelation */
    DeviceRelations = ExAllocatePoolWithTag(PagedPool, sizeof(*DeviceRelations), TAG_PCI);
    if (!DeviceRelations)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceRelations->Count = 1;
    DeviceRelations->Objects[0] = DeviceObject;

    /* The PnP manager will remove this when it is done with the PDO */
    ObReferenceObject(DeviceObject);

    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;

    return STATUS_SUCCESS;
}


/*** PUBLIC ******************************************************************/

NTSTATUS
PdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle Plug and Play IRPs for the child device
 * ARGUMENTS:
 *     DeviceObject = Pointer to physical device object of the child device
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DPRINT("Called\n");

    Status = Irp->IoStatus.Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("Unimplemented IRP_MN_DEVICE_USAGE_NOTIFICATION received\n");
            break;

        case IRP_MN_EJECT:
            DPRINT("Unimplemented IRP_MN_EJECT received\n");
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            Status = PdoQueryBusInformation(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = PdoQueryCapabilities(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = PdoQueryDeviceRelations(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT received\n");
            Status = PdoQueryDeviceText(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID received\n");
            Status = PdoQueryId(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("Unimplemented IRP_MN_QUERY_ID received\n");
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS received\n");
            Status = PdoQueryResourceRequirements(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES received\n");
            Status = PdoQueryResources(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_SET_LOCK:
            DPRINT("Unimplemented IRP_MN_SET_LOCK received\n");
            break;

        case IRP_MN_START_DEVICE:
            Status = PdoStartDevice(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE received\n");
            Status = PdoQueryInterface(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG received\n");
            Status = PdoReadConfig(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG received\n");
            Status = PdoWriteConfig(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS received\n");
            /* Nothing to do */
            Irp->IoStatus.Status = Status;
            break;

        default:
            DPRINT1("Unknown IOCTL 0x%lx\n", IrpSp->MinorFunction);
            break;
    }

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}

NTSTATUS
PdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle power management IRPs for the child device
 * ARGUMENTS:
 *     DeviceObject = Pointer to physical device object of the child device
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = Irp->IoStatus.Status;

    DPRINT("Called\n");

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_QUERY_POWER:
        case IRP_MN_SET_POWER:
            Status = STATUS_SUCCESS;
            break;
    }

    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}

/**
 * @brief Enable MSI/MSI-X interrupts for a PCI device
 */
NTSTATUS
PciEnableMsiInterrupts(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Vector,
    IN PKSERVICE_ROUTINE ServiceRoutine,
    IN PVOID ServiceContext,
    OUT PKINTERRUPT *InterruptObject)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    
    DPRINT("PciEnableMsiInterrupts called for device %p\n", PhysicalDeviceObject);
    
    /* Validate parameters */
    if (!PhysicalDeviceObject || !ServiceRoutine || !InterruptObject)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    DeviceExtension = (PPDO_DEVICE_EXTENSION)PhysicalDeviceObject->DeviceExtension;
    
    /* Verify this is a PCI PDO */
    if (!DeviceExtension || DeviceExtension->Common.IsFDO)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    /* Check if device supports MSI/MSI-X */
    if (!DeviceExtension->PciDevice->SupportsMsiX && !DeviceExtension->PciDevice->SupportsMsi)
    {
        DPRINT1("Device does not support MSI or MSI-X\n");
        return STATUS_NOT_SUPPORTED;
    }
    
    /* Configure MSI/MSI-X capability in hardware */
    if (DeviceExtension->PciDevice->SupportsMsiX)
    {
        Status = PciConfigureMsiXCapability(DeviceExtension, Vector);
    }
    else if (DeviceExtension->PciDevice->SupportsMsi)
    {
        Status = PciConfigureMsiCapability(DeviceExtension, Vector);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to configure MSI capability: 0x%08lx\n", Status);
        return Status;
    }
    
    /* Use standard interrupt connection with the configured vector */
    Status = IoConnectInterrupt(InterruptObject,
                               ServiceRoutine,
                               ServiceContext,
                               NULL,                    /* SpinLock */
                               Vector,                  /* Vector */
                               (KIRQL)Vector,          /* Irql */
                               (KIRQL)Vector,          /* SynchronizeIrql */
                               LevelSensitive,         /* InterruptMode */
                               FALSE,                  /* ShareVector */
                               0,                      /* ProcessorNumber */
                               FALSE);                 /* FloatingSave */
    
    if (NT_SUCCESS(Status))
    {
        DPRINT("MSI interrupt connected successfully with vector %lu\n", Vector);
    }
    else
    {
        DPRINT1("Failed to connect MSI interrupt: 0x%08lx\n", Status);
        /* Disable MSI capability on failure */
        if (DeviceExtension->PciDevice->SupportsMsiX)
        {
            PciDisableMsiXCapability(DeviceExtension);
        }
        else if (DeviceExtension->PciDevice->SupportsMsi)
        {
            PciDisableMsiCapability(DeviceExtension);
        }
    }
    
    return Status;
}

/**
 * @brief Disable MSI/MSI-X interrupts for a PCI device
 */
VOID
PciDisableMsiInterrupts(
    IN PKINTERRUPT InterruptObject)
{
    DPRINT("PciDisableMsiInterrupts called for interrupt %p\n", InterruptObject);
    
    if (!InterruptObject)
    {
        return;
    }
    
    /* Disconnect the standard interrupt */
    IoDisconnectInterrupt(InterruptObject);
    
    /* Note: MSI capability cleanup should be done by the device driver
     * when it's being unloaded, by calling a separate cleanup function */
    
    DPRINT("MSI interrupt disconnected\n");
}

/**
 * @brief Check if a device is a PCIe device
 */
BOOLEAN
PciIsPciExpressDevice(
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    
    if (!PhysicalDeviceObject)
    {
        return FALSE;
    }
    
    DeviceExtension = (PPDO_DEVICE_EXTENSION)PhysicalDeviceObject->DeviceExtension;
    
    /* Verify this is a PCI PDO */
    if (!DeviceExtension || DeviceExtension->Common.IsFDO)
    {
        return FALSE;
    }
    
    return DeviceExtension->PciDevice->IsPciExpress;
}

/**
 * @brief Get PCIe capabilities for a device
 */
NTSTATUS
PciGetPciExpressCapabilities(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PUSHORT Capabilities,
    OUT PUCHAR DeviceType)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    
    if (!PhysicalDeviceObject || !Capabilities || !DeviceType)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    DeviceExtension = (PPDO_DEVICE_EXTENSION)PhysicalDeviceObject->DeviceExtension;
    
    /* Verify this is a PCI PDO */
    if (!DeviceExtension || DeviceExtension->Common.IsFDO)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    /* Check if device is PCIe */
    if (!DeviceExtension->PciDevice->IsPciExpress)
    {
        return STATUS_NOT_SUPPORTED;
    }
    
    *Capabilities = DeviceExtension->PciDevice->PciExpressCapabilities;
    *DeviceType = DeviceExtension->PciDevice->DeviceType;
    
    return STATUS_SUCCESS;
}

/**
 * @brief Read from PCIe extended configuration space
 */
NTSTATUS
PciReadExtendedConfig(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Offset,
    OUT PVOID Buffer,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG BytesRead;
    
    if (!PhysicalDeviceObject || !Buffer || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Validate offset is in extended config space range */
    if (Offset < 0x100 || Offset + Length > 0x1000)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    DeviceExtension = (PPDO_DEVICE_EXTENSION)PhysicalDeviceObject->DeviceExtension;
    
    /* Verify this is a PCI PDO */
    if (!DeviceExtension || DeviceExtension->Common.IsFDO)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    /* Check if device is PCIe (required for extended config space) */
    if (!DeviceExtension->PciDevice->IsPciExpress)
    {
        return STATUS_NOT_SUPPORTED;
    }
    
    /* Read from extended configuration space */
    BytesRead = HalGetBusDataByOffset(PCIConfiguration,
                                      DeviceExtension->PciDevice->BusNumber,
                                      DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                      Buffer,
                                      Offset,
                                      Length);
    
    if (BytesRead != Length)
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Write to PCIe extended configuration space
 */
NTSTATUS
PciWriteExtendedConfig(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Offset,
    IN PVOID Buffer,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG BytesWritten;
    
    if (!PhysicalDeviceObject || !Buffer || Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Validate offset is in extended config space range */
    if (Offset < 0x100 || Offset + Length > 0x1000)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    DeviceExtension = (PPDO_DEVICE_EXTENSION)PhysicalDeviceObject->DeviceExtension;
    
    /* Verify this is a PCI PDO */
    if (!DeviceExtension || DeviceExtension->Common.IsFDO)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    /* Check if device is PCIe (required for extended config space) */
    if (!DeviceExtension->PciDevice->IsPciExpress)
    {
        return STATUS_NOT_SUPPORTED;
    }
    
    /* Write to extended configuration space */
    BytesWritten = HalSetBusDataByOffset(PCIConfiguration,
                                         DeviceExtension->PciDevice->BusNumber,
                                         DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                         Buffer,
                                         Offset,
                                         Length);
    
    if (BytesWritten != Length)
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    return STATUS_SUCCESS;
}

/* EOF */
