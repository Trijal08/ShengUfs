// SPDX-License-Identifier: BSD-2-Clause-Patent
// M-PHY / UniPro calibration for the Tensor G4 (zumapro) UFS host: the tables and the engine that
// applies them around link startup and a power-mode change.
//
// Carried over from the BSD-2-Clause-Patent Mu-Silicium firmware for this SoC
// (Silicon/Samsung/S5P9875Pkg/Library/PlatformUfsLib/UfsCal.c, itself a transcription of the
// downstream zuma ufs-cal.h), the sequence that brings the link up on this very device. The
// mainline Linux drivers (drivers/phy/samsung/phy-zuma-ufs.c, drivers/ufs/host/ufs-exynos.c) carry
// the same values; they were used to cross-check, not copied.
//
// Included by ExynosUfsLink.h after its primitives: exynosDmeSet/Get/PeerSet, exynosPmaWrite/Read,
// exynosUniproWrite, and the link state in gExynosLink.
#ifndef NATIVE_UFS_EXYNOS_CAL_H
#define NATIVE_UFS_EXYNOS_CAL_H

enum : uint8_t { kExynosPwrFast = 1, kExynosPwrSlow = 2, kExynosPwrFastAuto = 4, kExynosPwrSlowAuto = 5 };
enum : uint8_t { kExynosHsSeriesA = 1, kExynosHsSeriesB = 2 };

struct ExynosPwrMode {
  uint8_t lane, gear, mode, hsSeries;
};

// Where an entry is applied.
enum ExynosCalLayer : uint32_t {
  kCalNone = 0,
  kCalPcsCommon,        // DME_SET of a PCS attribute (lane 0 only)
  kCalPcsRxTx,          // DME_SET on every TX and RX lane
  kCalPmaCommon,        // APB write to the PMA common block
  kCalPmaTrsv,          // APB write to each lane's transceiver block
  kCalPllWait, kCalCdrWait, kCalCdrAfcWait,
  kCalUniproStdMib,     // DME_SET of a standard UniPro attribute
  kCalUniproDbgMib,     // DME_SET of a vendor debug attribute
  kCalUniproDbgApb,     // APB write into the UniPro debug window
  kCalPcsRx, kCalPcsTx,
  kCalPcsRxPrd, kCalPcsTxPrd,          // MCLK period
  kCalUniproDbgPrd,                    // UniPro 1.8 MCLK period into the debug window
  kCalPmaTrsvLane1SqOff, kCalPmaTrsvSq,
  kCalCommonWait,
  kCalPcsRxLrPrd, kCalPcsTxLrPrd,      // line-reset periods, three bytes each
  kCalPcsRxPrdRoundOff, kCalPcsTxPrdRoundOff,
  kCalUniproAdaptLength,
  kCalEmbCdrWait, kCalEmbCalWait,
};

// Which power modes an entry applies to (bit per PWM/HS gear and lane count).
enum : uint32_t {
  kPmdPwmG1L1 = 1U << 0, kPmdPwmG1L2 = 1U << 1, kPmdPwmG2L1 = 1U << 2, kPmdPwmG2L2 = 1U << 3,
  kPmdPwmG3L1 = 1U << 4, kPmdPwmG3L2 = 1U << 5, kPmdPwmG4L1 = 1U << 6, kPmdPwmG4L2 = 1U << 7,
  kPmdPwmG5L1 = 1U << 8, kPmdPwmG5L2 = 1U << 9,
  kPmdHsG1L1 = 1U << 11, kPmdHsG1L2 = 1U << 12, kPmdHsG2L1 = 1U << 13, kPmdHsG2L2 = 1U << 14,
  kPmdHsG3L1 = 1U << 15, kPmdHsG3L2 = 1U << 16, kPmdHsG4L1 = 1U << 17, kPmdHsG4L2 = 1U << 18,
  kPmdPwm = (1U << 10) - 1,
  kPmdAll = (1U << 19) - 1,
  kPmdHs = kPmdAll ^ kPmdPwm,
  kPmdHsBelowG4 = kPmdHsG1L1 | kPmdHsG1L2 | kPmdHsG2L1 | kPmdHsG2L2 | kPmdHsG3L1 | kPmdHsG3L2,
  kPmdHsG4 = kPmdHsG4L1 | kPmdHsG4L2,
};
enum : uint32_t { kPaInitialAdapt = 0x01, kPaNoAdapt = 0x03 };

