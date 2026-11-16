# Vendor callback reference trace

Actual module callbacks executed offline in Unicorn. Core scheduling and hardware are models; all assumptions and limits are in supply-callback-trace.json.

U(reg,mask,value); W(reg,value). Values passed to regmap are retained, so a legacy gain bus value such as 0xf9f is distinct from its byte transfer and defined readback field.

## normal_aif1_hp

Source-mux control precondition: {'reg': 9102, 'mask': 1, 'value': 0, 'note': 'Control put is not executed; selected source is a precondition'}

### up

Widget order: mt6368_vaud18 → CLK_BUF → DL Digital Clock → DL Digital Clock CH_1_2 → DL Power Supply → HP_Supply → AUDGLB → DL_GPIO → HP_PULL_DOWN → CLKSQ Audio → AUDNCP_CK → ZCD13M_CK → AUDIF_CK → AUD_CK → AUDIO_TOP_DAC_CTL → AUDIO_TOP_PWR_CLK → AUDIO_TOP_PDN_RESERVED → AUDIO_TOP_AFE_CTL → SDM_FIFO_CLK → SDM → NCP → AFE_ON → AFE_DL_SRC → ESD_RESIST → HP_DAMP → HP_MUTE → LDO_REMOTE → LDO → NV_REGULATOR → HP_ANA_TRIM → IBIST → AIF_RX → Headphone L → Headphone R → DAC In Mux → HPL Mux → HPR Mux → DACL → DACR

