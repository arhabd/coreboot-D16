/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <drivers/amd/amdmct/wrappers/mcti.h>
#include "mct_d_gcc.h"

void mct_BeforeDQSTrainSamp(struct DCTStatStruc *pDCTstat)
{
	u32 val;

	if (pDCTstat->LogicalCPUID & AMD_DR_Bx) {
		Set_NB32(pDCTstat->dev_dct, 0x98, 0x0D004007);
		val = Get_NB32(pDCTstat->dev_dct, 0x9C);
		val |= 0x3FF;
		Set_NB32(pDCTstat->dev_dct, 0x9C, val);
		Set_NB32(pDCTstat->dev_dct, 0x98, 0x4D0F4F07);

		Set_NB32(pDCTstat->dev_dct, 0x198, 0x0D004007);
		val = Get_NB32(pDCTstat->dev_dct, 0x19C);
		val |= 0x3FF;
		Set_NB32(pDCTstat->dev_dct, 0x19C, val);
		Set_NB32(pDCTstat->dev_dct, 0x198, 0x4D0F4F07);
	}
}

void mct_ExtMCTConfig_Bx(struct DCTStatStruc *pDCTstat)
{
	if (pDCTstat->LogicalCPUID & (AMD_DR_Bx)) {
		Set_NB32(pDCTstat->dev_dct, 0x11C, 0x0FE40FC0 | 1 << 29/* FlushWrOnStpGnt */);
	}
}
