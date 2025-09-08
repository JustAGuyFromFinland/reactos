/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm/generic/sysinfo.c
 * PURPOSE:         HAL Information Routines
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HaliQuerySystemInformation(IN HAL_QUERY_INFORMATION_CLASS InformationClass,
                           IN ULONG BufferSize,
                           IN OUT PVOID Buffer,
                           OUT PULONG ReturnedLength)
{
    /* No ARM-specific info classes implemented yet.
     * Return STATUS_NOT_IMPLEMENTED without hitting UNIMPLEMENTED or looping. */
    if (ReturnedLength)
        *ReturnedLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HaliSetSystemInformation(IN HAL_SET_INFORMATION_CLASS InformationClass,
                         IN ULONG BufferSize,
                         IN OUT PVOID Buffer)
{
    /* No ARM-specific set-info implemented. Return a standard not-implemented status. */
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