| # | Widget / origin | Operation |
|---|---|---|
| 1 | mt6368_vaud18 / module_callback | external regulator event 1 |
| 2 | CLK_BUF / module_callback | external mt6685_set_dcxo(True) |
| 3 | AUDGLB / modeled_dapm_coalesced | U(0x25a2,0x10,0x0) |
| 4 | DL_GPIO / module_callback | R(0x00c7) = model 0x0 |
| 5 | DL_GPIO / module_callback | W(0x00c9,0x38) |
| 6 | DL_GPIO / module_callback | W(0x00c8,0x8) |
| 7 | DL_GPIO / module_callback | W(0x00cc,0x3f) |
| 8 | DL_GPIO / module_callback | W(0x00cb,0x9) |
| 9 | DL_GPIO / module_callback | W(0x00cf,0x3f) |
| 10 | DL_GPIO / module_callback | W(0x00ce,0x9) |
| 11 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x0) |
| 12 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 13 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x10) |
| 14 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 15 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x20) |
| 16 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 17 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x30) |
| 18 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 19 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x40) |
| 20 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 21 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x50) |
| 22 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 23 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x60) |
| 24 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 25 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x70) |
| 26 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 27 | CLKSQ Audio / modeled_dapm_coalesced | U(0x2537,0x1,0x1) |
| 28 | AUDNCP_CK + ZCD13M_CK + AUDIF_CK / modeled_dapm_coalesced | U(0x230c,0x64,0x0) |
| 29 | AUD_CK / modeled_dapm_coalesced | U(0x230c,0x2,0x0) |
| 30 | AUD_CK / module_callback | delay 250..270 us |
| 31 | AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_RESERVED / modeled_dapm_coalesced | U(0x238f,0x45,0x0) |
| 32 | AUDIO_TOP_AFE_CTL / modeled_dapm_coalesced | U(0x238f,0x80,0x0) |
| 33 | AUDIO_TOP_AFE_CTL / module_callback | delay 250..270 us |
| 34 | SDM_FIFO_CLK / modeled_dapm_coalesced | U(0x2396,0x2,0x2) |
| 35 | SDM / module_callback | U(0x2396,0xfd,0x6) |
| 36 | SDM / module_callback | W(0x2393,0xcb) |
| 37 | SDM / module_callback | W(0x2392,0xa1) |
| 38 | SDM / module_callback | U(0x2396,0xfd,0x3) |
| 39 | SDM / module_callback | U(0x2396,0xfd,0xb) |
| 40 | NCP / module_callback | W(0x2489,0xc8) |
| 41 | NCP / module_callback | W(0x2488,0x0) |
| 42 | NCP / modeled_dapm_coalesced | U(0x2488,0x1,0x1) |
| 43 | AFE_ON / modeled_dapm_coalesced | U(0x2388,0x1,0x1) |
| 44 | AFE_DL_SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x1) |
| 45 | ESD_RESIST / module_callback | U(0x258d,0x1,0x1) |
| 46 | ESD_RESIST / module_callback | delay 250..270 us |
| 47 | HP_MUTE / module_callback | W(0x260b,0xf1e) |
| 48 | HP_MUTE / module_callback | W(0x260c,0xf1e) |
| 49 | LDO_REMOTE / modeled_dapm_coalesced | U(0x25a3,0x4,0x4) |
| 50 | LDO / modeled_dapm_coalesced | U(0x25a3,0x1,0x1) |
| 51 | NV_REGULATOR / modeled_dapm_coalesced | U(0x25a3,0x10,0x10) |
| 52 | NV_REGULATOR / module_callback | delay 100..120 us |
| 53 | HP_ANA_TRIM / modeled_dapm_coalesced | U(0x258d,0x80,0x80) |
| 54 | HP_ANA_TRIM / module_callback | U(0x258e,0xf8,0x18) |
| 55 | HP_ANA_TRIM / module_callback | U(0x258e,0x7,0x1) |
| 56 | HP_ANA_TRIM / module_callback | U(0x258f,0xf8,0x38) |
| 57 | HP_ANA_TRIM / module_callback | U(0x258f,0x7,0x2) |
| 58 | IBIST / modeled_dapm_coalesced | U(0x25a0,0x10,0x0) |
| 59 | HPL Mux / module_callback | U(0x259f,0x7,0x1) |
| 60 | HPL Mux / module_callback | U(0x25a1,0xc0,0x0) |
| 61 | HPL Mux / module_callback | U(0x25a1,0x3,0x0) |
| 62 | HPL Mux / module_callback | W(0x259c,0x87) |
| 63 | HPL Mux / module_callback | W(0x2591,0x0) |
| 64 | HPL Mux / module_callback | W(0x258c,0x33) |
| 65 | HPL Mux / module_callback | W(0x258d,0xf1) |
| 66 | HPL Mux / module_callback | W(0x258b,0x0) |
| 67 | HPL Mux / module_callback | W(0x258a,0xc) |
| 68 | HPL Mux / module_callback | W(0x258a,0x3c) |
| 69 | HPL Mux / module_callback | W(0x259a,0x0) |
| 70 | HPL Mux / module_callback | W(0x259b,0xc) |
| 71 | HPL Mux / module_callback | W(0x2589,0x30) |
| 72 | HPL Mux / module_callback | W(0x2588,0xc0) |
| 73 | HPL Mux / module_callback | W(0x2588,0xf0) |
| 74 | HPL Mux / module_callback | W(0x258a,0xfc) |
| 75 | HPL Mux / module_callback | W(0x259b,0xe) |
| 76 | HPL Mux / module_callback | W(0x259b,0x2) |
| 77 | HPL Mux / module_callback | W(0x258a,0xff) |
| 78 | HPL Mux / module_callback | U(0x258b,0x7,0x0) |
| 79 | HPL Mux / module_callback | U(0x258b,0x70,0x0) |
| 80 | HPL Mux / module_callback | delay 600..650 us |
| 81 | HPL Mux / module_callback | U(0x258b,0x7,0x1) |
| 82 | HPL Mux / module_callback | U(0x258b,0x70,0x10) |
| 83 | HPL Mux / module_callback | delay 600..650 us |
| 84 | HPL Mux / module_callback | U(0x258b,0x7,0x2) |
| 85 | HPL Mux / module_callback | U(0x258b,0x70,0x20) |
| 86 | HPL Mux / module_callback | delay 600..650 us |
| 87 | HPL Mux / module_callback | U(0x258b,0x7,0x3) |
| 88 | HPL Mux / module_callback | U(0x258b,0x70,0x30) |
| 89 | HPL Mux / module_callback | delay 600..650 us |
| 90 | HPL Mux / module_callback | U(0x258b,0x7,0x4) |
| 91 | HPL Mux / module_callback | U(0x258b,0x70,0x40) |
| 92 | HPL Mux / module_callback | delay 600..650 us |
| 93 | HPL Mux / module_callback | U(0x258b,0x7,0x5) |
| 94 | HPL Mux / module_callback | U(0x258b,0x70,0x50) |
| 95 | HPL Mux / module_callback | delay 600..650 us |
| 96 | HPL Mux / module_callback | U(0x258b,0x7,0x6) |
| 97 | HPL Mux / module_callback | U(0x258b,0x70,0x60) |
| 98 | HPL Mux / module_callback | delay 600..650 us |
| 99 | HPL Mux / module_callback | U(0x258b,0x7,0x7) |
| 100 | HPL Mux / module_callback | U(0x258b,0x70,0x70) |
| 101 | HPL Mux / module_callback | delay 600..650 us |
| 102 | HPL Mux / module_callback | U(0x259b,0xf0,0x0) |
| 103 | HPL Mux / module_callback | delay 600..650 us |
| 104 | HPL Mux / module_callback | U(0x259b,0xf0,0x10) |
| 105 | HPL Mux / module_callback | delay 600..650 us |
| 106 | HPL Mux / module_callback | U(0x259b,0xf0,0x20) |
| 107 | HPL Mux / module_callback | delay 600..650 us |
| 108 | HPL Mux / module_callback | U(0x259b,0xf0,0x30) |
| 109 | HPL Mux / module_callback | delay 600..650 us |
| 110 | HPL Mux / module_callback | U(0x259b,0xf0,0x40) |
| 111 | HPL Mux / module_callback | delay 600..650 us |
| 112 | HPL Mux / module_callback | U(0x259b,0xf0,0x50) |
| 113 | HPL Mux / module_callback | delay 600..650 us |
| 114 | HPL Mux / module_callback | U(0x259b,0xf0,0x60) |
| 115 | HPL Mux / module_callback | delay 600..650 us |
| 116 | HPL Mux / module_callback | U(0x259b,0xf0,0x70) |
| 117 | HPL Mux / module_callback | delay 600..650 us |
| 118 | HPL Mux / module_callback | U(0x259b,0xf0,0x80) |
| 119 | HPL Mux / module_callback | delay 600..650 us |
| 120 | HPL Mux / module_callback | U(0x259b,0xf0,0x90) |
| 121 | HPL Mux / module_callback | delay 600..650 us |
| 122 | HPL Mux / module_callback | U(0x259b,0xf0,0xa0) |
| 123 | HPL Mux / module_callback | delay 600..650 us |
| 124 | HPL Mux / module_callback | U(0x259b,0xf0,0xb0) |
| 125 | HPL Mux / module_callback | delay 600..650 us |
| 126 | HPL Mux / module_callback | U(0x259b,0xf0,0xc0) |
| 127 | HPL Mux / module_callback | delay 600..650 us |
| 128 | HPL Mux / module_callback | U(0x259b,0xf0,0xd0) |
| 129 | HPL Mux / module_callback | delay 600..650 us |
| 130 | HPL Mux / module_callback | U(0x259b,0xf0,0xe0) |
| 131 | HPL Mux / module_callback | delay 600..650 us |
| 132 | HPL Mux / module_callback | U(0x259b,0xf0,0xf0) |
| 133 | HPL Mux / module_callback | delay 600..650 us |
| 134 | HPL Mux / module_callback | W(0x258a,0xcf) |
| 135 | HPL Mux / module_callback | W(0x258b,0x77) |
| 136 | HPL Mux / module_callback | U(0x260b,0xf9f,0x1f) |
| 137 | HPL Mux / module_callback | U(0x260c,0xf9f,0x1f) |
| 138 | HPL Mux / module_callback | delay 600..650 us |
| 139 | HPL Mux / module_callback | W(0x258a,0xc3) |
| 140 | HPL Mux / module_callback | W(0x258a,0x3) |
| 141 | HPL Mux / module_callback | delay 100..120 us |
| 142 | HPL Mux / module_callback | U(0x25a2,0x1,0x1) |
| 143 | HPL Mux / module_callback | W(0x2589,0x30) |
| 144 | HPL Mux / module_callback | W(0x2588,0xff) |
| 145 | HPL Mux / module_callback | W(0x259a,0x0) |
| 146 | HPL Mux / module_callback | W(0x259b,0xf2) |
| 147 | HPL Mux / module_callback | delay 100..120 us |
| 148 | HPL Mux / module_callback | U(0x2589,0x3,0x2) |
| 149 | HPL Mux / module_callback | U(0x2589,0xc,0x8) |
| 150 | HPL Mux / module_callback | U(0x258d,0x70,0x70) |
| 151 | HPL Mux / module_callback | delay 100..150 us |
| 152 | HPL Mux / module_callback | U(0x258d,0x70,0x60) |
| 153 | HPL Mux / module_callback | delay 100..150 us |
| 154 | HPL Mux / module_callback | U(0x258d,0x70,0x50) |
| 155 | HPL Mux / module_callback | delay 100..150 us |
| 156 | HPL Mux / module_callback | U(0x258d,0x70,0x40) |
| 157 | HPL Mux / module_callback | delay 100..150 us |
| 158 | HPL Mux / module_callback | U(0x258d,0x70,0x30) |
| 159 | HPL Mux / module_callback | delay 100..150 us |
| 160 | HPL Mux / module_callback | U(0x258d,0x70,0x20) |
| 161 | HPL Mux / module_callback | delay 100..150 us |
| 162 | HPL Mux / module_callback | U(0x258d,0x70,0x10) |
| 163 | HPL Mux / module_callback | delay 100..150 us |
| 164 | HPL Mux / module_callback | U(0x258d,0x70,0x0) |
| 165 | HPL Mux / module_callback | delay 100..150 us |

