// SPDX-License-Identifier: BSD-2-Clause-Patent
// NativeUfs host glue for the Google Tensor G4 (zumapro; Pixel 9 Pro XL "komodo").
//
// Facts: the mainline Linux device tree (arch/arm64/boot/dts/exynos/google/zumapro.dtsi:
// ufs@13200000 reg "hci" "vs_hci" "unipro" "ufsp", phy@13204000, syscon@13020000,
// system-controller@15460000) and the BSD-2-Clause-Patent Mu-Silicium firmware for this SoC
// (Silicon/Samsung/S5P9875Pkg/Library/PlatformUfsLib/BoardInit.c, SamsungPkg/Drivers/UfsDxe).
// The vendor register names follow drivers/ufs/host/ufs-exynos.c (GPL; names and offsets only).
//
// What differs from the SM8550 path: no system MMU in front of the host (the UFS block has no
// iommus and the HSI2 SYSREG marks its traffic shareable), no clock controller to consult (the
// firmware leaves every clock running), one PRDT entry per 4096-byte data unit, and the
// nexus-type register that has to say whether the request about to ring is a SCSI command.
#ifndef NATIVE_UFS_EXYNOS_HOST_H
#define NATIVE_UFS_EXYNOS_HOST_H

#define UFS_HC_BASE        0x13200000ULL   // UFSHCI standard registers ("hci")
#define UFS_HC_SIZE        0x200ULL
#define UFS_VS_BASE        0x13201100ULL   // vendor HCI registers ("vs_hci")
#define UFS_VS_SIZE        0x2000ULL
#define UFS_UNIPRO_BASE    0x13280000ULL   // UniPro debug APB ("unipro")
#define UFS_UNIPRO_SIZE    0x8000ULL
#define UFS_PMA_BASE       0x13204000ULL   // M-PHY PMA ("phy-pma")
#define UFS_PMA_SIZE       0x4000ULL
#define UFS_PMU_BASE       0x15460000ULL   // PMU_ALIVE
#define UFS_PMU_PHY_CTRL   0x3ec0U         // UFS_PHY_CONTROL: bit 0 = isolation bypassed, PHY powered
#define UFS_SYSREG_BASE    0x13020000ULL   // HSI2 SYSREG
#define UFS_SYSREG_IOCC    0x710U          // bits 0-1: UFS reads/writes shareable (I/O coherent)
#define UFS_MCLK_HZ        200000000U      // sclk_unipro_main as the firmware programs it

static const char kUfsHostCompatible[] = "google,zuma-ufs";
#ifndef NATIVE_UFS_ROOT_PARTITION
#define NATIVE_UFS_ROOT_PARTITION "darwin"
#endif
static const char kUfsRootPartition[] = NATIVE_UFS_ROOT_PARTITION;
// HCI_TXPRDT_ENTRY_SIZE / HCI_RXPRDT_ENTRY_SIZE are 12 (log2 4096): one PRDT entry per data unit.
static constexpr uint32_t kUfsPrdEntryMax = 4096;

// Vendor HCI registers, offsets from UFS_VS_BASE.
enum : unsigned {
  kVsTxPrdtEntrySize = 0x00,
  kVsRxPrdtEntrySize = 0x04,
  kVs1usToCnt        = 0x0c,
  kVsIs              = 0x38,   // bit 20: UFS_IDLE indicator, set after a link reset
  kVsUtrlNexusType   = 0x40,   // bit n: request slot n carries a SCSI command (a "nexus" request)
  kVsUtmrlNexusType  = 0x44,
  kVsSwRst           = 0x50,   // bits 0-1: link and UniPro software reset
  kVsDataReorder     = 0x60,
  kVsApbClkCtrl      = 0x68,
  kVsAxiDmaBurstLen  = 0x6c,
  kVsGpioOut         = 0x70,   // bit 0: device RESET_n
  kVsV2p1Ctrl        = 0x8c,   // bit 16: IA_TICK_SEL
  kVsClkStopCtrl     = 0xb0,   // bit 4: REFCLKOUT stop
  kVsMisc            = 0xb4,   // clock control enables ("FORCE_HCS" in the firmware)
  kVsAcgDisable      = 0xfc,   // bit 0: automatic clock gating disabled (firmware name UFS_ACG_DISABLE)
  kVsIopAcgDisable   = 0x100,
  kVsMphyRefclkSel   = 0x108,
};

