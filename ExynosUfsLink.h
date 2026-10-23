// SPDX-License-Identifier: BSD-2-Clause-Patent
// Link startup for the Tensor G4 UFS host, the way the board's firmware does it (Mu-Silicium
// SamsungPkg/Drivers/UfsDxe UfsPreSetup, UfsInitInterface and UfsVendorSetup; S5P9875Pkg
// BoardInit.c; all BSD-2-Clause-Patent). Reached only through native-ufs-probe=15: the ordinary
// path adopts the link the firmware left up and never comes here.
//
// Order: PHY isolation off, I/O coherency, host software reset, device reset, host controller
// enable, vendor clock setup, lane count, pre-link calibration, DME_LINKSTARTUP, gear cap,
// post-link calibration, active lanes, vendor request setup, and empty request lists so the
// host reads as "ready" for the takeover that follows. Device initialisation and the gear change
// need the command layer and live in ExynosUfsPower.h.
#ifndef NATIVE_UFS_EXYNOS_LINK_H
#define NATIVE_UFS_EXYNOS_LINK_H

struct ExynosLinkState {
  OSSharedPtr<IOMemoryMap> hcMap, uniproMap, pmaMap, pmuMap, sysregMap;
  uintptr_t hc = 0, unipro = 0, pma = 0, pmu = 0, sysreg = 0;
  IOBufferMemoryDescriptor *lists = nullptr;   // 8 KiB: an empty transfer list and an empty task list
  uint8_t availableLanes = 0, connectedTxLanes = 0, connectedRxLanes = 0, activeTxLanes = 0, activeRxLanes = 0;
  uint8_t maxGear = 0;
  bool relinked = false;                       // ExynosUfsPower.h finishes device init and the gear change
};
static ExynosLinkState gExynosLink;

// UFSHCI UIC command registers and the completion bits they use.
enum : unsigned { kUicCmd = 0x90, kUicArg1 = 0x94, kUicArg2 = 0x98, kUicArg3 = 0x9c };
enum : uint32_t { kIsUicError = 1U << 2, kIsPowerMode = 1U << 4, kIsUicComplete = 1U << 10 };
enum : uint32_t { kDmeGet = 0x01, kDmeSet = 0x02, kDmePeerGet = 0x03, kDmePeerSet = 0x04, kDmeLinkStartup = 0x16 };
static inline uint32_t uicArgMib (uint32_t attribute, uint32_t selector) { return ((attribute & 0xffff) << 16) | (selector & 0xffff); }

static bool exynosUicReady (void)
{
  for (unsigned i = 0; i < 100000; i++) {
    if (read32 (gExynosLink.hc, 0x30) & (1U << 3)) return true;   // HCS.UCRDY
    IODelay (1);
  }
  return false;
}

// One UIC command, polled. `result` receives ARG3 (the value of a GET). The generic result code in
// ARG2 must be 0. A DME_SET of PA_PwrMode also waits for the power-mode completion and checks
// that the local request succeeded (HCS.UPMCRS == 1).
static bool exynosUic (uint32_t command, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *result, const char **why)
{
  uintptr_t hc = gExynosLink.hc;
  if (!exynosUicReady ()) { *why = "UIC not ready"; return false; }
  write32 (hc, 0x20, kIsUicComplete | kIsUicError | kIsPowerMode);
  write32 (hc, kUicArg1, arg1);
  write32 (hc, kUicArg2, arg2);
  write32 (hc, kUicArg3, arg3);
  __asm__ volatile ("dsb sy" ::: "memory");
  write32 (hc, kUicCmd, command);
  const unsigned limit = command == kDmeLinkStartup ? 1000000 : 200000;   // microseconds
  uint32_t is = 0;
  bool done = false;
  for (unsigned i = 0; i < limit; i++) {
    is = read32 (hc, 0x20);
    if (is & kIsUicComplete) { done = true; break; }
    IODelay (1);
  }
  write32 (hc, 0x20, is & (kIsUicComplete | kIsUicError));
  if (!done) { *why = "UIC command timeout"; return false; }
  uint32_t code = read32 (hc, kUicArg2) & 0xff;
  if (code != 0) { *why = command == kDmeLinkStartup ? "DME_LINKSTARTUP failed" : "UIC command error"; return false; }
  if ((is & kIsUicError) && command != kDmeLinkStartup) { *why = "UIC error during command"; return false; }
  if (result) *result = read32 (hc, kUicArg3);
  if (command == kDmeSet && arg1 == uicArgMib (0x1571, 0)) {
    bool changed = false;
    for (unsigned i = 0; i < 1000000; i++) {
      if (read32 (hc, 0x20) & kIsPowerMode) { changed = true; break; }
      IODelay (1);
    }
    write32 (hc, 0x20, kIsPowerMode);
    if (!changed || ((read32 (hc, 0x30) >> 8) & 7) != 1) { *why = "power mode change not accepted"; return false; }
  }
  return true;
}