### down

Widget order: DACL → DACR → Headphone L → Headphone R → DAC In Mux → HPL Mux → HPR Mux → AIF_RX → IBIST → HP_ANA_TRIM → NV_REGULATOR → LDO → LDO_REMOTE → HP_MUTE → HP_DAMP → ESD_RESIST → AFE_DL_SRC → AFE_ON → NCP → SDM → SDM_FIFO_CLK → AUDIO_TOP_AFE_CTL → AUDIO_TOP_DAC_CTL → AUDIO_TOP_PWR_CLK → AUDIO_TOP_PDN_RESERVED → AUD_CK → AUDNCP_CK → ZCD13M_CK → AUDIF_CK → CLKSQ Audio → HP_PULL_DOWN → DL_GPIO → AUDGLB → CLK_BUF → DL Digital Clock → DL Digital Clock CH_1_2 → DL Power Supply → HP_Supply → mt6368_vaud18

| # | Widget / origin | Operation |
|---|---|---|
| 1 | HPR Mux / module_callback | U(0x258d,0x70,0x0) |
| 2 | HPR Mux / module_callback | delay 100..150 us |
| 3 | HPR Mux / module_callback | U(0x258d,0x70,0x10) |
| 4 | HPR Mux / module_callback | delay 100..150 us |
| 5 | HPR Mux / module_callback | U(0x258d,0x70,0x20) |
| 6 | HPR Mux / module_callback | delay 100..150 us |
| 7 | HPR Mux / module_callback | U(0x258d,0x70,0x30) |
| 8 | HPR Mux / module_callback | delay 100..150 us |
| 9 | HPR Mux / module_callback | U(0x258d,0x70,0x40) |
| 10 | HPR Mux / module_callback | delay 100..150 us |
| 11 | HPR Mux / module_callback | U(0x258d,0x70,0x50) |
| 12 | HPR Mux / module_callback | delay 100..150 us |
| 13 | HPR Mux / module_callback | U(0x258d,0x70,0x60) |
| 14 | HPR Mux / module_callback | delay 100..150 us |
| 15 | HPR Mux / module_callback | U(0x258d,0x70,0x70) |
| 16 | HPR Mux / module_callback | delay 100..150 us |
| 17 | HPR Mux / module_callback | U(0x2589,0xf,0x0) |
| 18 | HPR Mux / module_callback | U(0x259a,0x1,0x0) |
| 19 | HPR Mux / module_callback | U(0x2588,0xf,0x0) |
| 20 | HPR Mux / module_callback | U(0x25a2,0x1,0x0) |
| 21 | HPR Mux / module_callback | W(0x258b,0x77) |
| 22 | HPR Mux / module_callback | W(0x258a,0xc3) |
| 23 | HPR Mux / module_callback | W(0x258a,0xcf) |
| 24 | HPR Mux / module_callback | U(0x260b,0xf9f,0x1e) |
| 25 | HPR Mux / module_callback | U(0x260c,0xf9f,0x1e) |
| 26 | HPR Mux / module_callback | delay 600..650 us |
| 27 | HPR Mux / module_callback | W(0x258a,0xff) |
| 28 | HPR Mux / module_callback | U(0x259b,0xf0,0xf0) |
| 29 | HPR Mux / module_callback | delay 600..650 us |
| 30 | HPR Mux / module_callback | U(0x259b,0xf0,0xe0) |
| 31 | HPR Mux / module_callback | delay 600..650 us |
| 32 | HPR Mux / module_callback | U(0x259b,0xf0,0xd0) |
| 33 | HPR Mux / module_callback | delay 600..650 us |
| 34 | HPR Mux / module_callback | U(0x259b,0xf0,0xc0) |
| 35 | HPR Mux / module_callback | delay 600..650 us |
| 36 | HPR Mux / module_callback | U(0x259b,0xf0,0xb0) |
| 37 | HPR Mux / module_callback | delay 600..650 us |
| 38 | HPR Mux / module_callback | U(0x259b,0xf0,0xa0) |
| 39 | HPR Mux / module_callback | delay 600..650 us |
| 40 | HPR Mux / module_callback | U(0x259b,0xf0,0x90) |
| 41 | HPR Mux / module_callback | delay 600..650 us |
| 42 | HPR Mux / module_callback | U(0x259b,0xf0,0x80) |
| 43 | HPR Mux / module_callback | delay 600..650 us |
| 44 | HPR Mux / module_callback | U(0x259b,0xf0,0x70) |
| 45 | HPR Mux / module_callback | delay 600..650 us |
| 46 | HPR Mux / module_callback | U(0x259b,0xf0,0x60) |
| 47 | HPR Mux / module_callback | delay 600..650 us |
| 48 | HPR Mux / module_callback | U(0x259b,0xf0,0x50) |
| 49 | HPR Mux / module_callback | delay 600..650 us |
| 50 | HPR Mux / module_callback | U(0x259b,0xf0,0x40) |
| 51 | HPR Mux / module_callback | delay 600..650 us |
| 52 | HPR Mux / module_callback | U(0x259b,0xf0,0x30) |
| 53 | HPR Mux / module_callback | delay 600..650 us |
| 54 | HPR Mux / module_callback | U(0x259b,0xf0,0x20) |
| 55 | HPR Mux / module_callback | delay 600..650 us |
| 56 | HPR Mux / module_callback | U(0x259b,0xf0,0x10) |
| 57 | HPR Mux / module_callback | delay 600..650 us |
| 58 | HPR Mux / module_callback | U(0x259b,0xf0,0x0) |
| 59 | HPR Mux / module_callback | delay 600..650 us |
| 60 | HPR Mux / module_callback | U(0x258b,0x7,0x7) |
| 61 | HPR Mux / module_callback | U(0x258b,0x70,0x70) |
| 62 | HPR Mux / module_callback | delay 600..650 us |
| 63 | HPR Mux / module_callback | U(0x258b,0x7,0x6) |
| 64 | HPR Mux / module_callback | U(0x258b,0x70,0x60) |
| 65 | HPR Mux / module_callback | delay 600..650 us |
| 66 | HPR Mux / module_callback | U(0x258b,0x7,0x5) |
| 67 | HPR Mux / module_callback | U(0x258b,0x70,0x50) |
| 68 | HPR Mux / module_callback | delay 600..650 us |
| 69 | HPR Mux / module_callback | U(0x258b,0x7,0x4) |
| 70 | HPR Mux / module_callback | U(0x258b,0x70,0x40) |
| 71 | HPR Mux / module_callback | delay 600..650 us |
| 72 | HPR Mux / module_callback | U(0x258b,0x7,0x3) |
| 73 | HPR Mux / module_callback | U(0x258b,0x70,0x30) |
| 74 | HPR Mux / module_callback | delay 600..650 us |
| 75 | HPR Mux / module_callback | U(0x258b,0x7,0x2) |
| 76 | HPR Mux / module_callback | U(0x258b,0x70,0x20) |
| 77 | HPR Mux / module_callback | delay 600..650 us |
| 78 | HPR Mux / module_callback | U(0x258b,0x7,0x1) |
| 79 | HPR Mux / module_callback | U(0x258b,0x70,0x10) |
| 80 | HPR Mux / module_callback | delay 600..650 us |
| 81 | HPR Mux / module_callback | U(0x258b,0x7,0x0) |
| 82 | HPR Mux / module_callback | U(0x258b,0x70,0x0) |
| 83 | HPR Mux / module_callback | delay 600..650 us |
| 84 | HPR Mux / module_callback | U(0x258a,0x3,0x0) |
| 85 | HPR Mux / module_callback | W(0x259a,0x1) |
| 86 | HPR Mux / module_callback | W(0x259b,0xe) |
| 87 | HPR Mux / module_callback | W(0x259b,0xc) |
| 88 | HPR Mux / module_callback | U(0x258a,0xc0,0x0) |
| 89 | HPR Mux / module_callback | U(0x2588,0x30,0x0) |
| 90 | HPR Mux / module_callback | U(0x2588,0xc0,0x0) |
| 91 | HPR Mux / module_callback | W(0x259a,0x1) |
| 92 | HPR Mux / module_callback | W(0x259b,0x2) |
| 93 | HPR Mux / module_callback | U(0x258a,0x30,0x0) |
| 94 | HPR Mux / module_callback | U(0x258a,0xc,0x0) |
| 95 | IBIST / modeled_dapm_coalesced | U(0x25a0,0x10,0x10) |
| 96 | HP_ANA_TRIM / modeled_dapm_coalesced | U(0x258d,0x80,0x0) |
| 97 | HP_ANA_TRIM / module_callback | W(0x258e,0x0) |
| 98 | HP_ANA_TRIM / module_callback | W(0x258f,0x0) |
| 99 | NV_REGULATOR / modeled_dapm_coalesced | U(0x25a3,0x10,0x0) |
| 100 | LDO / modeled_dapm_coalesced | U(0x25a3,0x1,0x0) |
| 101 | LDO_REMOTE / modeled_dapm_coalesced | U(0x25a3,0x4,0x0) |
| 102 | HP_MUTE / module_callback | W(0x260b,0xf9f) |
| 103 | HP_MUTE / module_callback | W(0x260c,0xf9f) |
| 104 | HP_DAMP / module_callback | W(0x259c,0x0) |
| 105 | HP_DAMP / module_callback | W(0x259d,0x0) |
| 106 | ESD_RESIST / module_callback | U(0x258d,0x1,0x0) |
| 107 | AFE_DL_SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x0) |
| 108 | AFE_ON / modeled_dapm_coalesced | U(0x2388,0x1,0x0) |
| 109 | NCP / modeled_dapm_coalesced | U(0x2488,0x1,0x0) |
| 110 | SDM / module_callback | U(0x2396,0xfd,0x0) |
| 111 | SDM / module_callback | W(0x2393,0xcb) |
| 112 | SDM / module_callback | W(0x2392,0xa0) |
| 113 | SDM_FIFO_CLK / modeled_dapm_coalesced | U(0x2396,0x2,0x0) |
| 114 | AUDIO_TOP_AFE_CTL / module_callback | delay 250..270 us |
| 115 | AUDIO_TOP_AFE_CTL / modeled_dapm_coalesced | U(0x238f,0x80,0x80) |
| 116 | AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_RESERVED / modeled_dapm_coalesced | U(0x238f,0x45,0x45) |
| 117 | AUD_CK / module_callback | delay 250..270 us |
| 118 | AUD_CK / modeled_dapm_coalesced | U(0x230c,0x2,0x2) |
| 119 | AUDNCP_CK + ZCD13M_CK + AUDIF_CK / modeled_dapm_coalesced | U(0x230c,0x64,0x64) |
| 120 | CLKSQ Audio / modeled_dapm_coalesced | U(0x2537,0x1,0x0) |
| 121 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x70) |
| 122 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 123 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x60) |
| 124 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 125 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x50) |
| 126 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 127 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x40) |
| 128 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 129 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x30) |
| 130 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 131 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x20) |
| 132 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 133 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x10) |
| 134 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 135 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x0) |
| 136 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 137 | DL_GPIO / module_callback | W(0x00cc,0x3f) |
| 138 | DL_GPIO / module_callback | W(0x00cf,0x7) |
| 139 | DL_GPIO / module_callback | U(0x008b,0xe,0x0) |
| 140 | AUDGLB / modeled_dapm_coalesced | U(0x25a2,0x10,0x10) |
| 141 | CLK_BUF / module_callback | external mt6685_set_dcxo(False) |
| 142 | mt6368_vaud18 / module_callback | external regulator event 8 |

