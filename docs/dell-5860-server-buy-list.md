# Dell Precision 5860 — Server Buy List

Planning document for the upgraded Web Vision Pro server. Covers the Dell base
order, the aftermarket parts to add, prices, and everything to verify before
paying.

- **Status:** quote / purchase planning
- **Last updated:** 2026-10-23
- **Target stack:** Ubuntu 24.04 LTS + Basler pylon 26.06, Qt 5.15, OpenCV 4.x
- **Reference:** [16-camera / 50 fps roadmap — Ubuntu 24.04 edition](web-vision-pro-16cam-50fps-roadmap-ubuntu2404.html)

---

## 1. Assumptions

The server is sized for the **upgrade target**, not the current 780×580 scout
fleet:

| Item | Target |
|---|---|
| Cameras | 16 × Basler ace 2 **a2A1920-51** (1920×1200, 2.3 MP) |
| Frame rate | 50 fps global |
| Event length | 2000 frames (~40 s) |
| Mix | 8 Mono8 + 8 color (BGR host-side) |
| Ring RAM | ≈ 74 GB all-mono / ≈ 148 GB 50/50 color |
| Network | 2 × 10GbE LACP to the server (≈ 15 Gbps aggregate) |
| Storage burst | ≈ 0.7–1.1 GB/s at trigger |
| GPU | Display GPU now (T400/T1000); RTX 4000 Ada reserved for later AI work |

If the real upgrade is 8 cameras, a lower resolution, or shorter events, the RAM
and storage lines can be reduced. The CPU/NIC/OS choices stay valid.

---

## 2. Decision

Buy a **Dell Precision 5860 Tower** as a base machine, then add the commodity
parts (RAM, second NVMe, 10G NIC, UPS) aftermarket.

Why this split:

- Dell supplies the hard-to-change parts: chassis, motherboard, CPU, PSU, cooling,
  BIOS/firmware support and 3-year on-site warranty.
- Aftermarket avoids Dell's 2–4× markup on RAM, SSDs and NICs.
- Standard parts stay upgradeable and replaceable.

### Naming clarification

- Dell's official name is **Precision 5860 Tower**.
- Resellers list it as **Precision T5860** or **T5860MT** — "T" = Tower,
  "MT" = Mini Tower chassis. This is the **same machine**, not a smaller model.
- The 5860 comes **only in this tower chassis**. There is no 5860 SFF.
- It is **not** Small Form Factor: full-height PCIe cards, 8 DIMM slots,
  2× M.2, 2× 3.5" bays, up to 1000 W PSU, full-length GPU support.
- The true SFF in Dell's line is the Precision 3460 SFF (low-profile cards,
  ~300 W, 4 DIMM) — **do not buy that**.

### Important CPU note

Xeon W-2400 processors have **no integrated graphics**. Order the cheapest
supported display GPU (NVIDIA T400/T1000) unless the quote already includes one
(an RTX A4000 bundled at a good price is fine).

---

## 3. Dell base order

Request this configuration. Prices are estimates; confirm the local quote.

| Option | Choose | Notes |
|---|---|---|
| Model | **Precision 5860 Tower** (T5860MT) | Regulatory model D03T |
| CPU | **Xeon W5-2465X** (16C/32T) | Fallback: W5-2455X (12C) if 16C is not offered |
| RAM | **16 GB (1×16 GB) DDR5-4800 ECC RDIMM** | Minimum — replaced by the aftermarket kit |
| OS drive | **1 TB M.2 NVMe** | Smallest offered is fine |
| PSU | **1000 W** | Do not accept 500/750 W |
| GPU | **NVIDIA T400 2 GB** (~$150–200) or T1000 (~$300–400) | Display only; keep an x16 slot for the future RTX 4000 Ada |
| 10G NIC | **None** | Add the Intel X710-DA2 aftermarket |
| Warranty | **3-year ProSupport on-site** | Must be tied to the service tag |
| OS | Windows 11 Pro or Ubuntu if offered | Ubuntu 24.04 is installed manually |
| **Base estimate** | **$5,500–6,500** | Varies by region, GPU and warranty |

