#include "precomp.h"

#define NDEBUG
#include <debug.h>

//
// ACPI Device Notification Handler
//
VOID
AcpiDeviceNotificationHandler(
    ACPI_HANDLE Device,
    UINT32 NotifyValue,
    VOID *Context)
{
  PPDO_DEVICE_DATA DeviceData = (PPDO_DEVICE_DATA)Context;
  PLIST_ENTRY Entry;
  PACPI_NOTIFICATION_HANDLER_ENTRY HandlerEntry;
  PACPI_NOTIFICATION_HANDLER_ENTRY *HandlersArray = NULL;
  ULONG HandlerCount = 0;
  ULONG i;
  KIRQL OldIrql;

  if (!DeviceData)
  {
    return;
  }

  /* First pass: count handlers and allocate array */
  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  
  Entry = DeviceData->NotificationHandlers.Flink;
  while (Entry != &DeviceData->NotificationHandlers)
  {
    HandlerCount++;
    Entry = Entry->Flink;
  }
  
  if (HandlerCount == 0)
  {
    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
    return;
  }

  /* Allocate array for handlers (using NonPagedPool since we're at DISPATCH_LEVEL) */
  HandlersArray = ExAllocatePoolWithTag(NonPagedPool, 
                                        HandlerCount * sizeof(PACPI_NOTIFICATION_HANDLER_ENTRY), 
                                        'AcpH');
  if (!HandlersArray)
  {
    KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
    return;
  }

  /* Second pass: copy handler pointers to array */
  i = 0;
  Entry = DeviceData->NotificationHandlers.Flink;
  while (Entry != &DeviceData->NotificationHandlers && i < HandlerCount)
  {
    HandlerEntry = CONTAINING_RECORD(Entry, ACPI_NOTIFICATION_HANDLER_ENTRY, ListEntry);
    HandlersArray[i] = HandlerEntry;
    i++;
    Entry = Entry->Flink;
  }
  
  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  /* Call handlers outside the spinlock */
  for (i = 0; i < HandlerCount; i++)
  {
    if (HandlersArray[i] && HandlersArray[i]->NotificationHandler)
    {
      HandlersArray[i]->NotificationHandler(HandlersArray[i]->NotificationContext, NotifyValue);
    }
  }
  
  /* Free the temporary array */
  ExFreePoolWithTag(HandlersArray, 'AcpH');
}

VOID
NTAPI
AcpiInterfaceReference(PVOID Context)
{
  UNIMPLEMENTED;
}

