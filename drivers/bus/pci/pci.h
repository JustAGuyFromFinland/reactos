#ifndef _PCI_PCH_
#define _PCI_PCH_

#include <ntifs.h>
#include <cmreslist.h>
#include <ntstrsafe.h>

#define TAG_PCI '0ICP'

//
// PCIe Device Types
//
#define PCIE_DEVICE_TYPE_ENDPOINT           0x0
#define PCIE_DEVICE_TYPE_LEGACY_ENDPOINT    0x1
#define PCIE_DEVICE_TYPE_ROOT_PORT          0x4
#define PCIE_DEVICE_TYPE_UPSTREAM_PORT      0x5
#define PCIE_DEVICE_TYPE_DOWNSTREAM_PORT    0x6
#define PCIE_DEVICE_TYPE_PCIE_TO_PCI_BRIDGE 0x7
#define PCIE_DEVICE_TYPE_PCI_TO_PCIE_BRIDGE 0x8
#define PCIE_DEVICE_TYPE_ROOT_ENDPOINT      0x9
#define PCIE_DEVICE_TYPE_ROOT_EVENT_COLLECTOR 0xA

//
// PCIe Capability Structure Offsets
//
#define PCIE_CAPABILITIES_REGISTER          0x02
#define PCIE_DEVICE_CAPABILITIES_REGISTER   0x04
#define PCIE_DEVICE_CONTROL_REGISTER        0x08
#define PCIE_DEVICE_STATUS_REGISTER         0x0A
#define PCIE_LINK_CAPABILITIES_REGISTER     0x0C
#define PCIE_LINK_CONTROL_REGISTER          0x10
#define PCIE_LINK_STATUS_REGISTER           0x12

//
// PCIe Device Control Register Bits
//
#define PCIE_DEVICE_CONTROL_CORRECTABLE_ERROR_ENABLE   0x0001
#define PCIE_DEVICE_CONTROL_NON_FATAL_ERROR_ENABLE     0x0002
#define PCIE_DEVICE_CONTROL_FATAL_ERROR_ENABLE         0x0004
#define PCIE_DEVICE_CONTROL_UNSUPPORTED_REQUEST_ENABLE 0x0008
#define PCIE_DEVICE_CONTROL_RELAXED_ORDERING_ENABLE    0x0010
#define PCIE_DEVICE_CONTROL_MAX_PAYLOAD_SIZE_MASK      0x00E0
#define PCIE_DEVICE_CONTROL_EXTENDED_TAG_ENABLE        0x0100
#define PCIE_DEVICE_CONTROL_PHANTOM_FUNCTIONS_ENABLE   0x0200
#define PCIE_DEVICE_CONTROL_AUX_POWER_PM_ENABLE        0x0400
#define PCIE_DEVICE_CONTROL_NO_SNOOP_ENABLE           0x0800

//
// PCIe Link Control Register Bits
//
#define PCIE_LINK_CONTROL_ASPM_L0S_ENABLE              0x0001
#define PCIE_LINK_CONTROL_ASPM_L1_ENABLE               0x0002
#define PCIE_LINK_CONTROL_RCB                         0x0008
#define PCIE_LINK_CONTROL_DISABLE_LINK                0x0010
#define PCIE_LINK_CONTROL_RETRAIN_LINK                0x0020
#define PCIE_LINK_CONTROL_COMMON_CLOCK_CONFIG         0x0040
#define PCIE_LINK_CONTROL_EXTENDED_SYNC               0x0080

typedef struct _PCI_DEVICE
{
    // Entry on device list
    LIST_ENTRY ListEntry;
    // Physical Device Object of device
    PDEVICE_OBJECT Pdo;
    // PCI bus number
    ULONG BusNumber;
    // PCI slot number
    PCI_SLOT_NUMBER SlotNumber;
    // PCI configuration data
    PCI_COMMON_CONFIG PciConfig;
    // Enable memory space
    BOOLEAN EnableMemorySpace;
    // Enable I/O space
    BOOLEAN EnableIoSpace;
    // Enable bus master
    BOOLEAN EnableBusMaster;
    // Whether the device is owned by the KD
    BOOLEAN IsDebuggingDevice;
    // MSI support fields
    BOOLEAN SupportsMsi;
    BOOLEAN SupportsMsiX;
    UCHAR MsiCapabilityOffset;
    UCHAR MsiXCapabilityOffset;
    // PCIe support fields
    BOOLEAN IsPciExpress;
    UCHAR PciExpressCapabilityOffset;
    USHORT PciExpressCapabilities;
    USHORT PciExpressDeviceControl;
    USHORT PciExpressLinkControl;
    UCHAR PciExpressVersion;
    UCHAR DeviceType;
    BOOLEAN SupportsAer;
    UCHAR AerCapabilityOffset;
    BOOLEAN SupportsPowerManagement;
    UCHAR PowerManagementCapabilityOffset;
    BOOLEAN SupportsHotPlug;
    BOOLEAN SupportsLinkStateManagement;
} PCI_DEVICE, *PPCI_DEVICE;


typedef enum
{
    dsStopped,
    dsStarted,
    dsPaused,
    dsRemoved,
    dsSurpriseRemoved
} PCI_DEVICE_STATE;


