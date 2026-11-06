#ifndef _ARM_SMMU_DETACH_LIB_H_
#define _ARM_SMMU_DETACH_LIB_H_

/**
  This Function Detaches all IOMMU Domains from the SMMU.

  @param[in] SkipStreams                   - The SIDs to be Skiped.
  @param[in] SkipStreamCount               - The Number of SIDs to be Skiped.
**/
VOID
ArmSmmuDetach (
  IN CONST UINT16 *SkipStreams,
  IN UINT8         SkipStreamCount
  );

/**
  This Function attaches a Stream ID to a Context Bank in bypass mode by
  adding a new Stream Match entry.  Masters whose firmware left no SMR
  entry behind (e.g. the wlan copy engines on some devices) fault on their
  first DMA when sCR0.USFCFG is set, and the OS may not be able to add the
  entry itself, so it has to happen here.

  @param[in] StreamId                      - The SID to be attached.
  @param[in] Mask                          - The SMR match mask (1 = don't care).
  @param[in] ContextBankIndex              - The Context Bank to route through
                                             (its SCTLR.M=0 makes this bypass).
**/
VOID
ArmSmmuAddBypassStream (
  IN UINT32 StreamId,
  IN UINT32 Mask,
  IN UINT32 ContextBankIndex
  );

#endif /* _ARM_SMMU_DETACH_LIB_H_ */