VOID
NTAPI
AcpiInterfaceDereference(PVOID Context)
{
  UNIMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceConnectVector(PDEVICE_OBJECT Context,
                           ULONG GpeNumber,
                           KINTERRUPT_MODE Mode,
                           BOOLEAN Shareable,
                           PGPE_SERVICE_ROUTINE ServiceRoutine,
                           PVOID ServiceContext,
                           PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceDisconnectVector(PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceEnableEvent(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceDisableEvent(PDEVICE_OBJECT Context,
                          PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceClearStatus(PDEVICE_OBJECT Context,
                         PVOID ObjectContext)
{
  UNIMPLEMENTED;

  return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
AcpiInterfaceNotificationsRegister(PDEVICE_OBJECT Context,
                                   PDEVICE_NOTIFY_CALLBACK NotificationHandler,
                                   PVOID NotificationContext)
{
  PPDO_DEVICE_DATA DeviceData;
  PACPI_NOTIFICATION_HANDLER_ENTRY HandlerEntry;
  ACPI_STATUS AcpiStatus;
  BOOLEAN FirstHandler = FALSE;
  KIRQL OldIrql;

  if (!Context || !NotificationHandler)
  {
    return STATUS_INVALID_PARAMETER;
  }

  DeviceData = (PPDO_DEVICE_DATA)Context->DeviceExtension;
  if (!DeviceData)
  {
    return STATUS_INVALID_PARAMETER;
  }

  /* Allocate handler entry */
  HandlerEntry = ExAllocatePoolWithTag(NonPagedPool, 
                                        sizeof(ACPI_NOTIFICATION_HANDLER_ENTRY), 
                                        'AcpN');
  if (!HandlerEntry)
  {
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  HandlerEntry->NotificationHandler = NotificationHandler;
  HandlerEntry->NotificationContext = NotificationContext;

  /* Add to the device's notification handler list */
  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  
  /* Check if this is the first handler */
  FirstHandler = IsListEmpty(&DeviceData->NotificationHandlers);
  
  InsertTailList(&DeviceData->NotificationHandlers, &HandlerEntry->ListEntry);
  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  /* Register ACPI notify handler only for the first handler */
  if (FirstHandler && DeviceData->AcpiHandle)
  {
    AcpiStatus = AcpiInstallNotifyHandler(DeviceData->AcpiHandle,
                                          ACPI_ALL_NOTIFY,
                                          AcpiDeviceNotificationHandler,
                                          DeviceData);
    if (ACPI_FAILURE(AcpiStatus))
    {
      /* Remove from list and free on failure */
      KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
      RemoveEntryList(&HandlerEntry->ListEntry);
      KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);
      ExFreePoolWithTag(HandlerEntry, 'AcpN');
      
      return STATUS_UNSUCCESSFUL;
    }
  }

  return STATUS_SUCCESS;
}

VOID
NTAPI
AcpiInterfaceNotificationsUnregister(PDEVICE_OBJECT Context,
                                     PDEVICE_NOTIFY_CALLBACK NotificationHandler)
{
  PPDO_DEVICE_DATA DeviceData;
  PLIST_ENTRY Entry;
  PACPI_NOTIFICATION_HANDLER_ENTRY HandlerEntry;
  BOOLEAN Found = FALSE;
  BOOLEAN IsEmpty = FALSE;
  KIRQL OldIrql;

  if (!Context || !NotificationHandler)
  {
    return;
  }

  DeviceData = (PPDO_DEVICE_DATA)Context->DeviceExtension;
  if (!DeviceData)
  {
    return;
  }

  /* Find and remove the handler from the list */
  KeAcquireSpinLock(&DeviceData->NotificationLock, &OldIrql);
  
  Entry = DeviceData->NotificationHandlers.Flink;
  while (Entry != &DeviceData->NotificationHandlers)
  {
    HandlerEntry = CONTAINING_RECORD(Entry, ACPI_NOTIFICATION_HANDLER_ENTRY, ListEntry);
    
    if (HandlerEntry->NotificationHandler == NotificationHandler)
    {
      RemoveEntryList(&HandlerEntry->ListEntry);
      ExFreePoolWithTag(HandlerEntry, 'AcpN');
      Found = TRUE;
      break;
    }
    
    Entry = Entry->Flink;
  }
  
  /* Check if this was the last handler */
  IsEmpty = IsListEmpty(&DeviceData->NotificationHandlers);
  
  KeReleaseSpinLock(&DeviceData->NotificationLock, OldIrql);

  /* If no more handlers and we have an ACPI handle, unregister ACPI notify handler */
  if (Found && IsEmpty && DeviceData->AcpiHandle)
  {
    AcpiRemoveNotifyHandler(DeviceData->AcpiHandle,
                            ACPI_ALL_NOTIFY,
                            AcpiDeviceNotificationHandler);
  }
}

NTSTATUS
Bus_PDO_QueryInterface(PPDO_DEVICE_DATA DeviceData,
                       PIRP Irp)
{
  PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
  PACPI_INTERFACE_STANDARD AcpiInterface;

  if (IrpSp->Parameters.QueryInterface.Version != 1)
  {
      DPRINT1("Invalid version number: %d\n",
              IrpSp->Parameters.QueryInterface.Version);
      return STATUS_INVALID_PARAMETER;
  }

  if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                        &GUID_ACPI_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
  {
      DPRINT("GUID_ACPI_INTERFACE_STANDARD\n");

      if (IrpSp->Parameters.QueryInterface.Size < sizeof(ACPI_INTERFACE_STANDARD))
      {
          DPRINT1("Buffer too small! (%d)\n", IrpSp->Parameters.QueryInterface.Size);
          return STATUS_BUFFER_TOO_SMALL;
      }

     AcpiInterface = (PACPI_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;

     AcpiInterface->InterfaceReference = AcpiInterfaceReference;
     AcpiInterface->InterfaceDereference = AcpiInterfaceDereference;
     AcpiInterface->GpeConnectVector = AcpiInterfaceConnectVector;
     AcpiInterface->GpeDisconnectVector = AcpiInterfaceDisconnectVector;
     AcpiInterface->GpeEnableEvent = AcpiInterfaceEnableEvent;
     AcpiInterface->GpeDisableEvent = AcpiInterfaceDisableEvent;
     AcpiInterface->GpeClearStatus = AcpiInterfaceClearStatus;
     AcpiInterface->RegisterForDeviceNotifications = AcpiInterfaceNotificationsRegister;
     AcpiInterface->UnregisterForDeviceNotifications = AcpiInterfaceNotificationsUnregister;

     return STATUS_SUCCESS;
  }
  else
  {
      DPRINT1("Invalid GUID\n");
      return STATUS_NOT_SUPPORTED;
  }
}