typedef struct _COMMON_DEVICE_EXTENSION
{
    // Pointer to device object, this device extension is associated with
    PDEVICE_OBJECT DeviceObject;
    // Wether this device extension is for an FDO or PDO
    BOOLEAN IsFDO;
    // Wether the device is removed
    BOOLEAN Removed;
    // Current device power state for the device
    DEVICE_POWER_STATE DevicePowerState;
} COMMON_DEVICE_EXTENSION, *PCOMMON_DEVICE_EXTENSION;

/* Physical Device Object device extension for a child device */
typedef struct _PDO_DEVICE_EXTENSION
{
    // Common device data
    COMMON_DEVICE_EXTENSION Common;
    // Functional device object
    PDEVICE_OBJECT Fdo;
    // Pointer to PCI Device informations
    PPCI_DEVICE PciDevice;
    // Device ID
    UNICODE_STRING DeviceID;
    // Instance ID
    UNICODE_STRING InstanceID;
    // Hardware IDs
    UNICODE_STRING HardwareIDs;
    // Compatible IDs
    UNICODE_STRING CompatibleIDs;
    // Textual description of device
    UNICODE_STRING DeviceDescription;
    // Textual description of device location
    UNICODE_STRING DeviceLocation;
    // Number of interfaces references
    LONG References;
} PDO_DEVICE_EXTENSION, *PPDO_DEVICE_EXTENSION;

/* Functional Device Object device extension for the PCI driver device object */
typedef struct _FDO_DEVICE_EXTENSION
{
    // Common device data
    COMMON_DEVICE_EXTENSION Common;
    // Entry on device list
    LIST_ENTRY ListEntry;
    // PCI bus number serviced by this FDO
    ULONG BusNumber;
    // Current state of the driver
    PCI_DEVICE_STATE State;
    // Namespace device list
    LIST_ENTRY DeviceListHead;
    // Number of (not removed) devices in device list
    ULONG DeviceListCount;
    // Lock for namespace device list
    KSPIN_LOCK DeviceListLock;
    // Lower device object
    PDEVICE_OBJECT Ldo;
} FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;


/* Driver extension associated with PCI driver */
typedef struct _PCI_DRIVER_EXTENSION
{
    //
    LIST_ENTRY BusListHead;
    // Lock for namespace bus list
    KSPIN_LOCK BusListLock;
} PCI_DRIVER_EXTENSION, *PPCI_DRIVER_EXTENSION;

typedef union _PCI_TYPE1_CFG_CYCLE_BITS
{
    struct
    {
        ULONG InUse:2;
        ULONG RegisterNumber:6;
        ULONG FunctionNumber:3;
        ULONG DeviceNumber:5;
        ULONG BusNumber:8;
        ULONG Reserved2:8;
    };
    ULONG AsULONG;
} PCI_TYPE1_CFG_CYCLE_BITS, *PPCI_TYPE1_CFG_CYCLE_BITS;

/* We need a global variable to get the driver extension,
 * because at least InterfacePciDevicePresent has no
 * other way to get it... */
extern PPCI_DRIVER_EXTENSION DriverExtension;

extern BOOLEAN HasDebuggingDevice;
extern PCI_TYPE1_CFG_CYCLE_BITS PciDebuggingDevice[2];

/* fdo.c */

NTSTATUS
FdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

NTSTATUS
FdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

/* pci.c */

NTSTATUS
PciCreateDeviceIDString(
    PUNICODE_STRING DeviceID,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateInstanceIDString(
    PUNICODE_STRING InstanceID,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateHardwareIDsString(
    PUNICODE_STRING HardwareIDs,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateCompatibleIDsString(
    PUNICODE_STRING HardwareIDs,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateDeviceDescriptionString(
    PUNICODE_STRING DeviceDescription,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateDeviceLocationString(
    PUNICODE_STRING DeviceLocation,
    PPCI_DEVICE Device);

NTSTATUS
PciDuplicateUnicodeString(
    IN ULONG Flags,
    IN PCUNICODE_STRING SourceString,
    OUT PUNICODE_STRING DestinationString);

/* pdo.c */

NTSTATUS
PdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

NTSTATUS
PdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

NTSTATUS
PciEnableMsiInterrupts(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Vector,
    IN PKSERVICE_ROUTINE ServiceRoutine,
    IN PVOID ServiceContext,
    OUT PKINTERRUPT *InterruptObject);

VOID
PciDisableMsiInterrupts(
    IN PKINTERRUPT InterruptObject);

BOOLEAN
PciIsPciExpressDevice(
    IN PDEVICE_OBJECT PhysicalDeviceObject);

NTSTATUS
PciGetPciExpressCapabilities(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PUSHORT Capabilities,
    OUT PUCHAR DeviceType);

NTSTATUS
PciReadExtendedConfig(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Offset,
    OUT PVOID Buffer,
    IN ULONG Length);

NTSTATUS
PciWriteExtendedConfig(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN ULONG Offset,
    IN PVOID Buffer,
    IN ULONG Length);


CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath);

#endif /* _PCI_PCH_ */