### Quote request template

> Please quote a Dell Precision 5860 Tower with:
> Xeon W5-2465X (16C/32T), 16 GB (1×16 GB) DDR5-4800 ECC RDIMM, 1 TB M.2 NVMe,
> 1000 W PSU, basic display GPU (T400/T1000), 3-year ProSupport on-site.
> No 10G NIC, no extra RAM or storage.
> Please confirm: 8 DIMM slots, 2× M.2 2280, 5 PCIe slots, 1000 W PSU,
> Ubuntu 24.04 support, and the service-tag warranty coverage in our country.

### Regional marketplace note

A listing such as "Dell Precision T5860MT, Xeon W5-2455X, 32 GB, 1 TB,
RTX A4000" is a valid 5860 Tower base **if**:

- the CPU is acceptable (W5-2455X is 12C, not 16C),
- the PSU is 1000 W,
- the service tag shows Dell on-site warranty in your country,
- the seller is an authorized Dell partner with a proper invoice.

Marketplace imports with seller-only warranty are a risk — verify before paying.

---

## 4. GPU plan — display now, AI later

The app is CPU-only today: OpenCV/Qt raster work, no CUDA/OpenCL code path. The
GPU is therefore **not needed to record or review events**. It is needed for two
separate reasons:

1. **Display** — Xeon W-2400 has no integrated graphics, so the server needs a
   basic GPU for the admin monitor.
2. **Future AI** — when TensorRT / pylon AI defect detection lands, add an
   inference GPU. Keep the slot and power headroom for it now.

### 4.1 Buy now (display only)

| GPU | VRAM | Power | Price | Notes |
|---|---:|---:|---:|---|
| NVIDIA T400 | 2 GB | 30 W | $150–200 | Cheapest display option; 3× mini-DP |
| NVIDIA T1000 | 8 GB | 50 W | $300–400 | Better display headroom; 4× mini-DP |
| NVIDIA RTX A4000 | 16 GB | 140 W | ~$1,000 | Can serve as display + later AI budget card; single-slot |

Recommendation: order the **T400** (or T1000 if the quote bundles it) and keep
the RTX 4000 Ada budget for when the AI path is actually ready. If the seller's
base config already includes an A4000 at a small premium, taking it is
reasonable — it is a usable display GPU and a budget AI card.

### 4.2 Later (AI inference)

| GPU | VRAM | Power | Price | Notes |
|---|---:|---:|---:|---|
| **RTX 4000 Ada** | 20 GB ECC | 130 W | $1,500–1,800 | Recommended; single-slot, 4× DP, TensorRT |
| RTX A4000 | 16 GB | 140 W | ~$1,000 | Budget / previous generation |
| RTX A5000 | 24 GB | 230 W | ~$2,000–2,500 | More VRAM for larger models |
| RTX 4090 | 24 GB | 450 W | ~$1,800–2,500 | Fastest, but 3–4 slot / 8-pin, high heat; not ideal for 24/7 |
| NVIDIA L4 | 24 GB | 72 W | ~$2,800 | Low power, **no display outputs** — wrong for a tower with an admin monitor |

Recommendation: **RTX 4000 Ada 20 GB ECC**. It fits the 1000 W PSU with ample
headroom, is single-slot, drives four displays, and is the roadmap's TensorRT
pick. Do not buy it now — the app has no GPU code path yet.

### 4.3 Power, slot and software

- **Slot:** keep one free PCIe x16 slot. The 5860 has 5 slots; the display GPU
  and X710 use two, leaving room.
- **Power:** the 1000 W PSU covers the 130–140 W class easily. A 450 W RTX 4090
  also fits numerically but needs 3–4 PCIe 8-pin cables, more clearance and
  better case airflow — not recommended in the 5860.
- **Power cable:** confirm the PSU includes the 8-pin PCIe cable the future GPU
  needs; the T400/T1000 need none.