struct ExynosCalEntry {
  uint32_t address, value, flag;
  ExynosCalLayer layer;
};

// MCLK periods. The host runs UniPro 1.8 (the firmware marks it HOST_EMBD).
static inline uint32_t exynosMclkPeriodNs (void) { return 1000000000U / UFS_MCLK_HZ; }
static inline uint32_t exynosMclkPeriodRoundOff (void) { return static_cast<uint32_t> (((2000000000ULL / UFS_MCLK_HZ) + 1) / 2); }
static inline uint32_t exynosUnipro18MclkPeriod (void) { return static_cast<uint32_t> (16ULL * 1000ULL * 1000000ULL / UFS_MCLK_HZ); }
static inline uint32_t exynosTxLineResetPeriod (void) { return static_cast<uint32_t> (3200ULL * (UFS_MCLK_HZ / 1000000ULL)); }
static inline uint32_t exynosRxLineResetDetectPeriod (void) { return static_cast<uint32_t> (1000ULL * (UFS_MCLK_HZ / 1000000ULL)); }

// Pre-link (EVT1 silicon, production). The reference clock is 38.4 MHz (PCS 0x202 = 0x22).
static const ExynosCalEntry kExynosCalPreLink[] = {
  {0x44,   0x00,       kPmdAll, kCalUniproDbgPrd},
  {0x200,  0x40,       kPmdAll, kCalPcsCommon},
  {0x202,  0x22,       kPmdAll, kCalPcsCommon},
  {0x12,   0x00,       kPmdAll, kCalPcsRxPrdRoundOff},
  {0xAA,   0x00,       kPmdAll, kCalPcsTxPrdRoundOff},
  {0xA9,   0x02,       kPmdAll, kCalPcsTx},
  {0xAB,   0x00,       kPmdAll, kCalPcsTxLrPrd},
  {0x11,   0x00,       kPmdAll, kCalPcsRx},
  {0x1B,   0x00,       kPmdAll, kCalPcsRxLrPrd},
  {0x2F,   0x79,       kPmdAll, kCalPcsRx},
  {0x84,   0x01,       kPmdAll, kCalPcsRx},
  {0x04,   0x01,       kPmdAll, kCalPcsTx},
  {0x25,   0xF6,       kPmdAll, kCalPcsRx},
  {0x7F,   0x00,       kPmdAll, kCalPcsTx},
  {0x200,  0x00,       kPmdAll, kCalPcsCommon},
  {0x155E, 0x00,       kPmdAll, kCalUniproStdMib},   // PA_Local_TX_LCC_Enable
  {0x3000, 0x00,       kPmdAll, kCalUniproStdMib},   // N_DeviceID
  {0x3001, 0x01,       kPmdAll, kCalUniproStdMib},   // N_DeviceID_valid
  {0x4021, 0x01,       kPmdAll, kCalUniproStdMib},   // T_PeerDeviceID
  {0x4020, 0x01,       kPmdAll, kCalUniproStdMib},   // T_ConnectionState: connected
  {0x140,  0x08,       kPmdAll, kCalPmaCommon},
  {0x014,  0x19,       kPmdAll, kCalPmaCommon},
  {0x02C,  0x44,       kPmdAll, kCalPmaCommon},
  {0x030,  0xC4,       kPmdAll, kCalPmaCommon},
  {0x034,  0xC3,       kPmdAll, kCalPmaCommon},
  {0x03C,  0x88,       kPmdAll, kCalPmaCommon},
  {0x058,  0x1A,       kPmdAll, kCalPmaCommon},
  {0x064,  0x04,       kPmdAll, kCalPmaCommon},
  {0x150,  0x88,       kPmdAll, kCalPmaCommon},
  {0x19C,  0x4C,       kPmdAll, kCalPmaCommon},
  {0x1A0,  0x4C,       kPmdAll, kCalPmaCommon},
  {0x804,  0x44,       kPmdAll, kCalPmaTrsv},
  {0x808,  0x44,       kPmdAll, kCalPmaTrsv},
  {0x80C,  0x00,       kPmdAll, kCalPmaTrsv},
  {0x810,  0x18,       kPmdAll, kCalPmaTrsv},
  {0x814,  0xC0,       kPmdAll, kCalPmaTrsv},
  {0x81C,  0x1C,       kPmdAll, kCalPmaTrsv},
  {0xBB0,  0x8C,       kPmdAll, kCalPmaTrsv},
  {0x9F0,  0xD0,       kPmdAll, kCalPmaTrsv},
  {0xA20,  0xFA,       kPmdAll, kCalPmaTrsv},
  {0xA24,  0x60,       kPmdAll, kCalPmaTrsv},
  {0x8D0,  0x30,       kPmdAll, kCalPmaTrsv},
  {0x8E4,  0x05,       kPmdAll, kCalPmaTrsv},
  {0x8F4,  0x05,       kPmdAll, kCalPmaTrsv},
  {0x934,  0x1A,       kPmdAll, kCalPmaTrsv},
  {0x938,  0x12,       kPmdAll, kCalPmaTrsv},
  {0x93C,  0x5E,       kPmdAll, kCalPmaTrsv},
  {0x964,  0x2A,       kPmdAll, kCalPmaTrsv},
  {0x980,  0x54,       kPmdAll, kCalPmaTrsv},
  {0x998,  0x54,       kPmdAll, kCalPmaTrsv},
  {0x9CC,  0x00,       kPmdAll, kCalPmaTrsv},
  {0x9D0,  0x00,       kPmdAll, kCalPmaTrsv},
  {0xAAC,  0x00,       kPmdAll, kCalPmaTrsv},
  {0xAB0,  0x02,       kPmdAll, kCalPmaTrsv},
  {0x140,  0x0C,       kPmdAll, kCalPmaCommon},
  {0x140,  0x00,       kPmdAll, kCalPmaCommon},
  // Trailing clocks and the PA debug option suites, which Linux writes from its attribute table.
  {0x1564, 0xFF,       kPmdAll, kCalUniproStdMib},   // PA_TxTrailingClocks
  {0x956A, 0x90913C1C, kPmdAll, kCalUniproDbgMib},
  {0x956D, 0xE01C115F, kPmdAll, kCalUniproDbgMib},
  // Receiver calibration done: bit 3 (as the mainline PHY driver waits), TRSV 0xC74 or 0xCE0.
  {0xC74,  0x08,       kPmdAll, kCalEmbCalWait},
  {0, 0, 0, kCalNone},
};