static bool exynosDmeSet (uint32_t attribute, uint32_t selector, uint32_t value, const char **why)
{
  return exynosUic (kDmeSet, uicArgMib (attribute, selector), 0, value, nullptr, why);
}
static bool exynosDmeGet (uint32_t attribute, uint32_t selector, uint32_t *value, const char **why)
{
  return exynosUic (kDmeGet, uicArgMib (attribute, selector), 0, 0, value, why);
}
static bool exynosDmePeerSet (uint32_t attribute, uint32_t selector, uint32_t value, const char **why)
{
  return exynosUic (kDmePeerSet, uicArgMib (attribute, selector), 0, value, nullptr, why);
}
static inline void exynosPmaWrite (uint32_t offset, uint32_t value) { write32 (gExynosLink.pma, offset, value); __asm__ volatile ("dsb sy" ::: "memory"); }
static inline uint32_t exynosPmaRead (uint32_t offset) { return read32 (gExynosLink.pma, offset); }
static inline void exynosUniproWrite (uint32_t offset, uint32_t value) { write32 (gExynosLink.unipro, offset, value); __asm__ volatile ("dsb sy" ::: "memory"); }

#include "ExynosUfsCal.h"

static bool exynosWaitBit (uintptr_t base, unsigned offset, uint32_t mask, bool set, unsigned loops)
{
  for (unsigned i = 0; i < loops; i++) {
    bool state = (read32 (base, offset) & mask) != 0;
    if (state == set) return true;
    IODelay (1);
  }
  return false;
}

static bool exynosLinkMap (const char **why)
{
  ExynosLinkState &s = gExynosLink;
  if (!s.hc) s.hc = mapPhys (UFS_HC_BASE, UFS_HC_SIZE, s.hcMap);
  if (!s.unipro) s.unipro = mapPhys (UFS_UNIPRO_BASE, UFS_UNIPRO_SIZE, s.uniproMap);
  if (!s.pma) s.pma = mapPhys (UFS_PMA_BASE, UFS_PMA_SIZE, s.pmaMap);
  if (!s.pmu) s.pmu = mapPhys (UFS_PMU_BASE, 0x4000, s.pmuMap);
  if (!s.sysreg) s.sysreg = mapPhys (UFS_SYSREG_BASE, 0x1000, s.sysregMap);
  if (!s.hc || !s.unipro || !s.pma || !s.pmu || !s.sysreg || !exynosMapVendor ()) { *why = "register window map"; return false; }
  if (!s.lists) {
    s.lists = IOBufferMemoryDescriptor::inTaskWithOptions (kernel_task,
      static_cast<IOOptionBits> (kIODirectionInOut) | static_cast<IOOptionBits> (kIOMemoryPhysicallyContiguous) |
      static_cast<IOOptionBits> (kIOMemoryMapperNone), 8192, 4096);
    if (!s.lists) { *why = "request list allocation"; return false; }
    bzero (s.lists->getBytesNoCopy (), 8192);
  }
  return true;
}

