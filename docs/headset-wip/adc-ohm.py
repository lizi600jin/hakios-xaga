#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MT6368 (MT6895 / xaga) ACCDET + HP-impedance ADC <-> ohm converter.

Every constant below carries the vendor-source line it came from.  There are
TWO different ADCs in this codebase and they must not be conflated:

  (1) ACCDET jack ADC -- AUXADC channel 5, 12 bit, VREF 1840 mV
        drivers/iio/adc/mtk-spmi-pmic-adc.c:33    #define VOLT_FULL 1840
        drivers/iio/adc/mtk-spmi-pmic-adc.c:98    AUXADC_CHANNEL(ACCDET, 12)
        drivers/iio/adc/mtk-spmi-pmic-adc.c:177   AUXADC_REG(ACCDET, MT6368,
                                                  AUXADC_RQST0, BIT(5), AUXADC_ADC5_L)
        include/linux/mfd/mt6368/registers.h:28   MT6368_AUXADC_ADC5_L 0x1092
      Read through iio_read_channel_processed() (mt6368-accdet.c:897), i.e. in
      **millivolts**, then only compared against millivolt thresholds
      (key_check() mt6368-accdet.c:1029-1054, moisture code).  The vendor
      source contains no function that turns this channel into ohms.

  (2) HP impedance ADC -- AUXADC channel 12 (HPOFS_CAL), 15 bit
        drivers/iio/adc/mtk-spmi-pmic-adc.c:99    AUXADC_CHANNEL(HPOFS_CAL, 15)
        drivers/iio/adc/mtk-spmi-pmic-adc.c:178   HPOFS_CAL -> AUXADC_ADC9_L
        sound/soc/codecs/mt6368.c:4981-4992       closed form R = V / I
        sound/soc/codecs/mt6368.c:5027-5213       phase state machine
      This is the only path that yields an ohm number, and that number is what
      mt6368_accdet_read_audio_res() consumes
      (sound/soc/codecs/mt6368-accdet.c:845-863, 5000 ohm test at :850).