## internal_sgen_hp

Source-mux control precondition: {'reg': 9102, 'mask': 1, 'value': 1, 'note': 'Control put is not executed; selected source is a precondition'}

### up

Widget order: mt6368_vaud18 → CLK_BUF → DL Digital Clock → DL Digital Clock CH_1_2 → DL Digital Clock CH_3 → DL Power Supply → HP_Supply → SGEN DL SRC → SGEN DL Enable → SGEN MUTE → AUDGLB → HP_PULL_DOWN → CLKSQ Audio → AUDNCP_CK → ZCD13M_CK → AUDIF_CK → AUD_CK → AUDIO_TOP_DAC_CTL → AUDIO_TOP_PWR_CLK → AUDIO_TOP_PDN_AFE_TESTMODEL → AUDIO_TOP_PDN_RESERVED → AUDIO_TOP_AFE_CTL → SDM_FIFO_CLK → SDM → SDM_3RD → NCP → AFE_ON → AFE_DL_SRC → ESD_RESIST → HP_DAMP → HP_MUTE → LDO_REMOTE → LDO → NV_REGULATOR → HP_ANA_TRIM → IBIST → Headphone L → Headphone R → SGEN DL → DAC In Mux → HPL Mux → HPR Mux → DACL → DACR

| # | Widget / origin | Operation |
|---|---|---|
| 1 | mt6368_vaud18 / module_callback | external regulator event 1 |
| 2 | CLK_BUF / module_callback | external mt6685_set_dcxo(True) |
| 3 | SGEN DL SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x1) |
| 4 | SGEN MUTE / module_callback | W(0x2396,0x6) |
| 5 | SGEN MUTE / module_callback | W(0x2393,0xcb) |
| 6 | SGEN MUTE / module_callback | W(0x2392,0xa1) |
| 7 | SGEN MUTE / module_callback | W(0x2396,0x3) |
| 8 | SGEN MUTE / module_callback | W(0x2396,0xb) |
| 9 | SGEN MUTE / module_callback | U(0x23c8,0xff,0x0) |
| 10 | SGEN MUTE / module_callback | U(0x23c9,0xff,0x1) |
| 11 | SGEN MUTE / module_callback | U(0x23ca,0xff,0x0) |
| 12 | SGEN DL Enable + SGEN MUTE / modeled_dapm_coalesced | U(0x23c8,0xc0,0x80) |
| 13 | AUDGLB / modeled_dapm_coalesced | U(0x25a2,0x10,0x0) |
| 14 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x0) |
| 15 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 16 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x10) |
| 17 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 18 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x20) |
| 19 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 20 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x30) |
| 21 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 22 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x40) |
| 23 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 24 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x50) |
| 25 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 26 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x60) |
| 27 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 28 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x70) |
| 29 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 30 | CLKSQ Audio / modeled_dapm_coalesced | U(0x2537,0x1,0x1) |
| 31 | AUDNCP_CK + ZCD13M_CK + AUDIF_CK / modeled_dapm_coalesced | U(0x230c,0x64,0x0) |
| 32 | AUD_CK / modeled_dapm_coalesced | U(0x230c,0x2,0x0) |
| 33 | AUD_CK / module_callback | delay 250..270 us |
| 34 | AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_AFE_TESTMODEL + AUDIO_TOP_PDN_RESERVED / modeled_dapm_coalesced | U(0x238f,0x47,0x0) |
| 35 | AUDIO_TOP_AFE_CTL / modeled_dapm_coalesced | U(0x238f,0x80,0x0) |
| 36 | AUDIO_TOP_AFE_CTL / module_callback | delay 250..270 us |
| 37 | SDM_FIFO_CLK / modeled_dapm_coalesced | U(0x2396,0x2,0x2) |
| 38 | SDM / module_callback | U(0x2396,0xfd,0x6) |
| 39 | SDM / module_callback | W(0x2393,0xcb) |
| 40 | SDM / module_callback | W(0x2392,0xa1) |
| 41 | SDM / module_callback | U(0x2396,0xfd,0x3) |
| 42 | SDM / module_callback | U(0x2396,0xfd,0xb) |
| 43 | SDM_3RD / module_callback | W(0x23a6,0x6) |
| 44 | SDM_3RD / module_callback | W(0x23a3,0xcb) |
| 45 | SDM_3RD / module_callback | W(0x23a2,0xa1) |
| 46 | SDM_3RD / module_callback | W(0x23a6,0x3) |
| 47 | SDM_3RD / module_callback | W(0x23a6,0xb) |
| 48 | NCP / module_callback | W(0x2489,0xc8) |
| 49 | NCP / module_callback | W(0x2488,0x0) |
| 50 | NCP / modeled_dapm_coalesced | U(0x2488,0x1,0x1) |
| 51 | AFE_ON / modeled_dapm_coalesced | U(0x2388,0x1,0x1) |
| 52 | AFE_DL_SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x1) |
| 53 | ESD_RESIST / module_callback | U(0x258d,0x1,0x1) |
| 54 | ESD_RESIST / module_callback | delay 250..270 us |
| 55 | HP_MUTE / module_callback | W(0x260b,0xf1e) |
| 56 | HP_MUTE / module_callback | W(0x260c,0xf1e) |
| 57 | LDO_REMOTE / modeled_dapm_coalesced | U(0x25a3,0x4,0x4) |
| 58 | LDO / modeled_dapm_coalesced | U(0x25a3,0x1,0x1) |
| 59 | NV_REGULATOR / modeled_dapm_coalesced | U(0x25a3,0x10,0x10) |
| 60 | NV_REGULATOR / module_callback | delay 100..120 us |
| 61 | HP_ANA_TRIM / modeled_dapm_coalesced | U(0x258d,0x80,0x80) |
| 62 | HP_ANA_TRIM / module_callback | U(0x258e,0xf8,0x18) |
| 63 | HP_ANA_TRIM / module_callback | U(0x258e,0x7,0x1) |
| 64 | HP_ANA_TRIM / module_callback | U(0x258f,0xf8,0x38) |
| 65 | HP_ANA_TRIM / module_callback | U(0x258f,0x7,0x2) |
| 66 | IBIST / modeled_dapm_coalesced | U(0x25a0,0x10,0x0) |
| 67 | HPL Mux / module_callback | U(0x259f,0x7,0x1) |
| 68 | HPL Mux / module_callback | U(0x25a1,0xc0,0x0) |
| 69 | HPL Mux / module_callback | U(0x25a1,0x3,0x0) |
| 70 | HPL Mux / module_callback | W(0x259c,0x87) |
| 71 | HPL Mux / module_callback | W(0x2591,0x0) |
| 72 | HPL Mux / module_callback | W(0x258c,0x33) |
| 73 | HPL Mux / module_callback | W(0x258d,0xf1) |
| 74 | HPL Mux / module_callback | W(0x258b,0x0) |
| 75 | HPL Mux / module_callback | W(0x258a,0xc) |
| 76 | HPL Mux / module_callback | W(0x258a,0x3c) |
| 77 | HPL Mux / module_callback | W(0x259a,0x0) |
| 78 | HPL Mux / module_callback | W(0x259b,0xc) |
| 79 | HPL Mux / module_callback | W(0x2589,0x30) |
| 80 | HPL Mux / module_callback | W(0x2588,0xc0) |
| 81 | HPL Mux / module_callback | W(0x2588,0xf0) |
| 82 | HPL Mux / module_callback | W(0x258a,0xfc) |
| 83 | HPL Mux / module_callback | W(0x259b,0xe) |
| 84 | HPL Mux / module_callback | W(0x259b,0x2) |
| 85 | HPL Mux / module_callback | W(0x258a,0xff) |
| 86 | HPL Mux / module_callback | U(0x258b,0x7,0x0) |
| 87 | HPL Mux / module_callback | U(0x258b,0x70,0x0) |
| 88 | HPL Mux / module_callback | delay 600..650 us |
| 89 | HPL Mux / module_callback | U(0x258b,0x7,0x1) |
| 90 | HPL Mux / module_callback | U(0x258b,0x70,0x10) |
| 91 | HPL Mux / module_callback | delay 600..650 us |
| 92 | HPL Mux / module_callback | U(0x258b,0x7,0x2) |
| 93 | HPL Mux / module_callback | U(0x258b,0x70,0x20) |
| 94 | HPL Mux / module_callback | delay 600..650 us |
| 95 | HPL Mux / module_callback | U(0x258b,0x7,0x3) |
| 96 | HPL Mux / module_callback | U(0x258b,0x70,0x30) |
| 97 | HPL Mux / module_callback | delay 600..650 us |
| 98 | HPL Mux / module_callback | U(0x258b,0x7,0x4) |
| 99 | HPL Mux / module_callback | U(0x258b,0x70,0x40) |
| 100 | HPL Mux / module_callback | delay 600..650 us |
| 101 | HPL Mux / module_callback | U(0x258b,0x7,0x5) |
| 102 | HPL Mux / module_callback | U(0x258b,0x70,0x50) |
| 103 | HPL Mux / module_callback | delay 600..650 us |
| 104 | HPL Mux / module_callback | U(0x258b,0x7,0x6) |
| 105 | HPL Mux / module_callback | U(0x258b,0x70,0x60) |
| 106 | HPL Mux / module_callback | delay 600..650 us |
| 107 | HPL Mux / module_callback | U(0x258b,0x7,0x7) |
| 108 | HPL Mux / module_callback | U(0x258b,0x70,0x70) |
| 109 | HPL Mux / module_callback | delay 600..650 us |
| 110 | HPL Mux / module_callback | U(0x259b,0xf0,0x0) |
| 111 | HPL Mux / module_callback | delay 600..650 us |
| 112 | HPL Mux / module_callback | U(0x259b,0xf0,0x10) |
| 113 | HPL Mux / module_callback | delay 600..650 us |
| 114 | HPL Mux / module_callback | U(0x259b,0xf0,0x20) |
| 115 | HPL Mux / module_callback | delay 600..650 us |
| 116 | HPL Mux / module_callback | U(0x259b,0xf0,0x30) |
| 117 | HPL Mux / module_callback | delay 600..650 us |
| 118 | HPL Mux / module_callback | U(0x259b,0xf0,0x40) |
| 119 | HPL Mux / module_callback | delay 600..650 us |
| 120 | HPL Mux / module_callback | U(0x259b,0xf0,0x50) |
| 121 | HPL Mux / module_callback | delay 600..650 us |
| 122 | HPL Mux / module_callback | U(0x259b,0xf0,0x60) |
| 123 | HPL Mux / module_callback | delay 600..650 us |
| 124 | HPL Mux / module_callback | U(0x259b,0xf0,0x70) |
| 125 | HPL Mux / module_callback | delay 600..650 us |
| 126 | HPL Mux / module_callback | U(0x259b,0xf0,0x80) |
| 127 | HPL Mux / module_callback | delay 600..650 us |
| 128 | HPL Mux / module_callback | U(0x259b,0xf0,0x90) |
| 129 | HPL Mux / module_callback | delay 600..650 us |
| 130 | HPL Mux / module_callback | U(0x259b,0xf0,0xa0) |
| 131 | HPL Mux / module_callback | delay 600..650 us |
| 132 | HPL Mux / module_callback | U(0x259b,0xf0,0xb0) |
| 133 | HPL Mux / module_callback | delay 600..650 us |
| 134 | HPL Mux / module_callback | U(0x259b,0xf0,0xc0) |
| 135 | HPL Mux / module_callback | delay 600..650 us |
| 136 | HPL Mux / module_callback | U(0x259b,0xf0,0xd0) |
| 137 | HPL Mux / module_callback | delay 600..650 us |
| 138 | HPL Mux / module_callback | U(0x259b,0xf0,0xe0) |
| 139 | HPL Mux / module_callback | delay 600..650 us |
| 140 | HPL Mux / module_callback | U(0x259b,0xf0,0xf0) |
| 141 | HPL Mux / module_callback | delay 600..650 us |
| 142 | HPL Mux / module_callback | W(0x258a,0xcf) |
| 143 | HPL Mux / module_callback | W(0x258b,0x77) |
| 144 | HPL Mux / module_callback | U(0x260b,0xf9f,0x1f) |
| 145 | HPL Mux / module_callback | U(0x260c,0xf9f,0x1f) |
| 146 | HPL Mux / module_callback | delay 600..650 us |
| 147 | HPL Mux / module_callback | W(0x258a,0xc3) |
| 148 | HPL Mux / module_callback | W(0x258a,0x3) |
| 149 | HPL Mux / module_callback | delay 100..120 us |
| 150 | HPL Mux / module_callback | U(0x25a2,0x1,0x1) |
| 151 | HPL Mux / module_callback | W(0x2589,0x30) |
| 152 | HPL Mux / module_callback | W(0x2588,0xff) |
| 153 | HPL Mux / module_callback | W(0x259a,0x0) |
| 154 | HPL Mux / module_callback | W(0x259b,0xf2) |
| 155 | HPL Mux / module_callback | delay 100..120 us |
| 156 | HPL Mux / module_callback | U(0x2589,0x3,0x2) |
| 157 | HPL Mux / module_callback | U(0x2589,0xc,0x8) |
| 158 | HPL Mux / module_callback | U(0x258d,0x70,0x70) |
| 159 | HPL Mux / module_callback | delay 100..150 us |
| 160 | HPL Mux / module_callback | U(0x258d,0x70,0x60) |
| 161 | HPL Mux / module_callback | delay 100..150 us |
| 162 | HPL Mux / module_callback | U(0x258d,0x70,0x50) |
| 163 | HPL Mux / module_callback | delay 100..150 us |
| 164 | HPL Mux / module_callback | U(0x258d,0x70,0x40) |
| 165 | HPL Mux / module_callback | delay 100..150 us |
| 166 | HPL Mux / module_callback | U(0x258d,0x70,0x30) |
| 167 | HPL Mux / module_callback | delay 100..150 us |
| 168 | HPL Mux / module_callback | U(0x258d,0x70,0x20) |
| 169 | HPL Mux / module_callback | delay 100..150 us |
| 170 | HPL Mux / module_callback | U(0x258d,0x70,0x10) |
| 171 | HPL Mux / module_callback | delay 100..150 us |
| 172 | HPL Mux / module_callback | U(0x258d,0x70,0x0) |
| 173 | HPL Mux / module_callback | delay 100..150 us |

