/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/include/msi.h
 * PURPOSE:         MSI/MSI-X Interrupt Support Declarations
 * PROGRAMMERS:     ReactOS MSI Implementation Team
 */

#pragma once

/* MSI/MSI-X PCI Capability IDs */
#define PCI_CAPABILITY_ID_MSI           0x05
#define PCI_CAPABILITY_ID_MSIX          0x11

/* MSI Control Register Bits */
#define MSI_CONTROL_ENABLE              0x0001
#define MSI_CONTROL_64BIT_CAPABLE       0x0080
#define MSI_CONTROL_MULTIPLE_MESSAGE    0x0E00
#define MSI_CONTROL_MULTIPLE_ENABLE     0x0070

/* MSI-X Control Register Bits */
#define MSIX_CONTROL_ENABLE             0x8000
#define MSIX_CONTROL_FUNCTION_MASK      0x4000

/* MSI Address Fields */
#define MSI_ADDRESS_BASE                0xFEE00000
#define MSI_ADDRESS_DEST_ID_MASK        0x00FF0000
#define MSI_ADDRESS_DEST_ID_SHIFT       12
#define MSI_ADDRESS_DEST_MODE_PHYSICAL  0x00000000
#define MSI_ADDRESS_DEST_MODE_LOGICAL   0x00000004

/* MSI Data Fields */
#define MSI_DATA_VECTOR_MASK            0x00FF
#define MSI_DATA_DELIVERY_FIXED         0x0000
#define MSI_DATA_DELIVERY_LOWPRI        0x0100
#define MSI_DATA_TRIGGER_EDGE           0x0000
#define MSI_DATA_TRIGGER_LEVEL          0x8000

/* Function Declarations */

/* HAL MSI Functions */
CODE_SEG("INIT")
VOID
NTAPI
HalpInitializeMsiSupport(VOID);

NTSTATUS
NTAPI
HalpConnectMsiInterrupt(
    IN ULONG Vector,
    IN PKSERVICE_ROUTINE ServiceRoutine,
    IN PVOID ServiceContext,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    OUT PKINTERRUPT *InterruptObject);

VOID
NTAPI
HalpDisconnectMsiInterrupt(
    IN PKINTERRUPT InterruptObject);

NTSTATUS
NTAPI
HalpEnablePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN ULONG MessageCount,
    OUT PULONG Vectors);

VOID
NTAPI
HalpDisablePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber);

/* Internal Helper Functions */
UCHAR
NTAPI
HalpFindPciCapability(
    IN PPCI_COMMON_CONFIG PciConfig,
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityId);

NTSTATUS
NTAPI
HalpConfigurePciMsi(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN ULONG MessageCount,
    OUT PULONG Vectors);

NTSTATUS
NTAPI
HalpConfigurePciMsiX(
    IN ULONG BusNumber,
    IN ULONG SlotNumber,
    IN UCHAR CapabilityOffset,
    IN ULONG MessageCount,
    OUT PULONG Vectors);

ULONG
NTAPI
HalpAllocateVector(VOID);

/* EOF */
