/*
 * irrx/mt8127/mtk_ir_hal.c - IRRX HAL for MT8127, board-file (non-DT) 3.10 kernel.
 * Replaces the MT8173 DT-based HAL file. All values come from the stock
 * 3.4 kernel decompile.
 *
 *   IRRX regs   virt 0xF0013000 (stock) -> phys 0x10013000 (INFERRED, verify)
 *   Clock       enable_clock(0x2e = MT_CG_INFRA_IRRX, "IRRX")
 *   Pin         mt_set_gpio_mode(0x12, 6)   (GPIO 18, mode 6)
 *   IRQ         119, level-low (IRQF_TRIGGER_LOW == 8), see board file part
 *   Regs        0x00 COUNT_HIGH, 0x20 IRCLR, 0xCC INT_EN, 0xD0 INT_CLR
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <mach/mt_gpio.h>
#include <mach/mt_clkmgr.h>

#include "mtk_ir_common.h"
#include "mtk_ir_regs.h"
#include "mtk_ir_core.h"

#define MT8127_IRRX_PHYS	0x10013000UL	/* inferred from 0xF0013000 */
#define MT8127_IRRX_SIZE	0x1000
#define MT8127_IRRX_IRQ		119
#define MT8127_IRRX_GPIO	18		/* 0x12 */
#define MT8127_IRRX_GPIO_MODE	6

/* register offsets, as used by the stock driver */
#define IR8127_COUNT_HIGH	0x00
#define IR8127_IRCLR		0x20
#define IR8127_INT_EN		0xCC
#define IR8127_INT_CLR		0xD0
/* last known register (INTSTAT); keeps the sysfs "register" dump under one page */
#define IR8127_LAST_REG		0xD4

unsigned long IRRX_BASE_PHY;
unsigned long IRRX_BASE_PHY_END;
unsigned long IRRX_BASE_VIRTUAL;
unsigned long IRRX_BASE_VIRTUAL_END;

static inline u32 ir_rd(u32 off)
{
	return readl((void __iomem *)(IRRX_BASE_VIRTUAL + off));
}

static inline void ir_wr(u32 off, u32 val)
{
	writel(val, (void __iomem *)(IRRX_BASE_VIRTUAL + off));
}

/* Call once, before anything touches registers (replaces the DT lookup). */
int mtk_ir_hal_map_regs(void)
{
	void __iomem *base;

	if (IRRX_BASE_VIRTUAL)
		return 0;
	base = ioremap(MT8127_IRRX_PHYS, MT8127_IRRX_SIZE);
	if (!base) {
		IR_LOG_ALWAYS("ioremap irrx failed\n");
		return -ENOMEM;
	}
	IRRX_BASE_PHY = MT8127_IRRX_PHYS;
	IRRX_BASE_PHY_END = MT8127_IRRX_PHYS + IR8127_LAST_REG;
	IRRX_BASE_VIRTUAL = (unsigned long)base;
	IRRX_BASE_VIRTUAL_END = IRRX_BASE_VIRTUAL + IR8127_LAST_REG;
	return 0;
}

/* ---- clock: legacy mt_clkmgr, nothing to probe ---- */
int mtk_ir_hal_clock_probe(void)
{
	return mtk_ir_hal_map_regs();
}

int mt_ir_core_clock_enable(bool bEnable)
{
	int ret;

	/*
	 * The stock core calls this first in init and never calls a clock
	 * probe, so make sure the registers are mapped here as well.
	 */
	ret = mtk_ir_hal_map_regs();
	if (ret)
		return ret;

	if (bEnable)
		enable_clock(MT_CG_INFRA_IRRX, "IRRX");
	else
		disable_clock(MT_CG_INFRA_IRRX, "IRRX");
	return 0;
}

/* separate IRDA gate is an MT8173 detail */
int mt_ir_core_irda_clock_enable(bool bEnable)
{
	return 0;
}

/* no PMIC rail for the receiver in the stock driver */
int mt_ir_hal_power_probe(void)
{
	return 0;
}

int mt_ir_hal_power_enable(bool bEnable)
{
	return 0;
}

/* stock mtk_ir_core_init does exactly this */
int mtk_ir_core_pin_set(void)
{
	mt_set_gpio_mode(MT8127_IRRX_GPIO, MT8127_IRRX_GPIO_MODE);
	return 0;
}

/* ---- interrupt control (stock sequence) ---- */
void mtk_ir_core_disable_hwirq(void)
{
	ir_wr(IR8127_INT_EN, ir_rd(IR8127_INT_EN) & ~1U);
	dsb();
}

/* stock: mtk_ir_core_clear_hwirq_stat */
void mtk_ir_core_clear_hwirq_stat(void)
{
	int tries = 100000;

	ir_wr(IR8127_IRCLR, ir_rd(IR8127_IRCLR) | 1);	/* state machine */
	dsb();
	ir_wr(IR8127_INT_CLR, ir_rd(IR8127_INT_CLR) | 1);	/* int status */
	dsb();
	while (ir_rd(IR8127_COUNT_HIGH) != 0 && --tries)
		cpu_relax();	/* stock spins forever; bounded here */
	if (!tries)
		IR_LOG_ALWAYS("COUNT_HIGH never cleared, is the clock on?\n");
	ir_wr(IR8127_INT_EN, ir_rd(IR8127_INT_EN) | 1);	/* re-enable */
	dsb();
}

/* name used by the 8173 tree's core */
void mtk_ir_core_clear_hwirq(void)
{
	mtk_ir_core_clear_hwirq_stat();
}

void mtk_ir_core_direct_clear_hwirq(void)
{
	mtk_ir_core_clear_hwirq_stat();
}

/*
 * Helpers the shared core / old HAL may still reference. Harmless if unused.
 */
void mt_ir_reg_remap(unsigned long pyaddress, unsigned long value, bool bWrite)
{
	void __iomem *p = ioremap(pyaddress, 0x4);

	if (!p) {
		IR_LOG_ALWAYS("reg_remap (0x%lx) error\n", pyaddress);
		return;
	}
	IR_LOG_ALWAYS("reg_remap (0x%lx) value=(0x%x)\n", pyaddress, readl(p));
	if (bWrite)
		writel(value, p);
	iounmap(p);
}

void mtk_ir_check_interrupt(void)
{
	/* 8173 GIC debug, not applicable */
}

/*
 * Check-count overflow, from stock mtk_ir_nec_check_cnt.
 * EXPBCNT is at 0x28; the check count is bits [12:6] (7 bits). When it
 * reaches 0x7f the receiver is stuck and must be cleared.
 */
#define IR8127_EXPBCNT		0x28
#define IR8127_CHK_CNT_SHIFT	6
#define IR8127_CHK_CNT_MAX	0x7f

static unsigned int mtk_ir_chk_cnt_overflows;	/* stock keeps a counter at +4 of a global */

int mtk_ir_check_cnt(void)
{
	u32 chk = (ir_rd(IR8127_EXPBCNT) & 0x1fff) >> IR8127_CHK_CNT_SHIFT;

	if (chk != IR8127_CHK_CNT_MAX)
		return 0;
	mtk_ir_chk_cnt_overflows++;
	mtk_ir_core_clear_hwirq_stat();
	return 1;
}

/* name used by the stock core */
int mtk_ir_nec_check_cnt(void)
{
	return mtk_ir_check_cnt();
}

/* mtk_ir_check_interrupt (8173 GIC debug, IRQ 140) is dropped. */