- **Driver:** Ubuntu 24.04 + `nvidia-driver-550` (or `ubuntu-drivers autoinstall`),
  CUDA 12.x, TensorRT 10.x / cuDNN.
- **Secure Boot:** enroll the MOK key at first reboot, or the NVIDIA driver will
  not load.
- **Disk:** pylon AI / vTools model repository needs ~50 GB; the 1 TB OS drive is
  enough, or use the archive HDD.
- **App impact:** none until the AI detection code path is added; current
  processing stays CPU-side.

### 4.4 GPU budget (not in current total)

- Display GPU: **$150–400** (likely already in the Dell base quote).
- Future RTX 4000 Ada: **+$1,500–1,800**, bought when AI work starts.
- Do not add the AI GPU to the first purchase; the 1000 W PSU and free x16 slot
  are the only preparations needed now.

---

## 5. Aftermarket parts and prices

USD street estimates, October 2026. DDR5 and NAND prices are volatile; treat
these as ±20% and confirm at order time.

| Item | Qty | Unit | Subtotal | Notes |
|---|---|---:|---:|---|
| 32 GB DDR5-4800 ECC **RDIMM** | 4 (128 GB) or 8 (256 GB) | $300–400 | $1,200–3,200 | Matched kit, 2Rx8, Samsung/Micron/SK Hynix |
| 4 TB M.2 Gen4 NVMe | 1 | $350–450 | $350–450 | Samsung 990 Pro 4TB or equivalent; second M.2 slot |
| Intel X710-DA2 2×10GbE SFP+ | 1 | $400–500 | $400–500 | Genuine Intel, full-height bracket, PCIe x8 |
| UPS 1500 VA line-interactive | 1 | $550–750 | $550–750 | APC SMT1500C or equivalent; NUT/apcupsd support |
| 8 TB HDD (archive, optional) | 1 | $150–250 | $150–250 | Uses one 3.5" bay |
| **Aftermarket total — 128 GB** | | | **$2,500–3,300** | Without optional HDD |
| **Aftermarket total — 256 GB** | | | **$3,700–4,900** | Without optional HDD |

### Notes on part choices

- **RAM:** Xeon W-2400 uses DDR5 **RDIMM**, not UDIMM. Buy all eight modules as
  one matched kit and remove the Dell module. Keep the original as a spare.
- **NVMe:** the 5860 has **2× M.2 2280** slots. One is the OS drive, the other is
  the 4 TB event drive. A consumer 4 TB Gen4 drive is fine for burst recording but
  has lower endurance (~0.6 DWPD) than an enterprise drive; archive old events to
  the HDD/NAS.
- **NIC:** the X710-DA2 is the PTP-safe choice. The onboard Dell 10G option is a
  single port and may not provide the same hardware timestamping support.
- **UPS:** size for server + switch; use a pure-sine-wave unit and connect it to
  NUT/apcupsd for graceful shutdown.

---

## 6. Budget summary

| Build | Dell base | Aftermarket | Total (excl. tax/shipping) |
|---|---:|---:|---:|
| 128 GB ECC | $5,500–6,500 | $2,500–3,300 | **$8,000–9,800** |
| 256 GB ECC | $5,500–6,500 | $3,700–4,900 | **$9,200–11,400** |

Add:

- 8 TB archive HDD: +$150–250
- Shop assembly/test: +$100–250 (self-assembly free)
- Tax, shipping, import duty: region-dependent

A factory-configured 256 GB / 4 TB / 10G machine typically quotes
**$14,000–18,000**, so the base + aftermarket route saves roughly $4,000–6,000.

The **AI GPU is not in these totals**: budget **+$1,500–1,800** for an RTX 4000
Ada when the TensorRT defect-detection work starts. The display GPU ($150–400)
is already covered by the Dell base estimate.

---

## 7. Verify before buying

### A. Model and seller

- [ ] Listing says **Precision 5860 Tower / T5860 / T5860MT**, regulatory D03T.
- [ ] Service tag resolves to Precision 5860 Tower on Dell's support site.
- [ ] Warranty is **Dell on-site**, in your country, tied to the service tag.
- [ ] Seller is an authorized Dell partner; invoice with VAT; return policy.
- [ ] Not a grey-market import with seller-only warranty.