static OSSharedPtr<IOMemoryMap> gExynosVsMap;
static uintptr_t gExynosVs = 0;

static bool exynosMapVendor (void)
{
  if (!gExynosVs) {
    gExynosVs = mapPhys (UFS_VS_BASE, UFS_VS_SIZE, gExynosVsMap);
  }
  return gExynosVs != 0;
}

// native-ufs-probe=2: what the firmware left, standard and vendor registers. Nothing is written.
static void hostPlatformInspect (void)
{
  OSSharedPtr<IOMemoryMap> hcMap;
  uintptr_t hc = mapPhys (UFS_HC_BASE, UFS_HC_SIZE, hcMap);
  if (!hc || !exynosMapVendor ()) {
    panic ("NATIVE_UFS inspect: map failed hc=%p vs=%p", (void *) hc, (void *) gExynosVs);
  }
  panic ("NATIVE_UFS inspect (exynos)\n"
         "cap=%08x ver=%08x is=%08x ie=%08x\n"
         "hcs=%08x hce=%08x utrlba=%08x_%08x db=%08x rsr=%08x\n"
         "utmrlba=%08x rsr=%08x uecpa=%08x uecdl=%08x\n"
         "vs is=%08x nexus=%08x/%08x swrst=%08x gpio=%08x\n"
         "vs clkstop=%08x misc=%08x acg=%08x prdt=%u/%u 1us=%u",
         read32 (hc, 0x00), read32 (hc, 0x08), read32 (hc, 0x20), read32 (hc, 0x24),
         read32 (hc, 0x30), read32 (hc, 0x34), read32 (hc, 0x54), read32 (hc, 0x50), read32 (hc, 0x58), read32 (hc, 0x60),
         read32 (hc, 0x70), read32 (hc, 0x80), read32 (hc, 0x38), read32 (hc, 0x3c),
         read32 (gExynosVs, kVsIs), read32 (gExynosVs, kVsUtrlNexusType), read32 (gExynosVs, kVsUtmrlNexusType),
         read32 (gExynosVs, kVsSwRst), read32 (gExynosVs, kVsGpioOut),
         read32 (gExynosVs, kVsClkStopCtrl), read32 (gExynosVs, kVsMisc), read32 (gExynosVs, kVsAcgDisable),
         read32 (gExynosVs, kVsTxPrdtEntrySize), read32 (gExynosVs, kVsRxPrdtEntrySize), read32 (gExynosVs, kVs1usToCnt));
}

static void hostPlatformInspectSmmu (void)
{
  panic ("NATIVE_UFS smmu: the Tensor UFS host has no system MMU in its path; probe mode 7 does not apply");
}

static void hostPlatformInspectContextBanks (void)
{
  panic ("NATIVE_UFS cb: the Tensor UFS host has no system MMU in its path; probe mode 8 does not apply");
}

// DMA from the host reaches physical memory directly (no IOMMU; the SYSREG marks it coherent).
// Only the vendor window is needed, for the nexus-type writes that follow.
static void hostPlatformPrepareDma (OSSharedPtr<IOMemoryMap> &, uint32_t report[3], uint64_t, uint64_t)
{
  report[0] = report[1] = report[2] = 0;
  if (!exynosMapVendor ()) {
    panic ("NATIVE_UFS exynos: vendor register map failed");
  }
}

static void hostPlatformBindFaultRegisters (OSSharedPtr<IOMemoryMap> &, const uint32_t[3], uintptr_t &smmu, uintptr_t &cb)
{
  smmu = 0;
  cb = 0;
}

// The host must know, before the doorbell, whether slot 0 carries a SCSI command (nexus request) or a
// NOP OUT / QUERY (no nexus). Linux writes the slot's bit in setup_xfer_req; the firmware writes all ones
// or zero before each command. Slot 0 is the only slot this driver uses.
static inline void hostPlatformBeforeSubmit (bool scsiCommand)
{
  if (gExynosVs) {
    write32 (gExynosVs, kVsUtrlNexusType, scsiCommand ? 1U : 0U);
    __asm__ volatile ("dsb sy" ::: "memory");
  }
}

// hostPlatformAfterNop lives in ExynosUfsPower.h: it needs the command layer, so NativeUfs.cpp includes it
// right after RootCommands.h, ahead of hostBringUp.

#include "ExynosUfsLink.h"
#endif