static const ExynosCalEntry kExynosCalPostLink[] = {
  {0x15D2, 0x00,  kPmdAll, kCalUniproAdaptLength},
  {0x15D3, 0x00,  kPmdAll, kCalUniproAdaptLength},
  {0x9529, 0x01,  kPmdAll, kCalUniproDbgMib},        // PA_DBG_MODE on
  {0x15A4, 0x3E8, kPmdAll, kCalUniproStdMib},        // PA_SaveConfigTime
  {0x9529, 0x00,  kPmdAll, kCalUniproDbgMib},        // PA_DBG_MODE off
  {0, 0, 0, kCalNone},
};

static const ExynosCalEntry kExynosCalPwm[] = {
  {0x2041, 8064,  kPmdPwm, kCalUniproStdMib}, {0x2042, 28224, kPmdPwm, kCalUniproStdMib}, {0x2043, 20160, kPmdPwm, kCalUniproStdMib},
  {0x15B0, 12000, kPmdPwm, kCalUniproStdMib}, {0x15B1, 32000, kPmdPwm, kCalUniproStdMib}, {0x15B2, 16000, kPmdPwm, kCalUniproStdMib},
  {0x7888, 8064,  kPmdPwm, kCalUniproDbgApb}, {0x788C, 28224, kPmdPwm, kCalUniproDbgApb}, {0x7890, 20160, kPmdPwm, kCalUniproDbgApb},
  {0x78B8, 12000, kPmdPwm, kCalUniproDbgApb}, {0x78BC, 32000, kPmdPwm, kCalUniproDbgApb}, {0x78C0, 16000, kPmdPwm, kCalUniproDbgApb},
  {0, 0, 0, kCalNone},
};
static const ExynosCalEntry kExynosCalPostPwm[] = {
  {0x020, 0x60, kPmdPwm, kCalPmaCommon},
  {0x888, 0x08, kPmdPwm, kCalPmaTrsv},
  {0x918, 0x01, kPmdAll, kCalPmaTrsv},
  {0, 0, 0, kCalNone},
};
// HS, series A and B carry the same values on this SoC.
static const ExynosCalEntry kExynosCalHs[] = {
  {0x15D4, kPaNoAdapt,      kPmdHsBelowG4, kCalUniproStdMib},   // PA_TxHsAdaptType
  {0x15D4, kPaInitialAdapt, kPmdHsG4,      kCalUniproStdMib},
  {0x2041, 8064,  kPmdHs, kCalUniproStdMib}, {0x2042, 28224, kPmdHs, kCalUniproStdMib}, {0x2043, 20160, kPmdHs, kCalUniproStdMib},
  {0x15B0, 12000, kPmdHs, kCalUniproStdMib}, {0x15B1, 32000, kPmdHs, kCalUniproStdMib}, {0x15B2, 16000, kPmdHs, kCalUniproStdMib},
  {0x7888, 8064,  kPmdHs, kCalUniproDbgApb}, {0x788C, 28224, kPmdHs, kCalUniproDbgApb}, {0x7890, 20160, kPmdHs, kCalUniproDbgApb},
  {0x78B8, 12000, kPmdHs, kCalUniproDbgApb}, {0x78BC, 32000, kPmdHs, kCalUniproDbgApb}, {0x78C0, 16000, kPmdHs, kCalUniproDbgApb},
  {0xDA4,  0x11,  kPmdHs, kCalPmaTrsv},
  {0x918,  0x03,  kPmdHs, kCalPmaTrsv},
  {0, 0, 0, kCalNone},
};
static const ExynosCalEntry kExynosCalPostHs[] = {
  {0x918, 0x01, kPmdAll, kCalPmaTrsv},
  {0, 0, 0, kCalNone},
};
static const ExynosCalEntry kExynosCalLane1SqOff[] = {
  {0x9F4, 0x08, kPmdAll, kCalPmaTrsvLane1SqOff},
  {0xA00, 0x3A, kPmdAll, kCalPmaTrsvLane1SqOff},
  {0, 0, 0, kCalNone},
};