### B. CPU and power

- [ ] CPU is **W5-2465X (16C/32T)**; W5-2455X (12C) only if accepted.
- [ ] PSU is **1000 W**, not 500/750 W.
- [ ] Case has the PCIe power cable needed by the future RTX 4000 Ada (single 8-pin, 130 W).

### C. Memory

- [ ] **8 DIMM slots**, DDR5 **ECC RDIMM**, max capacity 2 TB.
- [ ] Current module spec (size, speed, rank) is documented before ordering the kit.
- [ ] If Dell's 128 GB upgrade is within ~20% of the aftermarket kit, take it —
      RAM compatibility is the riskiest part of the DIY route.

### D. Storage and expansion

- [ ] **2× M.2 2280** slots (OS + event drive).
- [ ] **2× 3.5"** bays for archive HDD.
- [ ] **5 PCIe slots**; at least two free full-height slots (display GPU + X710).
- [ ] X710-DA2 has an x8 electrical slot with clearance.
- [ ] M.2 heatsink/clearance fits the chosen 4 TB drive.

### E. Display, GPU and OS

- [ ] Display GPU included (T400/T1000/A4000) — Xeon W-2400 has no iGPU.
- [ ] A free PCIe x16 slot and power headroom remain for the future RTX 4000 Ada.
- [ ] 1000 W PSU has the 8-pin PCIe cable the future GPU needs; T400/T1000 need none.
- [ ] If an A4000 is bundled, confirm it is the 16 GB single-slot card, not a
      used/mining unit, and that it drives the admin monitor.
- [ ] Do **not** accept an NVIDIA L4 as the display GPU — it has no display outputs.
- [ ] Ubuntu 24.04 is supported on the 5860 (Dell Ubuntu certification or manual install).
- [ ] NVIDIA driver path is planned (`nvidia-driver-550` / `ubuntu-drivers autoinstall`);
      Secure Boot MOK enrolment understood.
- [ ] pylon **26.06** supports Ubuntu 24.04 (Basler states 20.04 or newer).
- [ ] Noble `.deb` build path is ready (QtOpcUa/open62541 rebuilt against noble Qt);
      keep the jammy package as rollback.

### F. Network

- [ ] X710-DA2 is genuine Intel, full-height bracket, PCIe x8.
- [ ] 10G switch/uplink exists (SFP+ DAC or optics); otherwise the card falls back to 1G.
- [ ] PTP-capable leaf/core switches if cross-camera µs alignment is required.
- [ ] Jumbo frames (MTU 9000) can be enabled end-to-end.

### G. Aftermarket parts

- [ ] RAM is JEDEC DDR5-4800 ECC RDIMM 2Rx8, matched kit, with return policy.
- [ ] 4 TB NVMe is M.2 2280 Gen4 and fits the slot/heatsink.
- [ ] X710-DA2 is not a counterfeit card — buy from a distributor, not a marketplace.
- [ ] UPS is pure sine wave, 1500 VA, with NUT/apcupsd support.

### H. Environment and power

- [ ] 100–240 V supply, correct C13/C19 cable; Dell manual recommends a 16 A PDU.
- [ ] Tower location has filtered intake, ambient temperature headroom, and clearance.
- [ ] Noise acceptable for the control room.

### I. Retention and storage sizing

- [ ] Decide `EventRetentionCount` before ordering drives.
- [ ] At 2.3 MP, one 16-camera 2000-frame event is ~74 GB mono / ~148 GB mixed.
- [ ] 4 TB holds ~54 mono or ~27 mixed events — less than the current 200-event retention.
- [ ] For 200 events, plan ~15 TB mono / ~30 TB mixed, or archive to NAS and lower the count.

---

## 8. After delivery — setup checklist

