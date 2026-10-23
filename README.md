# ShengUfs

SM8550 UFS transport and sheng persistent storage.

Current state: the native UFS root works on sheng (SM8550). The Tensor G4 variant (komodo) is in
bring-up: the host glue, the transfer granularity and the link training are in place; the root
image and its owner-verified policies come later.

## Host variants

The SoC-specific part is one header selected at compile time; everything else (queue takeover,
UPIU and SCSI commands, polling, root windows, verified reads, diagnostics) is shared.

| Glue | Selected by | Covers |
| --- | --- | --- |
| `QcomUfsHost.h` | default | SM8550: GCC clock survey, apps_smmu pass-through, one PRDT entry per command |
| `ExynosUfsHost.h` | `build-arm-driver.py NativeUfs --host exynos` | Tensor G4: no IOMMU, one PRDT entry per 4096-byte data unit, the nexus-type register before each doorbell |

`--root-partition NAME` names the GPT partition that carries the root image (`userdata` on
sheng, `darwin` on komodo). `--unverified` builds without the owner-verified block layer: the
probe modes work, root publication refuses by design.

The Tensor link training (`native-ufs-probe=15`, `ExynosUfsLink.h`, `ExynosUfsPower.h`,
`ExynosUfsCal.h`) is the board firmware's sequence carried over from the BSD-licensed
Mu-Silicium `S5P9875Pkg` UFS library, cross-checked against the mainline Linux `phy-zuma-ufs`
and `ufs-exynos` drivers. The ordinary path never trains the link: it adopts the one the
firmware left up.

Build and interface details: [documentation](https://unixnu.com/drivers/sheng-ufs). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).