static inline bool exynosPwrIsHs (uint8_t mode) { return mode == kExynosPwrFast || mode == kExynosPwrFastAuto; }
static inline bool exynosPwrIsPwm (uint8_t mode) { return mode == kExynosPwrSlow || mode == kExynosPwrSlowAuto; }

static bool exynosCalModeMatches (const ExynosPwrMode *pmd, uint32_t flag)
{
  if (!pmd || flag == kPmdAll) return true;
  if (exynosPwrIsHs (pmd->mode) && flag == kPmdHs) return true;
  if (exynosPwrIsPwm (pmd->mode) && flag == kPmdPwm) return true;
  uint8_t g = pmd->gear;
  if (exynosPwrIsHs (pmd->mode)) {
    if (g == 1 && (flag & (kPmdHsG1L1 | kPmdHsG1L2))) return true;
    if (g == 2 && (flag & (kPmdHsG2L1 | kPmdHsG2L2))) return true;
    if (g == 3 && (flag & (kPmdHsG3L1 | kPmdHsG3L2))) return true;
    if (g == 4 && (flag & (kPmdHsG4L1 | kPmdHsG4L2))) return true;
  }
  if (exynosPwrIsPwm (pmd->mode)) {
    if (g == 1 && (flag & (kPmdPwmG1L1 | kPmdPwmG1L2))) return true;
    if (g == 2 && (flag & (kPmdPwmG2L1 | kPmdPwmG2L2))) return true;
    if (g == 3 && (flag & (kPmdPwmG3L1 | kPmdPwmG3L2))) return true;
    if (g == 4 && (flag & (kPmdPwmG4L1 | kPmdPwmG4L2))) return true;
    if (g == 5 && (flag & (kPmdPwmG5L1 | kPmdPwmG5L2))) return true;
  }
  return false;
}

enum : uint32_t { kExynosTxLane0 = 0, kExynosRxLane0 = 4 };
static inline uint32_t exynosPmaTrsv (uint32_t reg, unsigned lane) { return reg + 0x800U * lane; }