static bool hostPlatformLinkStartup (const char **why)
{
  if (!exynosLinkMap (why)) return false;
  ExynosLinkState &s = gExynosLink;
  uintptr_t hc = s.hc, vs = gExynosVs;
  // PHY isolation bypassed, M-PHY powered; UFS traffic I/O coherent.
  write32 (s.pmu, UFS_PMU_PHY_CTRL, read32 (s.pmu, UFS_PMU_PHY_CTRL) | 1U);
  write32 (s.sysreg, UFS_SYSREG_IOCC, read32 (s.sysreg, UFS_SYSREG_IOCC) | 3U);
  __asm__ volatile ("dsb sy" ::: "memory");
  // Host software reset (clock control enables cleared first, as the firmware does).
  if ((read32 (vs, kVsMisc) >> 4) & 0xF) write32 (vs, kVsMisc, 0);
  write32 (vs, kVsSwRst, 3);
  __asm__ volatile ("dsb sy" ::: "memory");
  if (!exynosWaitBit (vs, kVsSwRst, 3, false, 100000)) { *why = "host software reset"; return false; }
  uint32_t vsIs = read32 (vs, kVsIs);
  if ((vsIs >> 20) & 1) write32 (vs, kVsIs, vsIs);
  // Device reset through the host's RESET_n output.
  write32 (vs, kVsGpioOut, 0);
  __asm__ volatile ("dsb sy" ::: "memory");
  IODelay (5);
  write32 (vs, kVsGpioOut, 1);
  __asm__ volatile ("dsb sy" ::: "memory");
  IODelay (1000);
  // Host controller enable.
  write32 (hc, 0x34, 1);
  __asm__ volatile ("dsb sy" ::: "memory");
  if (!exynosWaitBit (hc, 0x34, 1, true, 100000)) { *why = "host controller enable"; return false; }
  // Reference clock output running, clock gating as the firmware sets it, automatic gating off.
  write32 (vs, kVsClkStopCtrl, read32 (vs, kVsClkStopCtrl) & ~(1U << 4));
  write32 (vs, kVsMisc, 0x0DE0);
  write32 (vs, kVsAcgDisable, read32 (vs, kVsAcgDisable) | 1U);
  // Two vendor writes the firmware makes before link startup (BoardInit/UfsPreSetup; purpose undocumented).
  write32 (vs, 0x114, 0x22);
  write32 (vs, 0x110, 1);
  // Interrupt aggregation tick from the 200 MHz clock.
  write32 (vs, kVsV2p1Ctrl, read32 (vs, kVsV2p1Ctrl) | (1U << 16));
  write32 (vs, kVs1usToCnt, (UFS_MCLK_HZ / 1000000U) & 0x3FFU);
  __asm__ volatile ("dsb sy" ::: "memory");
  // Lanes, pre-link calibration, link startup.
  uint32_t lanes = 0;
  if (!exynosDmeGet (0x1540, 0, &lanes, why)) return false;           // PA_AvailRxDataLanes
  s.availableLanes = static_cast<uint8_t> (lanes ? lanes : 1);
  if (!exynosCalPreLink (why)) return false;
  bool up = false;
  for (unsigned attempt = 0; attempt < 3 && !up; attempt++) {
    const char *ignored = "";
    up = exynosUic (kDmeLinkStartup, 0, 0, 0, nullptr, &ignored) && (read32 (hc, 0x30) & 1U);   // HCS.DP
  }
  if (!up) { *why = "link startup (no device present)"; return false; }
  uint32_t maxGear = 0;
  if (!exynosDmeGet (0x1587, 0, &maxGear, why)) return false;          // PA_MaxRxHSGear
  s.maxGear = static_cast<uint8_t> (maxGear > 4 ? 4 : maxGear);
  if (!exynosCalPostLink (why)) return false;
  uint32_t tx = 0, rx = 0;
  if (!exynosDmeGet (0x1560, 0, &tx, why) || !exynosDmeGet (0x1580, 0, &rx, why)) return false;   // PA_Active*DataLanes
  s.activeTxLanes = static_cast<uint8_t> (tx);
  s.activeRxLanes = static_cast<uint8_t> (rx);
  // Vendor request setup, then empty lists so the host reports both lists ready for the takeover.
  write32 (vs, kVsDataReorder, 0xA);
  write32 (vs, kVsTxPrdtEntrySize, 12);
  write32 (vs, kVsRxPrdtEntrySize, 12);
  write32 (vs, kVsUtrlNexusType, 0xFFFFFFFFU);
  write32 (vs, kVsUtmrlNexusType, 0xFFFFFFFFU);
  IOByteCount contiguous = 0;
  uint64_t lists = s.lists->getPhysicalSegment (0, &contiguous, kIOMemoryMapperNone);
  if (!lists || contiguous < 8192) { *why = "request list address"; return false; }
  write32 (hc, 0x70, static_cast<uint32_t> (lists + 4096));
  write32 (hc, 0x74, static_cast<uint32_t> ((lists + 4096) >> 32));
  write32 (hc, 0x50, static_cast<uint32_t> (lists));
  write32 (hc, 0x54, static_cast<uint32_t> (lists >> 32));
  __asm__ volatile ("dsb sy" ::: "memory");
  write32 (hc, 0x80, 1);
  write32 (hc, 0x60, 1);
  __asm__ volatile ("dsb sy" ::: "memory");
  if (!exynosWaitBit (hc, 0x30, 0x0f, true, 100000) || (read32 (hc, 0x30) & 0x0f) != 0x0f) { *why = "request lists ready"; return false; }
  s.relinked = true;
  IOLog ("NativeUfs: link re-trained: lanes avail=%u active tx=%u rx=%u max HS gear=%u\n",
         s.availableLanes, s.activeTxLanes, s.activeRxLanes, s.maxGear);
  return true;
}
#endif