### down

Widget order: DACL → DACR → Headphone L → Headphone R → SGEN DL → DAC In Mux → HPL Mux → HPR Mux → IBIST → HP_ANA_TRIM → NV_REGULATOR → LDO → LDO_REMOTE → HP_MUTE → HP_DAMP → ESD_RESIST → AFE_DL_SRC → AFE_ON → NCP → SDM → SDM_3RD → SDM_FIFO_CLK → AUDIO_TOP_AFE_CTL → AUDIO_TOP_DAC_CTL → AUDIO_TOP_PWR_CLK → AUDIO_TOP_PDN_AFE_TESTMODEL → AUDIO_TOP_PDN_RESERVED → AUD_CK → AUDNCP_CK → ZCD13M_CK → AUDIF_CK → CLKSQ Audio → HP_PULL_DOWN → AUDGLB → CLK_BUF → DL Digital Clock → DL Digital Clock CH_1_2 → DL Digital Clock CH_3 → DL Power Supply → HP_Supply → SGEN DL SRC → SGEN DL Enable → SGEN MUTE → mt6368_vaud18

| # | Widget / origin | Operation |
|---|---|---|
| 1 | HPR Mux / module_callback | U(0x258d,0x70,0x0) |
| 2 | HPR Mux / module_callback | delay 100..150 us |
| 3 | HPR Mux / module_callback | U(0x258d,0x70,0x10) |
| 4 | HPR Mux / module_callback | delay 100..150 us |
| 5 | HPR Mux / module_callback | U(0x258d,0x70,0x20) |
| 6 | HPR Mux / module_callback | delay 100..150 us |
| 7 | HPR Mux / module_callback | U(0x258d,0x70,0x30) |
| 8 | HPR Mux / module_callback | delay 100..150 us |
| 9 | HPR Mux / module_callback | U(0x258d,0x70,0x40) |
| 10 | HPR Mux / module_callback | delay 100..150 us |
| 11 | HPR Mux / module_callback | U(0x258d,0x70,0x50) |
| 12 | HPR Mux / module_callback | delay 100..150 us |
| 13 | HPR Mux / module_callback | U(0x258d,0x70,0x60) |
| 14 | HPR Mux / module_callback | delay 100..150 us |
| 15 | HPR Mux / module_callback | U(0x258d,0x70,0x70) |
| 16 | HPR Mux / module_callback | delay 100..150 us |
| 17 | HPR Mux / module_callback | U(0x2589,0xf,0x0) |
| 18 | HPR Mux / module_callback | U(0x259a,0x1,0x0) |
| 19 | HPR Mux / module_callback | U(0x2588,0xf,0x0) |
| 20 | HPR Mux / module_callback | U(0x25a2,0x1,0x0) |
| 21 | HPR Mux / module_callback | W(0x258b,0x77) |
| 22 | HPR Mux / module_callback | W(0x258a,0xc3) |
| 23 | HPR Mux / module_callback | W(0x258a,0xcf) |
| 24 | HPR Mux / module_callback | U(0x260b,0xf9f,0x1e) |
| 25 | HPR Mux / module_callback | U(0x260c,0xf9f,0x1e) |
| 26 | HPR Mux / module_callback | delay 600..650 us |
| 27 | HPR Mux / module_callback | W(0x258a,0xff) |
| 28 | HPR Mux / module_callback | U(0x259b,0xf0,0xf0) |
| 29 | HPR Mux / module_callback | delay 600..650 us |
| 30 | HPR Mux / module_callback | U(0x259b,0xf0,0xe0) |
| 31 | HPR Mux / module_callback | delay 600..650 us |
| 32 | HPR Mux / module_callback | U(0x259b,0xf0,0xd0) |
| 33 | HPR Mux / module_callback | delay 600..650 us |
| 34 | HPR Mux / module_callback | U(0x259b,0xf0,0xc0) |
| 35 | HPR Mux / module_callback | delay 600..650 us |
| 36 | HPR Mux / module_callback | U(0x259b,0xf0,0xb0) |
| 37 | HPR Mux / module_callback | delay 600..650 us |
| 38 | HPR Mux / module_callback | U(0x259b,0xf0,0xa0) |
| 39 | HPR Mux / module_callback | delay 600..650 us |
| 40 | HPR Mux / module_callback | U(0x259b,0xf0,0x90) |
| 41 | HPR Mux / module_callback | delay 600..650 us |
| 42 | HPR Mux / module_callback | U(0x259b,0xf0,0x80) |
| 43 | HPR Mux / module_callback | delay 600..650 us |
| 44 | HPR Mux / module_callback | U(0x259b,0xf0,0x70) |
| 45 | HPR Mux / module_callback | delay 600..650 us |
| 46 | HPR Mux / module_callback | U(0x259b,0xf0,0x60) |
| 47 | HPR Mux / module_callback | delay 600..650 us |
| 48 | HPR Mux / module_callback | U(0x259b,0xf0,0x50) |
| 49 | HPR Mux / module_callback | delay 600..650 us |
| 50 | HPR Mux / module_callback | U(0x259b,0xf0,0x40) |
| 51 | HPR Mux / module_callback | delay 600..650 us |
| 52 | HPR Mux / module_callback | U(0x259b,0xf0,0x30) |
| 53 | HPR Mux / module_callback | delay 600..650 us |
| 54 | HPR Mux / module_callback | U(0x259b,0xf0,0x20) |
| 55 | HPR Mux / module_callback | delay 600..650 us |
| 56 | HPR Mux / module_callback | U(0x259b,0xf0,0x10) |
| 57 | HPR Mux / module_callback | delay 600..650 us |
| 58 | HPR Mux / module_callback | U(0x259b,0xf0,0x0) |
| 59 | HPR Mux / module_callback | delay 600..650 us |
| 60 | HPR Mux / module_callback | U(0x258b,0x7,0x7) |
| 61 | HPR Mux / module_callback | U(0x258b,0x70,0x70) |
| 62 | HPR Mux / module_callback | delay 600..650 us |
| 63 | HPR Mux / module_callback | U(0x258b,0x7,0x6) |
| 64 | HPR Mux / module_callback | U(0x258b,0x70,0x60) |
| 65 | HPR Mux / module_callback | delay 600..650 us |
| 66 | HPR Mux / module_callback | U(0x258b,0x7,0x5) |
| 67 | HPR Mux / module_callback | U(0x258b,0x70,0x50) |
| 68 | HPR Mux / module_callback | delay 600..650 us |
| 69 | HPR Mux / module_callback | U(0x258b,0x7,0x4) |
| 70 | HPR Mux / module_callback | U(0x258b,0x70,0x40) |
| 71 | HPR Mux / module_callback | delay 600..650 us |
| 72 | HPR Mux / module_callback | U(0x258b,0x7,0x3) |
| 73 | HPR Mux / module_callback | U(0x258b,0x70,0x30) |
| 74 | HPR Mux / module_callback | delay 600..650 us |
| 75 | HPR Mux / module_callback | U(0x258b,0x7,0x2) |
| 76 | HPR Mux / module_callback | U(0x258b,0x70,0x20) |
| 77 | HPR Mux / module_callback | delay 600..650 us |
| 78 | HPR Mux / module_callback | U(0x258b,0x7,0x1) |
| 79 | HPR Mux / module_callback | U(0x258b,0x70,0x10) |
| 80 | HPR Mux / module_callback | delay 600..650 us |
| 81 | HPR Mux / module_callback | U(0x258b,0x7,0x0) |
| 82 | HPR Mux / module_callback | U(0x258b,0x70,0x0) |
| 83 | HPR Mux / module_callback | delay 600..650 us |
| 84 | HPR Mux / module_callback | U(0x258a,0x3,0x0) |
| 85 | HPR Mux / module_callback | W(0x259a,0x1) |
| 86 | HPR Mux / module_callback | W(0x259b,0xe) |
| 87 | HPR Mux / module_callback | W(0x259b,0xc) |
| 88 | HPR Mux / module_callback | U(0x258a,0xc0,0x0) |
| 89 | HPR Mux / module_callback | U(0x2588,0x30,0x0) |
| 90 | HPR Mux / module_callback | U(0x2588,0xc0,0x0) |
| 91 | HPR Mux / module_callback | W(0x259a,0x1) |
| 92 | HPR Mux / module_callback | W(0x259b,0x2) |
| 93 | HPR Mux / module_callback | U(0x258a,0x30,0x0) |
| 94 | HPR Mux / module_callback | U(0x258a,0xc,0x0) |
| 95 | IBIST / modeled_dapm_coalesced | U(0x25a0,0x10,0x10) |
| 96 | HP_ANA_TRIM / modeled_dapm_coalesced | U(0x258d,0x80,0x0) |
| 97 | HP_ANA_TRIM / module_callback | W(0x258e,0x0) |
| 98 | HP_ANA_TRIM / module_callback | W(0x258f,0x0) |
| 99 | NV_REGULATOR / modeled_dapm_coalesced | U(0x25a3,0x10,0x0) |
| 100 | LDO / modeled_dapm_coalesced | U(0x25a3,0x1,0x0) |
| 101 | LDO_REMOTE / modeled_dapm_coalesced | U(0x25a3,0x4,0x0) |
| 102 | HP_MUTE / module_callback | W(0x260b,0xf9f) |
| 103 | HP_MUTE / module_callback | W(0x260c,0xf9f) |
| 104 | HP_DAMP / module_callback | W(0x259c,0x0) |
| 105 | HP_DAMP / module_callback | W(0x259d,0x0) |
| 106 | ESD_RESIST / module_callback | U(0x258d,0x1,0x0) |
| 107 | AFE_DL_SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x0) |
| 108 | AFE_ON / modeled_dapm_coalesced | U(0x2388,0x1,0x0) |
| 109 | NCP / modeled_dapm_coalesced | U(0x2488,0x1,0x0) |
| 110 | SDM / module_callback | U(0x2396,0xfd,0x0) |
| 111 | SDM / module_callback | W(0x2393,0xcb) |
| 112 | SDM / module_callback | W(0x2392,0xa0) |
| 113 | SDM_3RD / module_callback | W(0x23a6,0x0) |
| 114 | SDM_3RD / module_callback | W(0x23a3,0xcb) |
| 115 | SDM_3RD / module_callback | W(0x23a2,0xa0) |
| 116 | SDM_FIFO_CLK / modeled_dapm_coalesced | U(0x2396,0x2,0x0) |
| 117 | AUDIO_TOP_AFE_CTL / module_callback | delay 250..270 us |
| 118 | AUDIO_TOP_AFE_CTL / modeled_dapm_coalesced | U(0x238f,0x80,0x80) |
| 119 | AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_AFE_TESTMODEL + AUDIO_TOP_PDN_RESERVED / modeled_dapm_coalesced | U(0x238f,0x47,0x47) |
| 120 | AUD_CK / module_callback | delay 250..270 us |
| 121 | AUD_CK / modeled_dapm_coalesced | U(0x230c,0x2,0x2) |
| 122 | AUDNCP_CK + ZCD13M_CK + AUDIF_CK / modeled_dapm_coalesced | U(0x230c,0x64,0x64) |
| 123 | CLKSQ Audio / modeled_dapm_coalesced | U(0x2537,0x1,0x0) |
| 124 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x70) |
| 125 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 126 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x60) |
| 127 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 128 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x50) |
| 129 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 130 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x40) |
| 131 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 132 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x30) |
| 133 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 134 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x20) |
| 135 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 136 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x10) |
| 137 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 138 | HP_PULL_DOWN / module_callback | U(0x258d,0x70,0x0) |
| 139 | HP_PULL_DOWN / module_callback | delay 100..150 us |
| 140 | AUDGLB / modeled_dapm_coalesced | U(0x25a2,0x10,0x10) |
| 141 | CLK_BUF / module_callback | external mt6685_set_dcxo(False) |
| 142 | SGEN DL SRC / modeled_dapm_coalesced | U(0x2389,0x1,0x0) |
| 143 | SGEN DL Enable + SGEN MUTE / modeled_dapm_coalesced | U(0x23c8,0xc0,0x40) |
| 144 | SGEN MUTE / module_callback | W(0x2396,0x0) |
| 145 | SGEN MUTE / module_callback | W(0x2393,0xcb) |
| 146 | SGEN MUTE / module_callback | W(0x2392,0xa0) |
| 147 | mt6368_vaud18 / module_callback | external regulator event 8 |

