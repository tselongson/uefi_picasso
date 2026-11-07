/**
  Portion of this Code is Written by Referencing this:
  linux/drivers/iommu/arm/arm-smmu/arm-smmu.c

  Copyright (c) 2024-2025. Project Aloha Authors. All rights reserved.
  Copyright (c) 2022-2025. DuoWoA Authors.
  Copyright (c) 2023-2026. Project Silicium Authors.

  SPDX-License-Identifier: MIT
*/

#include <Library/ArmSmmuDetachLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/TimerLib.h>

#include "SmmuV2.h"

//
// Global Variables
//
STATIC ARM_SMMU_V2_DEVICE SmmuDevice;

VOID
ArmSmmuDisableContextBank (IN UINT32 ContextBankIndex)
{
  UINT32 FaultStatus;

  // Clear Page Table Base Addresses
  ArmSmmuCbWrite64 (&SmmuDevice, ContextBankIndex, ARM_SMMU_CB_TTBR0, 0);
  ArmSmmuCbWrite64 (&SmmuDevice, ContextBankIndex, ARM_SMMU_CB_TTBR1, 0);

  // Clear CB_TCR
  ArmSmmuCbWrite (&SmmuDevice, ContextBankIndex, ARM_SMMU_CB_TCR, 0);

  // Get Fault Status
  FaultStatus = ArmSmmuCbRead (&SmmuDevice, ContextBankIndex, ARM_SMMU_CB_FSR);

  // Clear Fault Status
  if (FaultStatus) {
    ArmSmmuCbWrite (&SmmuDevice, ContextBankIndex, ARM_SMMU_CB_FSR, FaultStatus);
  }
}

VOID
ArmSmmuFreeContextBank (IN UINT32 ContextBankIndex)
{
  UINT32 Cbar;
  UINT32 CbarType;
  UINT32 CbarVmid;

  // Disable Context Bank
  ArmSmmuDisableContextBank (ContextBankIndex);

  // Get GR1_CBAR
  Cbar = ArmSmmuGr1Read (&SmmuDevice, ARM_SMMU_GR1_CBAR (ContextBankIndex));

  // Set CBAR_TYPE
  CbarType  = (Cbar >> CBAR_TYPE_SHIFT);
  CbarType &= CBAR_TYPE_MASK;

  // Set CBAR_VMID
  CbarVmid  = (Cbar >> CBAR_VMID_SHIFT);
  CbarVmid &= CBAR_VMID_MASK;

  // Verify CBAR_TYPE & CBAR_VMID
  if (CbarType != CBAR_TYPE_S2_TRANS && CbarType != CBAR_TYPE_S1_TRANS_S2_FAULT && (CbarType != CBAR_TYPE_S1_TRANS_S2_BYPASS || CbarVmid != 0xFF)) {
    // Set CBAR
    Cbar  = ((CBAR_TYPE_S1_TRANS_S2_BYPASS & CBAR_TYPE_MASK) << CBAR_TYPE_SHIFT);
    Cbar |= ((0xFF & CBAR_VMID_MASK) << CBAR_VMID_SHIFT);

    // Write GR1_CBAR
    ArmSmmuGr1Write (&SmmuDevice, ARM_SMMU_GR1_CBAR (ContextBankIndex), Cbar);
  }
}