- [ ] Update BIOS and firmware (Dell SupportAssist or manual).
- [ ] BIOS: set storage to **AHCI/NVMe** (disable Intel RST/RAID), confirm boot order.
- [ ] Install Ubuntu 24.04 LTS; handle Secure Boot/MOK for pylon/NVIDIA if needed.
- [ ] Install the 8 aftermarket DIMMs, run **memtest86+ for 24 h**.
- [ ] Install the 4 TB NVMe and the X710-DA2.
- [ ] Verify the display GPU and load the NVIDIA driver (`ubuntu-drivers autoinstall`);
      check the admin-monitor output.
- [ ] If the RTX 4000 Ada is fitted later: driver 550+, CUDA 12.x, TensorRT 10.x,
      MOK enrolment, ~50 GB for the pylon AI model repository.
- [ ] Verify the NIC: `lspci`, `ethtool -i`, `i40e`; update NVM only if required.
- [ ] Install pylon 26.06 and the noble `.deb`.
- [ ] Configure the 2×10G **LACP bond**, MTU 9000, and the 25 MB socket buffers
      shipped in `99-web-vision-pro.conf`.
- [ ] Configure PTP (`ptp4l`/`phc2sys`) if PTP switches/grandmaster are in use.
- [ ] Verify all 16 cameras in pylon Viewer and in the app; enable chunk timestamps.
- [ ] Run a 16-camera / 50 fps soak test plus one full record → review cycle.
- [ ] Configure UPS monitoring and graceful shutdown.
- [ ] Set event retention and the NAS/HDD archive job.
- [ ] Record the service tag and warranty expiry in the site documentation.

---

## 9. Do not buy

- Precision 3000-series SFF (low-profile cards, ~300 W, 4 DIMM)
- 500 W or 750 W PSU
- Non-ECC UDIMM memory
- A machine with only a single 1G NIC and no free PCIe slot
- HDD-only or SATA-SSD event storage
- Counterfeit X710 cards or grey-market Dell units with seller-only warranty
- A 4 TB QLC/Gen3 drive for the event disk
- An NVIDIA L4 as the only/display GPU (no display outputs)
- A 450 W RTX 4090 in the 5860 if quiet 24/7 operation and single-slot clearance matter
- An AI GPU before the TensorRT/pylon AI code path exists
- A used/mining GPU without warranty

---

## 10. Sources

- [16-camera roadmap — Ubuntu 24.04 edition](web-vision-pro-16cam-50fps-roadmap-ubuntu2404.html)
- [Roadmap §2d — GPU rationale](web-vision-pro-16cam-50fps-roadmap-ubuntu2404.html#2d-gpu-why-none-today-when-to-reconsider)
- [NVIDIA RTX 4000 Ada](https://www.nvidia.com/en-us/design-visualization/rtx-4000/)
- [Dell Precision 5860 Tower](https://www.dell.com/en-us/shop/desktop-computers/spd/precision5860tower)
- [Dell Precision 5860 Tower specifications](https://www.dell.com/support/manuals/en-sg/precision-5860-workstation/precision_5860_tower_ss/dimensions-and-weight)
- [Dell Precision 5860 spec sheet (PDF)](https://www.delltechnologies.com/assetlink/doc/en-my/precision-5860-tower-spec-sheet-en-dl1lsug-original.pdf.external)
- [Precision 5860 specs — IT Creations](https://www.itcreations.com/workstation-dell/dell-precision-5860-tower-workstation)
- [Precision T5860 Tower listing](https://www.serverbasket.net/p/dell-precision-t5860-tower-workstation/)
- [ASUS Pro WS W790-ACE](https://plugpcs.com/p/asus/pro-ws-w790-ace)
- [32 GB DDR5 ECC RDIMM listings](https://www.newegg.com/p/pl?d=32gb+ddr5+4800+ecc)
- [Seasonic Prime TX-1000 price history](https://pangoly.com/en/price-history/seasonic-prime-tx-1000)
- [Intel X710-DA2](https://www.amazon.com/Intel-Ethernet-Converged-X710-DA2-X710DA2/dp/B00NJ3ZC26)
