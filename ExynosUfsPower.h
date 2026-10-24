// SPDX-License-Identifier: BSD-2-Clause-Patent
// After a link this driver re-trained (ExynosUfsLink.h): device initialisation (fDeviceInit), the
// reference clock attribute, and the change to the fastest high-speed gear with the calibration
// around it, the way the board's firmware does it (Mu-Silicium UfsDxe UfsEndBootMode,
// UfsRefClkSetup, UfsCheck2Lane, UfsPreGearChange, UfsPmcCommon). Included after RootCommands.h.
#ifndef NATIVE_UFS_EXYNOS_POWER_H
#define NATIVE_UFS_EXYNOS_POWER_H

// Query write requests: SET FLAG (opcode 0x06) and WRITE ATTRIBUTE (0x04). Device configuration,
// not storage: nothing here touches a logical unit.
static IOReturn exynosQueryWrite (uint8_t opcode, uint8_t idn, uint32_t value)
{
  if (!hostClearUcd ()) return kIOReturnNotReady;
  uint8_t *ucd = gHost.virt + kUcdOffset;
  ucd[0] = 0x16;                      // QUERY REQUEST
  ucd[3] = 1;
  ucd[5] = 0x81;                      // standard write request
  ucd[12] = opcode;
  ucd[13] = idn;
  if (opcode == 0x04) putBe32 (ucd + 20, value);
  uint32_t ocs = 0xf;
  IOReturn status = hostSubmit (0, 0, 500000, &ocs);
  if (status != kIOReturnSuccess) return status;
  status = hostResponseHeader (0x36, 0, 1);
  if (status != kIOReturnSuccess) return status;
  const uint8_t *rsp = ucd + kRspOffset;
  return rsp[6] == 0 ? kIOReturnSuccess : kIOReturnError;
}

static bool exynosEndBootMode (const char **why)
{
  if (exynosQueryWrite (0x06, 0x01, 0) != kIOReturnSuccess) { *why = "fDeviceInit set"; return false; }   // SET FLAG fDeviceInit
  for (unsigned i = 0; i < 1000; i++) {
    uint32_t flag = 1;
    if (hostQueryRead (0x05, 0x01, &flag) != kIOReturnSuccess) { *why = "fDeviceInit read"; return false; }
    if ((flag & 0xff) == 0) return true;
    IOSleep (1);
  }
  *why = "fDeviceInit did not clear";
  return false;
}

static bool exynosRefClkSetup (const char **why)
{
  uint32_t value = 0;
  if (hostQueryRead (0x03, 0x0A, &value) != kIOReturnSuccess) { *why = "bRefClkFreq read"; return false; }   // READ ATTRIBUTE bRefClkFreq
  if (value == 2) return true;                                                                           // 38.4 MHz
  if (exynosQueryWrite (0x04, 0x0A, 2) != kIOReturnSuccess) { *why = "bRefClkFreq write"; return false; }
  return true;
}

static bool exynosGearChange (const char **why)
{
  ExynosLinkState &s = gExynosLink;
  uint32_t tx = 0, rx = 0;
  if (!exynosDmeGet (0x1561, 0, &tx, why) || !exynosDmeGet (0x1581, 0, &rx, why)) return false;   // PA_Connected*DataLanes
  s.connectedTxLanes = static_cast<uint8_t> (tx);
  s.connectedRxLanes = static_cast<uint8_t> (rx);
  if (!exynosDmeSet (0x1560, 0, tx, why) || !exynosDmeSet (0x1580, 0, rx, why)) return false;     // PA_Active*DataLanes
  ExynosPwrMode pmd = { static_cast<uint8_t> (tx), static_cast<uint8_t> (s.maxGear ? s.maxGear : 1), kExynosPwrFast, kExynosHsSeriesB };
  if (!exynosCalPrePmc (&pmd, why)) return false;
  if (!exynosDmeSet (0x1583, 0, pmd.gear, why)) return false;    // PA_RxGear
  if (!exynosDmeSet (0x1568, 0, pmd.gear, why)) return false;    // PA_TxGear
  if (!exynosDmeSet (0x1580, 0, pmd.lane, why)) return false;    // PA_ActiveRxDataLanes
  if (!exynosDmeSet (0x1560, 0, pmd.lane, why)) return false;    // PA_ActiveTxDataLanes
  if (!exynosDmeSet (0x1584, 0, 1, why)) return false;           // PA_RxTermination
  if (!exynosDmeSet (0x1569, 0, 1, why)) return false;           // PA_TxTermination
  if (!exynosDmeSet (0x156A, 0, pmd.hsSeries, why)) return false; // PA_HSSeries
  if (!exynosDmeSet (0x1571, 0, (uint32_t (pmd.mode) << 4) | pmd.mode, why)) return false;   // PA_PwrMode: FAST both ways
  if (!exynosCalPostPmc (&pmd, why)) return false;
  if (!exynosDmeGet (0x1560, 0, &tx, why) || !exynosDmeGet (0x1580, 0, &rx, why)) return false;
  s.activeTxLanes = static_cast<uint8_t> (tx);
  s.activeRxLanes = static_cast<uint8_t> (rx);
  IOLog ("NativeUfs: power mode HS-G%u series B, lanes tx=%u rx=%u\n", pmd.gear, s.activeTxLanes, s.activeRxLanes);
  return true;
}

// The link mode in force when the kernel takes the controller over. Mu-Silicium's UfsDxe trains to
// HS gear 4 (S5P9875Pkg PlatformUfsLib BoardInit sets GearMode = 4), and this driver does not reset
// the controller unless it re-trains the link itself, so that mode should survive the hand-off. A
// link still in PWM-G1, the mode DME_LINKSTARTUP leaves behind, would cap reads near 1 MB/s.
static void exynosReportInheritedLink (void)
{
  const char *ignored = NULL;
  uint32_t pwrMode = 0, txGear = 0, rxGear = 0, txLanes = 0, rxLanes = 0, series = 0, maxGear = 0;

  exynosDmeGet (0x1571, 0, &pwrMode, &ignored);   // PA_PwrMode: 0x11 fast both ways, 0x22 slow (PWM)
  exynosDmeGet (0x1568, 0, &txGear, &ignored);    // PA_TxGear
  exynosDmeGet (0x1583, 0, &rxGear, &ignored);    // PA_RxGear
  exynosDmeGet (0x1560, 0, &txLanes, &ignored);   // PA_ActiveTxDataLanes
  exynosDmeGet (0x1580, 0, &rxLanes, &ignored);   // PA_ActiveRxDataLanes
  exynosDmeGet (0x156A, 0, &series, &ignored);    // PA_HSSeries
  exynosDmeGet (0x1587, 0, &maxGear, &ignored);   // PA_MaxRxHSGear
  IOLog ("NativeUfs: inherited link PA_PwrMode=%#x gear tx=%u rx=%u lanes tx=%u rx=%u HS series=%u max HS gear=%u\n",
         pwrMode, txGear, rxGear, txLanes, rxLanes, series, maxGear);
}

static bool hostPlatformAfterNop (const char **why)
{
  exynosReportInheritedLink ();
  if (!gExynosLink.relinked) return true;
  if (!exynosEndBootMode (why) || !exynosRefClkSetup (why)) return false;
  return exynosGearChange (why);
}
#endif