BOOLEAN
ArmSmmuSkipStream (
  IN UINT32        StreamMask,
  IN UINT32        MaskedStreamId,
  IN CONST UINT16 *SkipStreams,
  IN UINT8         SkipStreamCount)
{
  // Verify Skip Strams
  if (SkipStreams == NULL) {
    return FALSE;
  }

  // Loop thru all Skip Strams
  for (UINTN i = 0; i < SkipStreamCount; i++) {
    // Verify Stream ID
    if ((SkipStreams[i] & ~StreamMask) == MaskedStreamId) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
ArmSmmuDetachAll (
  IN CONST UINT16 *SkipStreams,
  IN UINT8         SkipStreamCount)
{
  UINT32 ContextBank[16];
  UINT32 NumContextBanks = 0;

  // Get GR0_ID0
  UINT32 Id0 = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_ID0);

  // Get the Number of SMRGs
  UINT32 NumSmrg = (Id0 >> 0) & 0xFF;

  // Go thru each SMRG
  for (UINT32 i = 0; i < NumSmrg; i++) {
    UINT32 ContextBankIndex;
    UINT32 MaskedSmrId;
    UINT32 SmrMask;
    UINT32 SmrId;
    UINT32 S2cr;
    UINT32 Smr;

    // Get GR0_SMR
    Smr = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_SMR (i));

    // Verify SMR
    if (!(Smr & SMR_VALID)) {
      continue;
    }

    // Get GR0_S2CR
    S2cr = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_S2CR (i));

    // Extract Context Bank index and type
    ContextBankIndex = (S2cr >> S2CR_CBNDX_SHIFT) & S2CR_CBNDX_MASK;

    // Extract Stream ID and Mask
    SmrId   = (Smr >> SMR_ID_SHIFT) & SMR_ID_MASK;
    SmrMask = (Smr >> SMR_MASK_SHIFT) & SMR_ID_MASK;

    // Set Masked SMR ID
    MaskedSmrId = SmrId & ~SmrMask;

    // Verify Stream
    if (ArmSmmuSkipStream (SmrMask, MaskedSmrId, SkipStreams, SkipStreamCount)) {
      BOOLEAN AlreadyTracked = FALSE;

      // Go thru each Contect Bank
      for (UINT32 i = 0; i < NumContextBanks; i++) {
        // Compare Context Bank Index
        if (ContextBank[i] == ContextBankIndex) {
          AlreadyTracked = TRUE;
          break;
        }
      }

      // ?
      if (!AlreadyTracked && NumContextBanks < 16) {
        ContextBank[NumContextBanks++] = ContextBankIndex;
      }

      // Skip this Context Bank
      continue;
    }

    // Set FAULT Mode
    S2cr  = ((S2CR_TYPE_FAULT & S2CR_TYPE_MASK) << S2CR_TYPE_SHIFT);
    S2cr |= ((ContextBankIndex & S2CR_CBNDX_MASK) << S2CR_CBNDX_SHIFT);

    // Write GR0_S2CR
    ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_S2CR (i), S2cr);

    // Invalidate SMR Entry
    Smr &= ~SMR_VALID;
  
    // Write GR0_SMR
    ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_SMR (i), Smr);
  }

  // Invalidate TLB
  ArmInvalidateTlb ();

  // Go thru all Context Banks
  for (UINT32 i = 0; i < SmmuDevice.NumContextBanks; i++) {
    BOOLEAN AlreadyTracked = FALSE;

    // Go thru each Context Bank
    for (UINT32 j = 0; j < NumContextBanks; j++) {
      // Compare Context Bank ID
      if (ContextBank[j] == i) {
        AlreadyTracked = TRUE;
        break;
      }
    }

    // Skip Context Bank
    if (AlreadyTracked) {
      continue;
    }

    // Free Context Bank
    ArmSmmuFreeContextBank (i);
  }
}

VOID
ArmSmmuDetach (
  IN CONST UINT16 *SkipStreams,
  IN UINT8         SkipStreamCount)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SmmuRegion;

  // Locate "SMMU" Memory Region
  Status = LocateMemoryRegionByName ("SMMU", &SmmuRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Clear SMMU Device Variable
  ZeroMem ((VOID *)&SmmuDevice, sizeof (SmmuDevice));

  // Set SMMU Base Address
  SmmuDevice.BaseAddress = SmmuRegion.Address;

  // Get GR0_ID1
  UINT32 Id1 = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_ID1);

  // Set Remaining SMMU Data
  SmmuDevice.PageShift       = (Id1 & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
  SmmuDevice.NumPage         = 1 << ((READ_FIELD (ARM_SMMU_ID1_NUMPAGENDXB, Id1)) + 1);
  SmmuDevice.NumContextBanks = Id1 & ARM_SMMU_ID1_NUMCB_MASK;

  // Detach IOMMU Domains
  ArmSmmuDetachAll (SkipStreams, SkipStreamCount);
}

