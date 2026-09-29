// StringLoader.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h"

/**
 * DLL entry point. No per-process initialisation is required.
 *
 * @param hModule            Module instance handle.
 * @param ul_reason_for_call Reason the entry point is invoked (DLL_PROCESS_*).
 * @param lpReserved         Reserved loader context.
 *
 * @return TRUE.
 */
BOOL APIENTRY
DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}