Scale note (very easy to get wrong): detect_impedance() sums `num_detect = 8`
samples and hands the *sum* to calculate_impedance(), which divides by
detect_times at the very end (mt6368.c:5010).  Fields named `*_sum` are
8-sample sums; `per_sample` is that sum / 8.
"""

import argparse
import json
import os
import sys

# ------------------------------------------------------------------ provenance
KERNEL_REPO = "/home/lizi/IOS/work-xaga/k-xaga"
KERNEL_SHA = "0a8d13c1a180fed82803f56337a0b065ff704fb4"   # git rev-parse HEAD
SRC_ACCDET = "sound/soc/codecs/mt6368-accdet.c"
SRC_CODEC = "sound/soc/codecs/mt6368.c"
SRC_ADC = "drivers/iio/adc/mtk-spmi-pmic-adc.c"
SRC_REGS = "include/linux/mfd/mt6368/registers.h"

# ------------------------------------------------------------------ constants
VREF_MV = 1840                 # SRC_ADC:33   VOLT_FULL (mt6368 binding)
ACCDET_BITS = 12               # SRC_ADC:98   AUXADC_CHANNEL(ACCDET, 12)
ACCDET_FULL = 1 << ACCDET_BITS
HP_BITS = 15                   # SRC_ADC:99   AUXADC_CHANNEL(HPOFS_CAL, 15)
HP_FULL = 1 << HP_BITS         # 32768

FORMULA_NUM = 3600000          # SRC_CODEC:4989  val = 3600000 / pcm_offset * aux_diff
FORMULA_DIV = 7832             # SRC_CODEC:4991  DIV_ROUND_CLOSEST(val, 7832)
DETECT_TIMES = 8               # SRC_CODEC:5029  const unsigned int num_detect = 8

PHASE_DC = {"0": 288, "1": 1440, "2": 6048}      # SRC_CODEC:5043/5045/5047
DC_STEP = 96                                     # SRC_CODEC:5041
RES_1ST_THRESHOLD = 250                          # SRC_CODEC:5049
RES_2ND_THRESHOLD = 1000                         # SRC_CODEC:5051
AUXCABLE_IMPEDANCE = 5000                        # SRC_CODEC:5037
AUXADC_UPPER_BOUND = 32630                       # SRC_CODEC:5039

LINE_OUT_THRESHOLD_OHM = 5000                    # SRC_ACCDET:850
RET_LT_5K = -1                                   # SRC_ACCDET:61
RET_GT_5K = 0                                    # SRC_ACCDET:62

PARALLEL_OHM = 0                                 # mt6368.h:5959 -> correction disabled
MICBIAS_MV = 2800                                # SRC_ACCDET:3024-3031 (mic_vol=8 -> 2.8V)

# DTS key thresholds, in millivolts (mt6895.dtsi:2167-2169)
THREE_KEY_MV = {"mid": 0x50, "up": 0xDC, "down": 0x190}        # 80 / 220 / 400
FOUR_KEY_MV = {"mid": 0x3A, "voice": 0x79, "up": 0xC0, "down": 0x190}  # 58/121/192/400
THREE_KEY_CDD_MV = {"mid": 0x79, "up": 0xC0, "down": 0x258}    # 121 / 192 / 600

KEY_MD, KEY_UP, KEY_DW, KEY_AS, KEY_NONE = "MD_KEY", "UP_KEY", "DW_KEY", "AS_KEY", "NO_KEY"


# ------------------------------------------------------------------ helpers
def div_round_closest(a, b):
    """Linux DIV_ROUND_CLOSEST for b > 0."""
    if b == 0:
        raise ZeroDivisionError("DIV_ROUND_CLOSEST by zero")
    if a < 0:
        return -((-a + b // 2) // b)
    return (a + b // 2) // b


def cdiv(a, b):
    """C truncating integer division (toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ------------------------------------------------------------------ ACCDET ADC
def accdet_code_to_mv(code):
    """12-bit code -> millivolts, SRC_ADC:370-371.

    *val = auxadc_out * r_ratio[0] * VOLT_FULL / r_ratio[1] >> res
    with the DTS default r_ratio 1/1 (SRC_ADC:629-635) this is code*1840/4096.
    """
    if not 0 <= code < ACCDET_FULL:
        raise ValueError("ACCDET code out of 12-bit range: %d" % code)
    return code * VREF_MV / ACCDET_FULL


def mv_to_accdet_code(mv):
    """Millivolts -> nearest 12-bit code."""
    if mv < 0:
        raise ValueError("negative millivolts")
    return min(int(round(mv * ACCDET_FULL / VREF_MV)), ACCDET_FULL - 1)


def key_bucket_3key(mv, table=None):
    """SRC_ACCDET:1043-1052 (three-key branch of key_check)."""
    t = table or THREE_KEY_MV
    if t["up"] <= mv < t["down"]:
        return KEY_DW
    if t["mid"] <= mv < t["up"]:
        return KEY_UP
    if mv < t["mid"]:
        return KEY_MD
    return KEY_NONE


def key_bucket_4key(mv, table=None):
    """SRC_ACCDET:1031-1042 (four-key branch of key_check)."""
    t = table or FOUR_KEY_MV
    if t["up"] <= mv < t["down"]:
        return KEY_DW
    if t["voice"] <= mv < t["up"]:
        return KEY_UP
    if t["mid"] <= mv < t["voice"]:
        return KEY_AS
    if mv < t["mid"]:
        return KEY_MD
    return KEY_NONE


def key_divider_mv(r_key, r_bias, v_bias_mv=MICBIAS_MV):
    """Node voltage for a key resistor, assuming a plain divider to ground.

    NOT a vendor formula -- the schematic bias resistor is absent from source.
    Provided only so the inverse can be applied once r_bias is measured.
    """
    if r_bias <= 0 or r_key < 0:
        raise ValueError("bad resistance")
    return v_bias_mv * r_key / (r_bias + r_key)


def key_ohm_for_mv(mv, r_bias, v_bias_mv=MICBIAS_MV):
    """Inverse of key_divider_mv; returns ohm or None when the node is pinned."""
    if not 0 <= mv < v_bias_mv:
        return None
    return r_bias * mv / (v_bias_mv - mv)


# ------------------------------------------------------------------ HP path
def hp_adc_to_ohm(adc_delta, phase, efuse_cal=0, detect_times=DETECT_TIMES):
    """ADC delta -> ohm, exact copy of the vendor integer math.

    SRC_CODEC:4989-4991  val = 3600000 / pcm_offset * aux_diff; R = val / 7832
    SRC_CODEC:5010       r_tmp = DIV_ROUND_CLOSEST(r_tmp, detect_times)
    SRC_CODEC:5012-5019  efuse: r = DIV_ROUND_CLOSEST(r*128 + cal, 128)

    `adc_delta` is dc_value (SRC_CODEC:5008) = stepped reading minus DC=0
    baseline, summed over `detect_times` samples.
    """
    dc = PHASE_DC[str(phase)]
    val = cdiv(FORMULA_NUM, dc) * adc_delta      # truncating divide first, as in C
    r = div_round_closest(val, FORMULA_DIV)
    r = div_round_closest(r, detect_times)
    if efuse_cal != 0 and r != 0:
        r = div_round_closest(r * 128 + efuse_cal, 128)
    return r


def hp_ohm_to_adc(ohm, phase, detect_times=DETECT_TIMES):
    """ohm -> summed ADC delta (inverse of hp_adc_to_ohm with cal=0)."""
    k = cdiv(FORMULA_NUM, PHASE_DC[str(phase)])
    approx = int(round(ohm * detect_times * FORMULA_DIV / k))
    best, best_err = None, None
    for d in range(max(0, approx - 4), approx + 5):
        got = hp_adc_to_ohm(d, phase, 0, detect_times)
        err = abs(got - ohm)
        if best_err is None or err < best_err or (err == best_err and d < best):
            best, best_err = d, err
        if err == 0:
            break
    return best


def hp_adc_interval_for_ohm(ohm, phase, detect_times=DETECT_TIMES):
    """Contiguous summed-delta interval reporting exactly `ohm` (inclusive)."""
    d0 = hp_ohm_to_adc(ohm, phase, detect_times)
    if hp_adc_to_ohm(d0, phase, 0, detect_times) != ohm:
        return None, None
    lo = d0
    while lo > 0 and hp_adc_to_ohm(lo - 1, phase, 0, detect_times) == ohm:
        lo -= 1
    hi = d0
    while hp_adc_to_ohm(hi + 1, phase, 0, detect_times) == ohm:
        hi += 1
    return lo, hi


def phase_k(phase):
    """Integer coefficient: R = k * per_sample_delta / 7832 (SRC_CODEC:4989)."""
    return cdiv(FORMULA_NUM, PHASE_DC[str(phase)])


def phase_ceiling_ohm(phase):
    """Largest ohm value still inside 15 bit at this phase."""
    return phase_k(phase) * (HP_FULL - 1) // FORMULA_DIV


def phase_for_ohm(ohm):
    """Phase the state machine lands in (SRC_CODEC:5123-5134, :5152-5163, :5165-5175)."""
    if ohm < RES_1ST_THRESHOLD:
        return "2"
    if ohm < RES_2ND_THRESHOLD:
        return "1"
    return "0"


def saturation_limit_ohm():
    """Largest ohm value that still avoids the "treat as cable" clamp.

    SRC_CODEC:5140-5142 compares `detect_sum / num_detect` -- a C integer
    division, so the clamp fires at floor(sum/8) > 32630, i.e. sum >= 261048.
    The largest sum that survives is 8*32630+7 = 261047.
    """
    return hp_adc_to_ohm(DETECT_TIMES * AUXADC_UPPER_BOUND + (DETECT_TIMES - 1), "0")


def classify_audio_res(ohm):
    """Verdict of mt6368_accdet_read_audio_res(), SRC_ACCDET:845-863."""
    return RET_GT_5K if ohm >= LINE_OUT_THRESHOLD_OHM else RET_LT_5K


def ohm_resolution(phase):
    """Ohm per LSB of the per-sample delta at this phase."""
    return phase_k(phase) / FORMULA_DIV


# ------------------------------------------------------------------ table
TABLE_OHMS = [
    # 0 .. 250 (phase 2 territory, but 0 hits the delta==0 clamp)
    0, 1, 2, 5, 10, 15, 20, 32, 50, 70, 100, 135, 150, 200, 235, 249,
    # 250 .. 1000 (phase 1)
    250, 300, 470, 500, 680, 700, 900, 999,
    # 1000 .. 52079 (phase 0) -- includes the 5000 ohm decision boundary
    1000, 1500, 2000, 2200, 3000, 4000, 4700, 4999, 5000, 5100,
    6000, 8000, 10000, 15000, 20000, 30000, 40000, 50000, 52078, 52079,
    # saturated: driver reports 5000 no matter what
    52080, 65535, 100000,
]

# Ohms that bracket the only decision the driver actually makes.
BOUNDARY_OHMS = [4997, 4998, 4999, 5000, 5001, 5002]


def build_table():
    rows = []
    sat = saturation_limit_ohm()
    for ohm in TABLE_OHMS:
        ph = phase_for_ohm(ohm)
        row = {"ohm": ohm, "phase": ph, "dc": PHASE_DC[ph],
               "phase_range_ohm": [0, phase_ceiling_ohm(ph)],
               "ohm_per_lsb": round(ohm_resolution(ph), 6)}
        if ohm > sat:
            row.update({
                "adc_sum": None, "adc_per_sample": None,
                "adc_per_sample_min": None, "adc_per_sample_max": None,
                "reported_ohm": AUXCABLE_IMPEDANCE,
                "verdict": classify_audio_res(AUXCABLE_IMPEDANCE),
                "note": "ADC saturates (>%d per sample): SRC_CODEC:5140-5142 "
                        "assigns auxcable_impedance=5000 (SRC_CODEC:5037)" % AUXADC_UPPER_BOUND,
            })
        else:
            d = hp_ohm_to_adc(ohm, ph)
            lo, hi = hp_adc_interval_for_ohm(ohm, ph)
            rep = hp_adc_to_ohm(d, ph)
            ps = d / DETECT_TIMES
            note = "round-trip exact"
            if lo is None:
                note = "not representable at 1 LSB resolution; nearest reports %d" % rep
            reported = rep
            if d == 0:
                # SRC_CODEC:5110-5121: at dc_phase0 a zero delta reads as "no load".
                reported = AUXCABLE_IMPEDANCE
                note = ("ideal delta 0, but SRC_CODEC:5110-5121 maps delta==0 at "
                        "dc_phase0 to auxcable_impedance=5000 -> driver can never "
                        "report 0 ohm")
            row.update({
                "adc_sum": d,
                "adc_per_sample": round(ps, 3),
                "adc_per_sample_min": round(lo / DETECT_TIMES, 3) if lo is not None else None,
                "adc_per_sample_max": round(hi / DETECT_TIMES, 3) if hi is not None else None,
                "reported_ohm": reported,
                "verdict": classify_audio_res(reported),
                "note": note,
            })
        rows.append(row)
    return rows


def build_boundary_table():
    """Dense view around the 5000 ohm LINE_OUT threshold (SRC_ACCDET:850)."""
    out = []
    for ohm in BOUNDARY_OHMS:
        ph = phase_for_ohm(ohm)
        d = hp_ohm_to_adc(ohm, ph)
        lo, hi = hp_adc_interval_for_ohm(ohm, ph)
        out.append({
            "ohm": ohm,
            "phase": ph,
            "adc_sum": d,
            "adc_per_sample": round(d / DETECT_TIMES, 3),
            "adc_per_sample_min": round(lo / DETECT_TIMES, 3) if lo is not None else None,
            "adc_per_sample_max": round(hi / DETECT_TIMES, 3) if hi is not None else None,
            "read_audio_res": classify_audio_res(ohm),
            "verdict": "LINE_OUT" if classify_audio_res(ohm) == RET_GT_5K else "normal device",
        })
    return out


def build_mv_table():
    out = []
    for code in (0, 100, 178, 300, 490, 890, 1000, 2048, 4095):
        mv = accdet_code_to_mv(code)
        out.append({
            "adc_code": code,
            "mv": round(mv, 3),
            "key_3key": key_bucket_3key(mv),
            "key_4key": key_bucket_4key(mv),
        })
    return out


def build_document():
    return {
        "meta": {
            "project": "HakiOS / MT6895 xaga / MT6368 ACCDET",
            "kernel_repo": KERNEL_REPO,
            "kernel_head": KERNEL_SHA,
            "sources": [SRC_ACCDET, SRC_CODEC, SRC_ADC, SRC_REGS],
            "generator": "docs/headset-wip/adc-ohm.py --emit",
            "units": {"adc_sum": "sum of %d samples (int)" % DETECT_TIMES,
                      "adc_per_sample": "adc_sum / %d" % DETECT_TIMES,
                      "ohm": "ohm"},
        },
        "accdet_jack_adc": {
            "channel": 5,
            "bits": ACCDET_BITS,
            "vref_mv": VREF_MV,
            "code_to_mv": "mv = code * 1840 / 4096",
            "mv_per_lsb": VREF_MV / ACCDET_FULL,
            "registers": {
                "request": "MT6368_AUXADC_RQST0 0x1108 bit5 (SRC_ADC:177)",
                "data": "MT6368_AUXADC_ADC5_L 0x1092, 15-bit word, valid bit15, "
                        "payload bits[11:0] (SRC_REGS:28, SRC_ADC:245,270)",
                "mux_sel": "ACCDET_CON0 0x2688 bit0 ACCDET_AUXADC_SEL",
                "mux_sw": "ACCDET_CON0 0x2688 bit1 ACCDET_AUXADC_SW",
            },
            "efuse_offset": {
                "nvmem_byte": "91*2 (SRC_ACCDET:927)",
                "extract": "val&0xFF; if(>128) -=256; >>1  (SRC_ACCDET:928-931)",
                "unit": "mV, subtracted in accdet_get_auxadc() (SRC_ACCDET:904-911)",
                "range_mv": [-64, 63],
            },
            "ohm_formula": None,
            "ohm_note": "source contains no code path from this channel to ohm",
        },
        "hp_impedance_adc": {
            "channel": 12,
            "bits": HP_BITS,
            "channel_src": "SRC_ADC:99 (AUXADC_CHANNEL(HPOFS_CAL, 15)); data register SRC_ADC:178 -> MT6368_AUXADC_ADC9_L",
            "code_to_ohm": "R = (3600000/dc) * delta_sum / 7832 / 8",
            "phases": [
                {"phase": p, "dc": PHASE_DC[p], "k": phase_k(p),
                 "ohm_per_lsb_per_sample": round(ohm_resolution(p), 6),
                 "max_ohm_15bit": phase_ceiling_ohm(p),
                 "src": "mt6368.c:5043-5047"}
                for p in ("0", "1", "2")
            ],
            "saturation_limit_ohm": saturation_limit_ohm(),
            "saturation_src": "mt6368.c:5039,5140-5142",
            "efuse_cal": {
                "byte": "0xCA (202) = bit 1616..1623 (mt6368.c:5249-5254)",
                "fields": "HPDET_COMP[6:0] + sign bit7 (mt6368.c:5260-5262)",
                "applied": "r = (r*128 + cal)/128 -> additive cal/128 ohm (mt6368.c:5016-5018)",
                "range_ohm": [-0.99, 0.99],
            },
            "parallel_ohm": PARALLEL_OHM,
            "parallel_src": "mt6368.h:5959 -> block at mt6368.c:5180-5187 is dead code",
        },
        "key_thresholds": {
            "source": "mt6895.dtsi:2167-2169, parsed at mt6368-accdet.c:2670-2696",
            "three_key_mv": THREE_KEY_MV,
            "four_key_mv": FOUR_KEY_MV,
            "three_key_cdd_mv": THREE_KEY_CDD_MV,
            "efuse_4key_src": "mt6368-accdet.c:998-1027 (only when DTS omits the property)",
            "ohm_values_in_source": False,
            "ohm_note": "vendor source has no ohm table for hook/volume keys; "
                        "DTS thresholds are millivolts. The usual 0/70/135/235/470 ohm "
                        "numbers do not appear anywhere in these files.",
        },
        "table": build_table(),
        "boundary_5000": build_boundary_table(),
        "accdet_mv_table": build_mv_table(),
        "not_in_source": [
            "any ADC-code -> ohm function for AUXADC channel 5 (ACCDET)",
            "hook/volume key resistance values in ohm",
            "mic-bias series resistor value / schematic divider",
            "an in-tree caller of mt6368_accdet_read_audio_res() "
            "(sparse clone could not fetch the rest: promisor remote TLS failure)",
        ],
    }


# ------------------------------------------------------------------ selftest
def selftest():
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    # 1. ACCDET LSB and DTS thresholds -> codes
    ck("ACCDET LSB = 1840/4096 = 0.44921875 mV",
       abs(accdet_code_to_mv(1) - 0.44921875) < 1e-12,
       repr(accdet_code_to_mv(1)))
    ck("80 mV -> code 178", mv_to_accdet_code(0x50) == 178, mv_to_accdet_code(0x50))
    ck("220 mV -> code 490", mv_to_accdet_code(0xDC) == 490, mv_to_accdet_code(0xDC))
    ck("400 mV -> code 890", mv_to_accdet_code(0x190) == 890, mv_to_accdet_code(0x190))

    # 2. the truncating divide in the C formula is observable
    ck("C truncation 3600000/6048 == 595 (not 595.238)",
       cdiv(3600000, 6048) == 595, cdiv(3600000, 6048))

    # 3. phase coefficients
    ck("k(phase0)=12500 k(phase1)=2500 k(phase2)=595",
       [phase_k(p) for p in ("0", "1", "2")] == [12500, 2500, 595],
       [phase_k(p) for p in ("0", "1", "2")])

    # 4. anchors: 8-sample sum 25064 gives 5000 / 1000 / 238 by phase
    ck("sum 25064 -> phase0 5000 ohm", hp_adc_to_ohm(25064, "0") == 5000,
       hp_adc_to_ohm(25064, "0"))
    ck("sum 25064 -> phase1 1000 ohm", hp_adc_to_ohm(25064, "1") == 1000,
       hp_adc_to_ohm(25064, "1"))
    ck("sum 25064 -> phase2 238 ohm", hp_adc_to_ohm(25064, "2") == 238,
       hp_adc_to_ohm(25064, "2"))

    # 5. per-sample anchor that matches the vendor comment at mt6368.c:5140
    ck("phase0 per-sample 3133 -> 5000 ohm",
       hp_adc_to_ohm(3133 * 8, "0") == 5000, hp_adc_to_ohm(3133 * 8, "0"))

    # 6. round-trip on the interesting ohms
    bad = []
    for ohm in (0, 50, 70, 135, 235, 249, 250, 470, 999, 1000, 2200, 5000):
        ph = phase_for_ohm(ohm)
        d = hp_ohm_to_adc(ohm, ph)
        if hp_adc_to_ohm(d, ph) != ohm:
            bad.append((ohm, ph, d, hp_adc_to_ohm(d, ph)))
    ck("round-trip exact for 12 sample ohms", not bad, bad)

    # 7. the 5000 ohm boundary, both sides
    ck("classify 4999 -> -1", classify_audio_res(4999) == RET_LT_5K)
    ck("classify 5000 -> 0", classify_audio_res(5000) == RET_GT_5K)

    # 8. phase map including the two thresholds
    ck("phase map 249/250/999/1000/5000",
       [phase_for_ohm(x) for x in (249, 250, 999, 1000, 5000)] == ["2", "1", "1", "0", "0"],
       [phase_for_ohm(x) for x in (249, 250, 999, 1000, 5000)])

    # 9. phase ceilings stay inside 15 bit
    ck("phase2 ceiling < 2500", phase_ceiling_ohm("2") < 2500, phase_ceiling_ohm("2"))
    ck("phase1 ceiling < 10500", phase_ceiling_ohm("1") < 10500, phase_ceiling_ohm("1"))
    ck("phase0 ceiling > 50000", phase_ceiling_ohm("0") > 50000, phase_ceiling_ohm("0"))

    # 10. saturation boundary is exact: floor(sum/8) > 32630 fires at sum 261048
    ck("saturation limit == 52079 ohm", saturation_limit_ohm() == 52079,
       saturation_limit_ohm())
    ck("sum 261047 (32630 avg) survives -> 52079",
       hp_adc_to_ohm(261047, "0") == 52079, hp_adc_to_ohm(261047, "0"))
    ck("sum 261048 (32631 avg) is the first clamped sum",
       261047 // DETECT_TIMES == AUXADC_UPPER_BOUND
       and 261048 // DETECT_TIMES == AUXADC_UPPER_BOUND + 1,
       (261047 // DETECT_TIMES, 261048 // DETECT_TIMES))
    ck("52080 saturates -> 5000 ohm",
       [r for r in build_table() if r["ohm"] == 52080][0]["reported_ohm"] == 5000)

    # 11. efuse calibration is additive cal/128 ohm, not a percentage
    ck("efuse +127 adds ~1 ohm at 5000",
       hp_adc_to_ohm(25064, "0", efuse_cal=127) == 5001,
       hp_adc_to_ohm(25064, "0", efuse_cal=127))

    # 12. key buckets at the DTS edges
    ck("3-key 0 mV -> MD", key_bucket_3key(0) == KEY_MD)
    ck("3-key 79.9 -> MD, 80 -> UP",
       (key_bucket_3key(79.9), key_bucket_3key(80)) == (KEY_MD, KEY_UP))
    ck("3-key 220 -> DW, 400 -> NO_KEY",
       (key_bucket_3key(220), key_bucket_3key(400)) == (KEY_DW, KEY_NONE))
    ck("4-key 57 -> MD, 58 -> AS",
       (key_bucket_4key(57), key_bucket_4key(58)) == (KEY_MD, KEY_AS))

    # 13. divider inverse: monotonic and exact round-trip
    v = key_divider_mv(135, 2200)
    ck("divider 135R/2200R -> %.2f mV then back" % v,
       abs(key_ohm_for_mv(v, 2200) - 135) < 1e-9, key_ohm_for_mv(v, 2200))

    # 14. table shape
    rows = build_table()
    ck("table has %d rows" % len(TABLE_OHMS), len(rows) == len(TABLE_OHMS), len(rows))
    ck("boundary table brackets 5000 with 6 rows",
       len(build_boundary_table()) == 6, len(build_boundary_table()))
    bd = {r["ohm"]: r["read_audio_res"] for r in build_boundary_table()}
    ck("boundary verdict flips exactly at 5000",
       [bd[o] for o in (4999, 5000)] == [RET_LT_5K, RET_GT_5K], bd)
    ck("table 0 ohm -> sum 0", rows[0]["adc_sum"] == 0, rows[0]["adc_sum"])
    ck("table 100k reported as 5000",
       rows[-1]["reported_ohm"] == 5000, rows[-1]["reported_ohm"])

    ok = sum(1 for _, c, _ in checks if c)
    for name, cond, detail in checks:
        print("%s  %s%s" % ("PASS" if cond else "FAIL", name,
                            "" if cond else "  <- " + str(detail)))
    print("\n%d/%d assertions passed" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


# ------------------------------------------------------------------ markdown
def emit_markdown(doc):
    sat = doc["hp_impedance_adc"]["saturation_limit_ohm"]
    L = []
    A = L.append
    A("# MT6368 ACCDET / HP 阻抗 ADC → 欧姆 查找表")
    A("")
    A("自动生成：`python3 adc-ohm.py --emit`。所有数字来自厂商源码，行号随行标注。")
    A("内核树 `%s` @ `%s`。" % (doc["meta"]["kernel_repo"], doc["meta"]["kernel_head"]))
    A("")
    A("## 1. 结论")
    A("")
    A("**ACCDET 自己不测阻抗。** `mt6368-accdet.c` 全文没有任何把 ADC 码换成欧姆的代码：")
    A("插孔侧只用 (a) `0x26b6` bit[7:6] 的 AB 比较器状态判类型，"
      "(b) AUXADC 通道 5 读**毫伏**，去和 DTS/efuse 的**毫伏**阈值比。")
    A("")
    A("- ACCDET 的 ADC：通道 5、12 bit、VREF **1840 mV**（`mtk-spmi-pmic-adc.c:33,98,177`）。")
    A("  唯一消费点 `accdet_get_auxadc()` → `key_check()`（`mt6368-accdet.c:892-914,1029-1054`）。")
    A("- 真正的欧姆值来自**另一条路**：codec 的 HP 阻抗测量，AUXADC 通道 12（HPOFS_CAL，15 bit），")
    A("  注入 DC 偏移后算 `R = 3600000/dc × Δ / 7832`（`mt6368.c:4981-4992`）。")
    A("  这个欧姆数最终喂给 `mt6368_accdet_read_audio_res()`，在 **5000 Ω** 处二分类"
      "（`mt6368-accdet.c:845-863`）。")
    A("")
    A("因此本表给的是「欧姆 → 期望 ADC 增量」，不是「ACCDET 通道码」。两者不是同一个 ADC。")
    A("")
    A("## 2. 公式")
    A("")
    A("```")
    A("R[ohm] = (3600000 / dc) * Δsum / 7832 / 8")
    A("")
    A("dc    = 288 (phase0) | 1440 (phase1) | 6048 (phase2)   mt6368.c:5043-5047")
    A("Δsum  = Σ_{i=0..7}( adc(dc_i) - adc(dc=0) )             mt6368.c:5008,5029,5010")
    A("8     = num_detect，最后一步才除                           mt6368.c:5010")
    A("```")
    A("")
    A("注意 `3600000 / dc` 是**整数截断**再乘：phase2 得 595 而非 595.238（`mt6368.c:4989`）。")
    A("")
    A("### 3 个 phase 的系数与量程")
    A("")
    A("| phase | dc | k=3600000/dc (int) | Ω/LSB（单次采样） | 15 bit 上限 | 源码 |")
    A("|---|---|---|---|---|---|")
    for p in doc["hp_impedance_adc"]["phases"]:
        A("| %s | %d | %d | %.4f | %d Ω | `mt6368.c:5043-5047` |"
          % (p["phase"], p["dc"], p["k"], p["ohm_per_lsb_per_sample"], p["max_ohm_15bit"]))
    A("")
    A("phase 选择（`mt6368.c:5123-5134`）：先在 dc=288 用**单次**采样算 `pick_impedance`，")
    A("`<250` 走 phase2，`<1000` 走 phase1，否则留在 phase0 用 8 次采样正式测量。")
    A("")
    A("### 饱和与钳位")
    A("")
    A("- 单次采样均值 > **32630**（`mt6368.c:5039`）时直接赋 `auxcable_impedance = 5000` 并 break"
      "（`mt6368.c:5140-5142`）。这里比的是 C 整数除法 `detect_sum / num_detect`，即"
      "`floor(sum/8) > 32630`，首个触发点是 sum=261048。折合 **%d Ω** 以上一律报 5000。" % sat)
    A("- Δ 恰为 0 时（dc=288 处 `dc_sum/8 == detect_sum`，`mt6368.c:5110-5121`）也报 5000，"
      "所以 0 Ω 永远报不出来：0 Ω 的码 0 会被当成「没插」。")
    A("- `PARALLEL_OHM` 为 0（`mt6368.h:5959`），`mt6368.c:5180-5187` 的并联修正是死代码。")
    A("")
    A("### efuse 校准")
    A("")
    A("`HPDET_COMP[6:0]` + 符号位 bit7，取自 efuse 字节 0xCA（`mt6368.c:5249-5262`），")
    A("按 `r = (r*128 + cal)/128` 施加（`mt6368.c:5016-5018`）——这是**加性** `cal/128` 欧姆，")
    A("即 ±0.99 Ω，不是百分比。")
    A("")
    A("## 3. 查找表：欧姆 → 期望 ADC 码")
    A("")
    A("`Δsum` = 8 次采样增量之和；`Δ/sample` = 单次采样增量（= 真实 ADC 读数差）。")
    A("")
    A("| Ω | phase | dc | Δsum | Δ/sample | 期望区间(Δ/sample) | 回报 Ω | read_audio_res | 备注 |")
    A("|---|---|---|---|---|---|---|---|---|")
    for r in doc["table"]:
        if r["adc_sum"] is None:
            A("| %d | %s | %d | — | — | — | %d | %d | %s |"
              % (r["ohm"], r["phase"], r["dc"], r["reported_ohm"], r["verdict"], r["note"]))
        else:
            rng = ("%s..%s" % (r["adc_per_sample_min"], r["adc_per_sample_max"])
                   if r["adc_per_sample_min"] is not None else "—")
            A("| %d | %s | %d | %d | %.2f | %s | %d | %d | %s |"
              % (r["ohm"], r["phase"], r["dc"], r["adc_sum"], r["adc_per_sample"],
                 rng, r["reported_ohm"], r["verdict"], r["note"]))
    A("")
    A("`read_audio_res` 列：`0` = `RET_GT_5K`（判为 LineOut），`-1` = `RET_LT_5K`"
      "（判为普通耳机）。定义在 `mt6368-accdet.c:61-62,849-851`。")
    A("")
    A("## 4. 5000 Ω 判定边界（驱动唯一的二分类）")
    A("")
    A("`mt6368_accdet_read_audio_res()` 只做一件事：`res_value < 5000 ? RET_LT_5K : RET_GT_5K`"
      "（`mt6368-accdet.c:849-851`）。下表给出边界附近的精确码。")
    A("")
    A("| Ω | phase | Δsum | Δ/sample | 区间 | read_audio_res | 判为 |")
    A("|---|---|---|---|---|---|---|")
    for r in doc["boundary_5000"]:
        A("| %d | %s | %d | %.2f | %s..%s | %d | %s |"
          % (r["ohm"], r["phase"], r["adc_sum"], r["adc_per_sample"],
             r["adc_per_sample_min"], r["adc_per_sample_max"],
             r["read_audio_res"], r["verdict"]))
    A("")
    A("phase 0 的单次采样分辨率是 **1.596 Ω/LSB**，但驱动把 8 次采样**求和**后才除"
      "（`mt6368.c:5010`），")
    A("等效 0.1995 Ω/LSB，最后又 `DIV_ROUND_CLOSEST` 成整数欧姆。")
    A("所以可分辨的最小台阶是 **1 Ω = Δsum 5 个 LSB**（0.625 LSB/采样），"
      "上表 4999→5000 正好差 5。")
    A("要更细只能加采样次数，不是靠公式精度。")
    A("")
    A("## 5. MIC 按键（hook / volume）")
    A("")
    A("按键走的是**通道 5 的毫伏**，阈值来自 DTS（毫伏），不是欧姆：")
    A("")
    A("`headset-three-key-threshold = <0 0x50 0xdc 0x190>`（`mt6895.dtsi:2167`）→"
      " mid=80 / up=220 / down=400 mV。")
    A("解析：`memcpy(&three_key, three_key+1, ...)`，第 0 个元素被丢弃（`mt6368-accdet.c:2685-2697`）。")
    A("")
    A("`key_check()`（`mt6368-accdet.c:1029-1054`）的判据：")
    A("")
    A("| 电压区间 (mV) | 3-key | 4-key (`<0 0x3a 0x79 0xc0 0x190>`, `mt6895.dtsi:2169`) |")
    A("|---|---|---|")
    A("| [0, 58) | MD_KEY | MD_KEY |")
    A("| [58, 80) | MD_KEY | AS_KEY |")
    A("| [80, 121) | UP_KEY | AS_KEY |")
    A("| [121, 192) | UP_KEY | UP_KEY |")
    A("| [192, 220) | UP_KEY | DW_KEY |")
    A("| [220, 400) | DW_KEY | DW_KEY |")
    A("| ≥ 400 | NO_KEY | NO_KEY |")
    A("")
    A("xaga 用的是 `headset-key-mode = <0>` → `ACCDET_THREE_KEY`（`mt6895.dtsi:2166`，")
    A("`mt6368-accdet.c:2660-2665`），所以生效的是 3-key 那一列。")
    A("")
    A("### 5.1 按键欧姆值：源码中不存在")
    A("")
    A("0/70/135/235/470 Ω 这类数字在这些文件里**一次都没出现**。要用欧姆，必须先有原理图上的")
    A("mic-bias 串联电阻 `R_bias`。有了它才能反解：")
    A("")
    A("```")
    A("V_node = V_bias * R_key / (R_bias + R_key)        V_bias = 2800 mV  (mt6368-accdet.c:3024-3031)")
    A("R_key  = R_bias * V_node / (V_bias - V_node)")
    A("```")
    A("`R_bias` 未在源码中出现，**需实测/查原理图**。上表毫伏阈值可直接代入该式。")
    A("")
    A("## 6. 12 bit 通道码 → 毫伏（按键/湿度用）")
    A("")
    A("`mv = code * 1840 / 4096`（`mtk-spmi-pmic-adc.c:370-371`，r_ratio 默认 1/1，`:629-635`）。")
    A("")
    A("| code | mV | 3-key | 4-key |")
    A("|---|---|---|---|")
    for r in doc["accdet_mv_table"]:
        A("| %d | %.2f | %s | %s |" % (r["adc_code"], r["mv"], r["key_3key"], r["key_4key"]))
    A("")
    A("efuse 偏移先减掉再比：`auxadc_offset` 取自 nvmem 字节 `91*2`，`val&0xFF`，>128 减 256，再 `>>1`，")
    A("单位 mV，范围 −64..63（`mt6368-accdet.c:927-931`，减法在 `:904-911`）。")
    A("")
    A("## 7. 前置条件（不出值就是这里没做对）")
    A("")
    A("1. 初始化有**两条**路径，只跑一次的闸门是 `atomic_cmpxchg(&accdet_first,1,0)`"
      "（`mt6368-accdet.c:3142`）：")
    A("   - probe 里 `atomic_set(&accdet_first,1)` 后挂 `ACCDET_INIT_WAIT_TIMER = 10*HZ` 定时器"
      "（`mt6368-accdet.c:206,3495-3496`），超时走 `delay_init_work_callback()`；")
    A("   - 或 audio 侧显式调 `mt6368_accdet_late_init()`（`mt6368-accdet.c:3139-3149`），"
      "先 `del_timer_sync` 再初始化。")
    A("   两条都执行同一组三步：`accdet_init()` → `accdet_init_debounce()` → `accdet_init_once()`"
      "（`mt6368-accdet.c:3144-3146`，`3211-3217`）。**只做其中一步不够。**")
    A("2. `accdet_init()` 里 `ACCDET_SEQ_INIT` 置位后 `usleep_range(2000,3000)` 再清，"
      "再 `usleep_range(1000,1500)`，")
    A("   然后才下发 debounce（`mt6368-accdet.c:3110-3115`）。这是唯一让模拟前端稳定的等待，"
      "跳步会读到上一次的 AB/比较器状态。")
    A("3. `accdet_init_once()` 先打 `RG_ACCDET_RST` 复位脉冲，再配 micbias；"
      "`mic_vol=8` 走 2.8 V 分支")
    A("   （`mt6368-accdet.c:2984-2992`，`3024-3031`）。bias 没起来时读到的是 0 mV，"
      "会被当成 MD_KEY 一直按着。")
    A("4. AUXADC 通道映射走 DTS：`io-channels = <&mt6368_auxadc 9>` + `io-channel-names = \"pmic_accdet\"`")
    A("   （`mt6895.dtsi:2170-2171`）；通道号 9 是 dt-binding 的 `AUXADC_ACCDET`，")
    A("   硬件通道是 5（`include/dt-bindings/iio/mt635x-auxadc.h:19`，`mtk-spmi-pmic-adc.c:98`）。")
    A("5. HP 阻抗路径还要求 `HEADPHONE` 已上电且 mux 切到 `HP_MUX_HP_IMPEDANCE`，")
    A("   否则 `hp_impedance_get()` 直接返回缓存值（`mt6368.c:5222-5229`）。")
    A("   测量期间还要 `set_trim_buf_gain(TRIM_BUF_GAIN_18DB)` + `AUXADC_AVG_64`（`mt6368.c:5073-5077`）。")
    A("6. `mt6368_accdet_read_audio_res()` 在本树**没有调用者**（`EXPORT_SYMBOL`，`mt6368-accdet.c:864`）。")
    A("   谁喂它欧姆值不在 `sound/` 里——未验证。")
    A("")
    A("## 8. 源码中不存在的东西")
    A("")
    for item in doc["not_in_source"]:
        A("- " + item)
    A("")
    A("## 9. 与实测的对照（stock dump，未插耳机）")
    A("")
    A("`accdet-baseline.json`（sid=5，`dump_reg`，2026-11-14，无耳机）里 `0x26b6 = 0xFF`，")
    A("按 `MEM_IN = (0x26b6 >> 6) & 0x3`（`mt6368-accdet.c:2005-2006` 的读法）得 **AB = 3**，")
    A("即 `ACCDET_STATE_AB_11` → 拔出（`mt6368-accdet.c:2045-2056`）。这与「未插」一致，")
    A("说明 `MEM_IN` 字段的读法正确。")
    A("")
    A("同一份 dump 里 `0x2688 = 0x00`，即 `ACCDET_AUXADC_SEL`/`ACCDET_AUXADC_SW` 都是 0——")
    A("**ACCDET→AUXADC 的模拟开关在未插入时是关的**。所以想读通道 5，必须先让状态机进 MIC_BIAS")
    A("（`accdet_queue_work()` 里 `accdet_status == MIC_BIAS` 才取 `cali_voltage`，"
      "`mt6368-accdet.c:2164-2165`）。")
    A("")
    A("本表所有欧姆数字**未经实测校验**：源码推导值，等有耳机实测 ADC 后再回归。")
    A("")
    A("## 10. selftest")
    A("")
    A("```")
    A("$ python3 adc-ohm.py --selftest")
    A(SELFTEST_OUTPUT.rstrip())
    A("```")
    return "\n".join(L) + "\n"


SELFTEST_OUTPUT = ""


# ------------------------------------------------------------------ CLI
def main(argv=None):
    global SELFTEST_OUTPUT
    ap = argparse.ArgumentParser(
        description="MT6368 ACCDET / HP-impedance ADC <-> ohm converter "
                    "(constants cited from vendor source)")
    ap.add_argument("--adc", type=int, help="raw ADC value")
    ap.add_argument("--ohm", type=int, help="resistance in ohm")
    ap.add_argument("--kind", choices=["hp", "accdet"], default="hp",
                    help="hp = 15-bit HPOFS delta (gives ohm, default); "
                         "accdet = 12-bit jack ADC code (gives mV only)")
    ap.add_argument("--phase", choices=["0", "1", "2"], default=None)
    ap.add_argument("--efuse-cal", type=int, default=0,
                    help="HPDET_COMP value, -127..127 (mt6368.c:5260-5262)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--table", action="store_true", help="dump the ohm table")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--emit", action="store_true",
                    help="write ADC-TO-OHM.json and ADC-TO-OHM.md next to this script")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    if args.emit:
        here = os.path.dirname(os.path.abspath(__file__))
        outdir = os.path.join(here, "findings")
        os.makedirs(outdir, exist_ok=True)

        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = selftest()
        SELFTEST_OUTPUT = buf.getvalue()

        doc = build_document()
        jpath = os.path.join(outdir, "ADC-TO-OHM.json")
        mpath = os.path.join(outdir, "ADC-TO-OHM.md")
        with open(jpath, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False)
            f.write("\n")
        with open(mpath, "w", encoding="utf-8") as f:
            f.write(emit_markdown(doc))
        print("selftest rc=%d" % rc)
        print("wrote %s" % jpath)
        print("wrote %s" % mpath)
        return rc

    if args.table:
        rows = build_table()
        if args.json:
            print(json.dumps(rows, indent=2, ensure_ascii=False))
        else:
            print("%-7s %-6s %-5s %-8s %-10s %-12s %-8s %s" %
                  ("ohm", "phase", "dc", "dSum", "d/sample", "range(d/sample)", "rep", "note"))
            for r in rows:
                rng = ("%s..%s" % (r["adc_per_sample_min"], r["adc_per_sample_max"])
                       if r["adc_per_sample_min"] is not None else "-")
                print("%-7d %-6s %-5d %-8s %-10s %-12s %-8d %s" %
                      (r["ohm"], r["phase"], r["dc"],
                       r["adc_sum"] if r["adc_sum"] is not None else "-",
                       r["adc_per_sample"] if r["adc_per_sample"] is not None else "-",
                       rng, r["reported_ohm"], r["note"]))
        return 0

    if args.adc is not None and args.kind == "accdet":
        mv = accdet_code_to_mv(args.adc)
        out = {
            "adc_code": args.adc, "bits": ACCDET_BITS, "vref_mv": VREF_MV,
            "mv": round(mv, 4), "ohm": None,
            "ohm_note": "vendor source has no ACCDET-mV -> ohm function; only mV thresholds",
            "key_3key": key_bucket_3key(mv), "key_4key": key_bucket_4key(mv),
        }
        if args.json:
            print(json.dumps(out, indent=2, ensure_ascii=False))
        else:
            print("ACCDET ch5 code %d (12 bit, VREF %d mV, LSB %.6f mV)"
                  % (args.adc, VREF_MV, VREF_MV / ACCDET_FULL))
            print("  voltage   : %.3f mV" % mv)
            print("  ohm       : n/a  (no such conversion in vendor source)")
            print("  key 3-key : %s" % out["key_3key"])
            print("  key 4-key : %s" % out["key_4key"])
        return 0

    if args.adc is not None:
        phase = args.phase or "0"
        ohm = hp_adc_to_ohm(args.adc, phase, args.efuse_cal)
        out = {
            "kind": "hp", "adc_sum": args.adc, "adc_per_sample": args.adc / DETECT_TIMES,
            "phase": phase, "dc": PHASE_DC[phase], "efuse_cal": args.efuse_cal,
            "ohm": ohm, "read_audio_res": classify_audio_res(ohm),
            "over_15bit": args.adc >= HP_FULL * DETECT_TIMES,
        }
        if args.json:
            print(json.dumps(out, indent=2, ensure_ascii=False))
        else:
            print("HP phase %s (dc=%d), dSum=%d (%.2f/sample) -> %d ohm"
                  % (phase, PHASE_DC[phase], args.adc, args.adc / DETECT_TIMES, ohm))
            print("  read_audio_res(): %d  (%s)"
                  % (out["read_audio_res"],
                     "RET_GT_5K -> LineOut" if ohm >= 5000 else "RET_LT_5K -> normal device"))
            if out["over_15bit"]:
                print("  WARNING: dSum >= 8*2^15, outside the 15-bit HPOFS range")
        return 0

    if args.ohm is not None:
        ohm = args.ohm
        ph = args.phase or phase_for_ohm(ohm)
        sat = saturation_limit_ohm()
        if ohm > sat:
            out = {
                "ohm": ohm, "phase": ph, "adc_sum": None,
                "note": "above saturation limit %d ohm -> driver reports 5000 "
                        "(mt6368.c:5039,5140-5142)" % sat,
                "reported_ohm": 5000, "read_audio_res": classify_audio_res(5000),
            }
        else:
            d = hp_ohm_to_adc(ohm, ph)
            lo, hi = hp_adc_interval_for_ohm(ohm, ph)
            out = {
                "ohm": ohm, "phase": ph, "dc": PHASE_DC[ph],
                "adc_sum": d, "adc_per_sample": round(d / DETECT_TIMES, 3),
                "adc_per_sample_min": round(lo / DETECT_TIMES, 3) if lo is not None else None,
                "adc_per_sample_max": round(hi / DETECT_TIMES, 3) if hi is not None else None,
                "reported_ohm": hp_adc_to_ohm(d, ph),
                "read_audio_res": classify_audio_res(ohm),
            }
        if args.json:
            print(json.dumps(out, indent=2, ensure_ascii=False))
        else:
            print("%d ohm -> phase %s" % (ohm, out["phase"]))
            if out.get("adc_sum") is None:
                print("  " + out["note"])
            else:
                print("  dc step     : %d  (k=%d, %.4f ohm/LSB/sample)"
                      % (PHASE_DC[ph], phase_k(ph), ohm_resolution(ph)))
                print("  adc dSum    : %d  (%.2f per sample; sum of %d)"
                      % (out["adc_sum"], out["adc_per_sample"], DETECT_TIMES))
                print("  d/sample rng: %s..%s"
                      % (out["adc_per_sample_min"], out["adc_per_sample_max"]))
                print("  reports back: %d ohm" % out["reported_ohm"])
            print("  read_audio_res(): %d" % out["read_audio_res"])
        return 0

    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