static bool exynosCalWaitPllLock (uint32_t addr, uint32_t mask)
{
  for (unsigned i = 0; i < 100; i++) {
    if ((exynosPmaRead (addr) & mask) == mask) return true;
    IODelay (1);
  }
  return false;
}
static bool exynosCalWaitCdrLock (uint32_t addr, uint32_t mask, unsigned lane)
{
  for (unsigned i = 0; i < 1000; i++) {
    if ((exynosPmaRead (exynosPmaTrsv (addr, lane)) & mask) == mask) return true;
    IODelay (1);
  }
  return false;
}
static bool exynosCalWaitEmbCdrLock (uint32_t addr, uint32_t mask, unsigned lane)
{
  for (unsigned i = 0; i < 100; i++) {
    IODelay (40);
    if ((exynosPmaRead (exynosPmaTrsv (addr, lane)) & mask) == mask) return true;
    IODelay (1);
    exynosPmaWrite (exynosPmaTrsv (0x888, lane), 0x10);
    exynosPmaWrite (exynosPmaTrsv (0x888, lane), 0x18);
  }
  return false;
}
// Receiver calibration completion is reported at TRSV 0xC74 (downstream) or 0xCE0 (gs101 path).
static bool exynosCalWaitEmbCalDone (uint32_t addr, uint32_t mask, unsigned lane)
{
  for (unsigned i = 0; i < 5000; i++) {
    uint32_t primary = exynosPmaRead (exynosPmaTrsv (addr, lane));
    uint32_t alternate = exynosPmaRead (exynosPmaTrsv (0xCE0, lane));
    if ((primary & mask) == mask || (alternate & mask) == mask) return true;
    IODelay (40);
  }
  return false;
}

// Apply one table for the lanes the link has. `pmd` null applies every entry regardless of mode.
static bool exynosCalApply (const ExynosCalEntry *cfg, const ExynosPwrMode *pmd, const char **why)
{
  const unsigned lanes = gExynosLink.availableLanes ? gExynosLink.availableLanes : 1;
  for (; cfg->layer != kCalNone; cfg++) {
    for (unsigned lane = 0; lane < lanes; lane++) {
      if (!exynosCalModeMatches (pmd, cfg->flag)) continue;
      switch (cfg->layer) {
      case kCalUniproStdMib:
      case kCalUniproDbgMib:
      case kCalPcsCommon:
        if (lane == 0 && !exynosDmeSet (cfg->address, 0, cfg->value, why)) return false;
        break;
      case kCalUniproDbgPrd:
        if (lane == 0) exynosUniproWrite (cfg->address, exynosUnipro18MclkPeriod ());
        break;
      case kCalUniproDbgApb:
        if (lane == 0) exynosUniproWrite (cfg->address, cfg->value);
        break;
      case kCalUniproAdaptLength:
        if (lane == 0) {
          uint32_t value = 0;
          if (!exynosDmeGet (cfg->address, 0, &value, why)) return false;
          if (value & 0x80) {
            if ((value & 0x7F) < 2 && !exynosDmeSet (cfg->address, 0, 0x82, why)) return false;
          } else if (((value + 1) % 4) != 0) {
            do { value++; } while (((value + 1) % 4) != 0);
            if (!exynosDmeSet (cfg->address, 0, value, why)) return false;
          }
        }
        break;
      case kCalPcsRxTx:
        if (!exynosDmeSet (cfg->address, kExynosTxLane0 + lane, cfg->value, why)) return false;
        if (!exynosDmeSet (cfg->address, kExynosRxLane0 + lane, cfg->value, why)) return false;
        break;
      case kCalPcsRx:
        if (!exynosDmeSet (cfg->address, kExynosRxLane0 + lane, cfg->value, why)) return false;
        break;
      case kCalPcsTx:
        if (!exynosDmeSet (cfg->address, kExynosTxLane0 + lane, cfg->value, why)) return false;
        break;
      case kCalPcsRxPrd:
        if (!exynosDmeSet (cfg->address, kExynosRxLane0 + lane, exynosMclkPeriodNs (), why)) return false;
        break;
      case kCalPcsTxPrd:
        if (!exynosDmeSet (cfg->address, kExynosTxLane0 + lane, exynosMclkPeriodNs (), why)) return false;
        break;
      case kCalPcsRxPrdRoundOff:
        if (!exynosDmeSet (cfg->address, kExynosRxLane0 + lane, exynosMclkPeriodRoundOff (), why)) return false;
        break;
      case kCalPcsTxPrdRoundOff:
        if (!exynosDmeSet (cfg->address, kExynosTxLane0 + lane, exynosMclkPeriodRoundOff (), why)) return false;
        break;
      case kCalPcsRxLrPrd: {
        uint32_t period = exynosRxLineResetDetectPeriod ();
        if (!exynosDmeSet (cfg->address, kExynosRxLane0 + lane, (period >> 16) & 0xFF, why)) return false;
        if (!exynosDmeSet (cfg->address + 1, kExynosRxLane0 + lane, (period >> 8) & 0xFF, why)) return false;
        if (!exynosDmeSet (cfg->address + 2, kExynosRxLane0 + lane, period & 0xFF, why)) return false;
        break;
      }
      case kCalPcsTxLrPrd: {
        uint32_t period = exynosTxLineResetPeriod ();
        if (!exynosDmeSet (cfg->address, kExynosTxLane0 + lane, (period >> 16) & 0xFF, why)) return false;
        if (!exynosDmeSet (cfg->address + 1, kExynosTxLane0 + lane, (period >> 8) & 0xFF, why)) return false;
        if (!exynosDmeSet (cfg->address + 2, kExynosTxLane0 + lane, period & 0xFF, why)) return false;
        break;
      }
      case kCalPmaCommon:
        if (lane == 0) exynosPmaWrite (cfg->address, cfg->value);
        break;
      case kCalPmaTrsv:
        exynosPmaWrite (exynosPmaTrsv (cfg->address, lane), cfg->value);
        break;
      case kCalPmaTrsvLane1SqOff:
        if (lane == 1 && gExynosLink.connectedRxLanes < gExynosLink.availableLanes) {
          exynosPmaWrite (exynosPmaTrsv (cfg->address, lane), cfg->value);
        }
        break;
      case kCalPmaTrsvSq:
        if (lane < gExynosLink.connectedRxLanes) exynosPmaWrite (exynosPmaTrsv (cfg->address, lane), cfg->value);
        break;
      case kCalPllWait:
        if (lane == 0 && !exynosCalWaitPllLock (cfg->address, cfg->value)) { *why = "PHY PLL lock"; return false; }
        break;
      case kCalCdrWait:
        if (lane < gExynosLink.activeRxLanes && !exynosCalWaitCdrLock (cfg->address, cfg->value, lane)) { *why = "PHY CDR lock"; return false; }
        break;
      case kCalEmbCdrWait:
        if (lane < gExynosLink.activeRxLanes && !exynosCalWaitEmbCdrLock (cfg->address, cfg->value, lane)) { *why = "PHY embedded CDR lock"; return false; }
        break;
      case kCalCommonWait:
        if (lane == 0) IODelay (cfg->value);
        break;
      case kCalEmbCalWait:
        if (!exynosCalWaitEmbCalDone (cfg->address, cfg->value, lane)) { *why = "PHY receiver calibration"; return false; }
        break;
      default:
        break;
      }
    }
  }
  return true;
}

