#include "am_mcu_apollo.h"

AM_SHARED_RW uint32_t ui32MCUClaimed[AM_HAL_ACCESS_STRUCT_SIZE];
AM_USED const uint32_t ui32MCUAllowed[AM_HAL_ACCESS_STRUCT_SIZE] =
{
    0x00000000,
    0x00000000,
    0x00000000
};

AM_SHARED_RW uint32_t ui32DSP0Claimed[AM_HAL_ACCESS_STRUCT_SIZE];
AM_USED const uint32_t ui32DSP0Allowed[AM_HAL_ACCESS_STRUCT_SIZE] =
{
    0x00000000,
    0x00000000,
    0x00000000
};

AM_SHARED_RW uint32_t ui32DSP1Claimed[AM_HAL_ACCESS_STRUCT_SIZE];
AM_USED const uint32_t ui32DSP1Allowed[AM_HAL_ACCESS_STRUCT_SIZE] =
{
    0x00000000,
    0x00000000,
    0x00000000
};

AM_USED const uint32_t ui32SharedAccess[AM_HAL_ACCESS_STRUCT_SIZE] =
{
    0x00000000,
    0x00000000,
    0x00000000
};

AM_USED const am_hal_access_t sGlobalAccess =
{
    ui32SharedAccess,
    ui32MCUAllowed,
    ui32DSP0Allowed,
    ui32DSP1Allowed,
    ui32MCUClaimed,
    ui32DSP0Claimed,
    ui32DSP1Claimed,
};

AM_RESOURCE_TABLE const am_hal_access_t *psGlobalAccessPtr = &sGlobalAccess;
