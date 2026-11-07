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
  // two masters, 0x20 and 0x21.  Upstream sc7180 describes the wcn3990 as
  // 0xc0/0xc1 for the data pipes and 0xc2/0xc3 for firmware access, with TZ
  // doing the mapping on TZ targets.  This board's SMMU has neither pair, so
  // bind the whole block.
  //
  // Explicit slots, not a scan: build 444 showed the 0x20 entry missing while
  // 0xc0 sat exactly where 0x20 would have gone, which is what a dropped
  // store followed by a re-scan looks like.  Slots 12..14 are free in the
  // table XBL leaves behind (0..7 and a few high ones are taken).
  ArmSmmuBindStreamAtSlot (12, 0x20, 1, 61);
  ArmSmmuBindStreamAtSlot (13, 0xc0, 1, 61);
  ArmSmmuBindStreamAtSlot (14, 0xc2, 1, 61);

  // Probe: stream 0x7a5 is used by nothing on this board.  If this entry is
  // in the table when XNU dumps it, SEC's SMMU writes land; if it is not,
  // nothing this library wrote ever reached the hardware and the wlan
  // streams have to be matched some other way.
  ArmSmmuBindStreamAtSlot (15, 0x7a5, 0, 61);

  // A matched stream routed to a bank whose SCTLR.M is still set would walk
  // page tables nobody wrote, so make bank 61 a plain bypass bank first.
  ArmSmmuForceBypassBank (61);

  // Safety net for any master this firmware did not anticipate: unmatched
  // streams pass through untranslated instead of faulting.
  ArmSmmuAllowUnmatchedStreams ();
}
