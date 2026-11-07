/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/ArmSmmuDetachLib.h>

#include "PlatformRegisters.h"

VOID
DisableWatchDogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR QTimerRegion;

  // Locate "QTIMER" Memory Region
  Status = LocateMemoryRegionByName ("QTIMER", &QTimerRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disable WatchDog Timer
  MmioWrite32 (QTimerRegion.Address + APSS_WDT_OFFSET + APSS_WDT_ENABLE_OFFSET, 0x0);
}

VOID
PlatformInitialize ()
{
  // Disable WatchDog Timer
  DisableWatchDogTimer ();

  // The wlan copy engines have no stream-match entry: the device tree
  // attaches the wlan node to the apps SMMU with SID 0xc0 (mask 1) and the
  // network stack probes SID 0x20, but XBL leaves both unmatched and the
  // kernel cannot add an entry (this SMMU's registers are policy-locked
  // once the OS runs - only the console's 0x800000e0 store is accepted).
  // With sCR0.USFCFG set, the first CE DMA faults the whole machine.
  // Attach both streams to the console's bypass context bank (61) now,
  // while the registers are still writable from here.
  // The device tree gives the icnss node iommus = <&apps_smmu 0x20 0x01>:
  // two masters (0x20 and 0x21).  Mask 0 here covered only 0x20 and the
  // copy engines still faulted their first DMA, so match the tree's mask.
  ArmSmmuAddBypassStream (0x20, 1, 61);
  ArmSmmuAddBypassStream (0xc0, 1, 61);
  // Safety net for any master this firmware did not anticipate: unmatched
  // streams pass through untranslated instead of faulting.
  ArmSmmuAllowUnmatchedStreams ();
}