VOID
ArmSmmuAddBypassStream (
  IN UINT32 StreamId,
  IN UINT32 Mask,
  IN UINT32 ContextBankIndex)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SmmuRegion;

  // Locate "SMMU" Memory Region
  Status = LocateMemoryRegionByName ("SMMU", &SmmuRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Clear SMMU Device Variable
  ZeroMem ((VOID *)&SmmuDevice, sizeof (SmmuDevice));

  // Set SMMU Base Address
  SmmuDevice.BaseAddress = SmmuRegion.Address;

  // Get GR0_ID1
  UINT32 Id1 = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_ID1);

  // Set Remaining SMMU Data
  SmmuDevice.PageShift       = (Id1 & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
  SmmuDevice.NumPage         = 1 << ((READ_FIELD (ARM_SMMU_ID1_NUMPAGENDXB, Id1)) + 1);
  SmmuDevice.NumContextBanks = Id1 & ARM_SMMU_ID1_NUMCB_MASK;

  // Get the Number of SMRGs.  SMMUv2 keeps the stream-match group count
  // in IDR2[25:16] (NUMS2CR, the SMR count equals the S2CR count) - the
  // IDR0 low byte read here before is a feature bitmap, which on picasso
  // made this scan stop before the first free slot and silently bind
  // nothing.
  UINT32 Id2    = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_ID2);
  UINT32 NumSmrg = READ_FIELD (ARM_SMMU_ID2_NUMS2CR, Id2);

  if ((NumSmrg == 0) || (NumSmrg > 128)) {
    NumSmrg = 128;
  }

  // If the stream is already matched, leave it alone
  for (UINT32 i = 0; i < NumSmrg; i++) {
    UINT32 Smr = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_SMR (i));

    if ((Smr & SMR_VALID) && ((Smr & SMR_ID_MASK) == (StreamId & ~(Mask & SMR_ID_MASK)))) {
      return;
    }
  }

  // Find a free SMR slot and route it through the Context Bank first, the
  // order the console path uses (an S2CR store with the pair is known to
  // stick, an SMR-first store may not).  The first stream-match store after
  // power-on is dropped on this silicon (build 444: the 0x20 entry vanished
  // while the later 0xc0 entry stuck, same function, same phase), so write
  // the pair until the read back matches.
  for (UINT32 i = 0; i < NumSmrg; i++) {
    UINT32 Smr = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_SMR (i));
    UINT32 Want;

    if (Smr & SMR_VALID) {
      continue;
    }

    Want = SMR_VALID
      | ((StreamId & SMR_ID_MASK) << SMR_ID_SHIFT)
      | ((Mask & SMR_ID_MASK) << SMR_MASK_SHIFT);

    for (UINT32 attempt = 0; attempt < 8; attempt++) {
      ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_S2CR (i), ContextBankIndex & S2CR_CBNDX_MASK);
      ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_SMR (i), Want);

      // Invalidate TLB
      ArmInvalidateTlb ();

      if (ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_SMR (i)) == Want) {
        return;
      }

      // Store lost before the SMMU clock settled - let it settle and retry.
      MicroSecondDelay (100);
    }
    return;
  }
}

