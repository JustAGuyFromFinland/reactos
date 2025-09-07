/*
 * PROJECT:         ReactOS MSI Test Driver
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            drivers/test/msitest/msitest.c
 * PURPOSE:         Test driver for MSI/MSI-X functionality
 * PROGRAMMERS:     ReactOS MSI Implementation Team
 */

#include <ntddk.h>

#define DPRINT(fmt, ...) DbgPrint("MSITEST: " fmt, ##__VA_ARGS__)

/* Test interrupt service routine */
BOOLEAN
NTAPI
MsiTestISR(
    PKINTERRUPT Interrupt,
    PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(ServiceContext);
    
    DPRINT("MSI interrupt received!\n");
    
    /* Always claim the interrupt */
    return TRUE;
}

/* Driver unload routine */
VOID
NTAPI
MsiTestUnload(
    PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DPRINT("MSI test driver unloading\n");
}

/* Test MSI functionality */
VOID
NTAPI
TestMsiFunctionality(VOID)
{
    IO_CONNECT_INTERRUPT_PARAMETERS Parameters;
    NTSTATUS Status;
    
    DPRINT("Testing MSI functionality\n");
    
    /* Initialize connection parameters */
    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Version = CONNECT_MESSAGE_BASED;
    Parameters.MessageBased.PhysicalDeviceObject = NULL; /* Would be actual PDO in real driver */
    Parameters.MessageBased.ConnectionContext.InterruptObject = NULL;
    Parameters.MessageBased.ServiceRoutine = MsiTestISR;
    Parameters.MessageBased.ServiceContext = NULL;
    Parameters.MessageBased.SpinLock = NULL;
    Parameters.MessageBased.SynchronizeIrql = PASSIVE_LEVEL;
    Parameters.MessageBased.FloatingSave = FALSE;
    Parameters.MessageBased.MessageServiceRoutine = NULL;
    Parameters.MessageBased.MessageServiceContext = NULL;
    Parameters.MessageBased.FallBackServiceRoutine = NULL;
    Parameters.MessageBased.FallBackServiceContext = NULL;
    
    /* Try to connect MSI interrupt */
    Status = IoConnectInterruptEx(&Parameters);
    
    if (NT_SUCCESS(Status))
    {
        DPRINT("MSI interrupt connection successful! Status: 0x%08lx\n", Status);
        
        /* In a real driver, we would configure the device here to generate MSI interrupts */
        
        /* Disconnect the interrupt */
        IoDisconnectInterruptEx(&Parameters);
        DPRINT("MSI interrupt disconnected\n");
    }
    else
    {
        DPRINT("MSI interrupt connection failed with status: 0x%08lx\n", Status);
    }
}

/* Driver entry point */
NTSTATUS
NTAPI
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    
    DPRINT("MSI test driver loaded\n");
    
    /* Set up driver object */
    DriverObject->DriverUnload = MsiTestUnload;
    
    /* Test MSI functionality */
    TestMsiFunctionality();
    
    return STATUS_SUCCESS;
}

/* EOF */