// Samsung's recommendation: host activate time below the device's, hibernate time above it.
static bool exynosCalHibern8Values (const char **why)
{
  uint32_t minActivate = 0, peerMinActivate = 0, maxHibern8 = 0;
  if (!exynosDmeGet (0x8F, kExynosRxLane0, &minActivate, why)) return false;
  if (!exynosDmeGet (0x15A8, 0, &peerMinActivate, why)) return false;    // PA_TActivate
  if (!exynosDmeGet (0x15A7, 0, &maxHibern8, why)) return false;         // PA_Hibern8Time
  if (peerMinActivate >= minActivate && !exynosDmePeerSet (0x15A8, 0, peerMinActivate + 1, why)) return false;
  return exynosDmeSet (0x15A7, 0, maxHibern8 + 1, why);
}

static bool exynosCalPreLink (const char **why) { return exynosCalApply (kExynosCalPreLink, nullptr, why); }

static bool exynosCalPostLink (const char **why)
{
  if (!exynosCalHibern8Values (why)) return false;
  if (!exynosCalApply (kExynosCalPostLink, nullptr, why)) return false;
  if (gExynosLink.availableLanes == 2 && gExynosLink.connectedRxLanes == 1) {
    return exynosCalApply (kExynosCalLane1SqOff, nullptr, why);
  }
  return true;
}

static bool exynosCalPrePmc (const ExynosPwrMode *pmd, const char **why)
{
  if (exynosPwrIsPwm (pmd->mode)) return exynosCalApply (kExynosCalPwm, pmd, why);
  if (pmd->hsSeries == kExynosHsSeriesA || pmd->hsSeries == kExynosHsSeriesB) return exynosCalApply (kExynosCalHs, pmd, why);
  *why = "power mode";
  return false;
}

static bool exynosCalPostPmc (const ExynosPwrMode *pmd, const char **why)
{
  if (exynosPwrIsPwm (pmd->mode)) return exynosCalApply (kExynosCalPostPwm, pmd, why);
  if (pmd->hsSeries == kExynosHsSeriesA || pmd->hsSeries == kExynosHsSeriesB) return exynosCalApply (kExynosCalPostHs, pmd, why);
  *why = "power mode";
  return false;
}
#endif