VOID
ArmSmmuAllowUnmatchedStreams (VOID)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SmmuRegion;

  // Locate "SMMU" Memory Region
  Status = LocateMemoryRegionByName ("SMMU", &SmmuRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Clear SMMU Device Variable
  ZeroMem ((VOID *)&SmmuDevice, sizeof (SmmuDevice));

  // Set SMMU Base Address
  SmmuDevice.BaseAddress = SmmuRegion.Address;

  // sCR0.USFCFG set means a stream without a stream-match entry faults.
  // The wlan masters on picasso carry stream 0x20 mask 1 per the device
  // tree, but any master whose stream this firmware did not anticipate
  // would fault its first DMA and wedge the interconnect, so let
  // unmatched streams pass through untranslated instead.  Global registers
  // may refuse stores, so verify and retry.
  for (UINT32 attempt = 0; attempt < 8; attempt++) {
    UINT32 Scr0 = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_sCR0);

    if ((Scr0 & ARM_SMMU_sCR0_USFCFG) == 0) {
      return;
    }

    ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_sCR0, Scr0 & ~ARM_SMMU_sCR0_USFCFG);

    if ((ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_sCR0) & ARM_SMMU_sCR0_USFCFG) == 0) {
      return;
    }

    MicroSecondDelay (100);
  }
}

VOID
ArmSmmuBindStreamAtSlot (
  IN UINT32 Slot,
  IN UINT32 StreamId,
  IN UINT32 Mask,
  IN UINT32 ContextBankIndex)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SmmuRegion;
  UINT32                       Want;

  Status = LocateMemoryRegionByName ("SMMU", &SmmuRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  ZeroMem ((VOID *)&SmmuDevice, sizeof (SmmuDevice));
  SmmuDevice.BaseAddress = SmmuRegion.Address;

  // An explicit slot costs nothing and removes the scan from the equation:
  // when a store is dropped and the next call scans again it lands in the
  // same slot and overwrites the entry written a moment earlier (build 444:
  // the 0x20 entry was missing while 0xc0 sat in the slot 0x20 had taken).
  Want = SMR_VALID
    | ((StreamId & SMR_ID_MASK) << SMR_ID_SHIFT)
    | ((Mask & SMR_ID_MASK) << SMR_MASK_SHIFT);

  for (UINT32 attempt = 0; attempt < 16; attempt++) {
    ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_S2CR (Slot),
      ContextBankIndex & S2CR_CBNDX_MASK);
    ArmSmmuGr0Write (&SmmuDevice, ARM_SMMU_GR0_SMR (Slot), Want);
    ArmInvalidateTlb ();

    if (ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_SMR (Slot)) == Want) {
      return;
    }

    MicroSecondDelay (200);
  }
}

VOID
ArmSmmuForceBypassBank (
  IN UINT32 Bank)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR SmmuRegion;
  UINT32                       Id1;
  UINT32                       Sctlr;

  Status = LocateMemoryRegionByName ("SMMU", &SmmuRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  ZeroMem ((VOID *)&SmmuDevice, sizeof (SmmuDevice));
  SmmuDevice.BaseAddress = SmmuRegion.Address;

  Id1 = ArmSmmuGr0Read (&SmmuDevice, ARM_SMMU_GR0_ID1);
  SmmuDevice.PageShift       = (Id1 & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
  SmmuDevice.NumPage         = 1 << ((READ_FIELD (ARM_SMMU_ID1_NUMPAGENDXB, Id1)) + 1);
  SmmuDevice.NumContextBanks = Id1 & ARM_SMMU_ID1_NUMCB_MASK;

  if (Bank >= SmmuDevice.NumContextBanks) {
    return;
  }

  // If the bank the wlan streams are routed to still has translation on, a
  // stream that matches it would walk page tables nobody wrote and fault
  // anyway.  Clearing SCTLR.M makes it a bypass bank, the state the USB
  // driver runs its own bank in (sctlr=014c2114, M=0).
  // CB_SCTLR is 0x0 in the bank's page, bit 0 is M.  Spelled as literals
  // because SmmuV2.h (the header this file sees) does not define them.
  Sctlr = ArmSmmuCbRead (&SmmuDevice, Bank, 0x0);

  if (Sctlr & 0x1u) {
    ArmSmmuCbWrite (&SmmuDevice, Bank, 0x0, Sctlr & ~0x1u);
    ArmInvalidateTlb ();
  }
}
