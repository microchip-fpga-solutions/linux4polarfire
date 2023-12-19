// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Dan Sneddon
 */

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define DSI_PHY_RSTZ			0xa0
#define PHY_SHUTDOWNZ			0

#define DSI_PHY_TST_CTRL0		0xb4
#define PHY_TESTCLK			BIT(1)
#define PHY_UNTESTCLK			0
#define PHY_TESTCLR			BIT(0)
#define PHY_UNTESTCLR			0

#define DSI_PHY_TST_CTRL1		0xb8
#define PHY_TESTEN			BIT(16)
#define PHY_UNTESTEN			0
#define PHY_TESTDOUT(n)			(((n) & 0xff) << 8)
#define PHY_TESTDIN(n)			(((n) & 0xff) << 0)

#define BYPASS_VCO_RANGE	BIT(7)
#define VCO_RANGE_CON_SEL(val)	(((val) & 0x7) << 3)
#define VCO_IN_CAP_CON_DEFAULT	(0x0 << 1)
#define VCO_IN_CAP_CON_LOW	(0x1 << 1)
#define VCO_IN_CAP_CON_HIGH	(0x2 << 1)
#define REF_BIAS_CUR_SEL	BIT(0)

#define CP_CURRENT_3UA	0x1
#define CP_CURRENT_4_5UA	0x2
#define CP_CURRENT_7_5UA	0x6
#define CP_CURRENT_6UA	0x9
#define CP_CURRENT_12UA	0xb
#define CP_CURRENT_SEL(val)	((val) & 0xf)
#define CP_PROGRAM_EN		BIT(7)

#define LPF_RESISTORS_15_5KOHM	0x1
#define LPF_RESISTORS_13KOHM	0x2
#define LPF_RESISTORS_11_5KOHM	0x4
#define LPF_RESISTORS_10_5KOHM	0x8
#define LPF_RESISTORS_8KOHM	0x10
#define LPF_PROGRAM_EN		BIT(6)
#define LPF_RESISTORS_SEL(val)	((val) & 0x3f)

#define HSFREQRANGE_SEL(val)	(((val) & 0x3f) << 1)

#define INPUT_DIVIDER(val)	(((val) - 1) & 0x7f)
#define LOW_PROGRAM_EN		0
#define HIGH_PROGRAM_EN		BIT(7)
#define LOOP_DIV_LOW_SEL(val)	(((val) - 1) & 0x1f)
#define LOOP_DIV_HIGH_SEL(val)	((((val) - 1) >> 5) & 0xf)
#define PLL_LOOP_DIV_EN		BIT(5)
#define PLL_INPUT_DIV_EN	BIT(4)

#define PLL_BIAS_CUR_SEL_CAP_VCO_CONTROL		0x10
#define PLL_CP_CONTROL_PLL_LOCK_BYPASS			0x11
#define PLL_LPF_AND_CP_CONTROL				0x12
#define PLL_INPUT_DIVIDER_RATIO				0x17
#define PLL_LOOP_DIVIDER_RATIO				0x18
#define PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL	0x19

#define SFR_ISS_CFG	0x240

struct dw_mipi_dsi_mchp {
	struct device *dev;
	void __iomem *base;
	struct dw_mipi_dsi_plat_data pdata;

	/* needed for PLL config */
	unsigned int lane_mbps;
	u16 input_div;
	u16 feedback_div;
	u32 format;

	struct clk *pclk;
	struct clk *pllref_clk;
	const struct mipi_dsi_config *config;
};

struct dphy_pll_parameter_map {
	unsigned int max_mbps;
	u8 hsfreqrange;
	u8 icpctrl;
	u8 lpfctrl;
};

/* The table is based on 27MHz DPHY pll reference clock. */
static const struct dphy_pll_parameter_map dppa_map[] = {
	{  89, 0x00, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{  99, 0x10, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{ 109, 0x20, CP_CURRENT_3UA, LPF_RESISTORS_13KOHM },
	{ 129, 0x01, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 139, 0x11, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 149, 0x21, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 169, 0x02, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 179, 0x12, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 199, 0x22, CP_CURRENT_6UA, LPF_RESISTORS_13KOHM },
	{ 219, 0x03, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 239, 0x13, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 249, 0x23, CP_CURRENT_4_5UA, LPF_RESISTORS_13KOHM },
	{ 269, 0x04, CP_CURRENT_6UA, LPF_RESISTORS_11_5KOHM },
	{ 299, 0x14, CP_CURRENT_6UA, LPF_RESISTORS_11_5KOHM },
	{ 329, 0x05, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 359, 0x15, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 399, 0x25, CP_CURRENT_3UA, LPF_RESISTORS_15_5KOHM },
	{ 449, 0x06, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 499, 0x16, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 549, 0x07, CP_CURRENT_7_5UA, LPF_RESISTORS_10_5KOHM },
	{ 599, 0x17, CP_CURRENT_7_5UA, LPF_RESISTORS_10_5KOHM },
	{ 649, 0x08, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 699, 0x18, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 749, 0x09, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 799, 0x19, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 849, 0x29, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 899, 0x39, CP_CURRENT_7_5UA, LPF_RESISTORS_11_5KOHM },
	{ 949, 0x0a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{ 999, 0x1a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1049, 0x2a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1099, 0x3a, CP_CURRENT_12UA, LPF_RESISTORS_8KOHM },
	{1149, 0x0b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1199, 0x1b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1249, 0x2b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1299, 0x3b, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1349, 0x0c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1399, 0x1c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1449, 0x2c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM },
	{1500, 0x3c, CP_CURRENT_12UA, LPF_RESISTORS_10_5KOHM }
};

static int max_mbps_to_parameter(unsigned int max_mbps)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dppa_map); i++)
		if (dppa_map[i].max_mbps >= max_mbps)
			return i;

	return -EINVAL;
}

static inline void dsi_write(struct dw_mipi_dsi_mchp *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi_mchp *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static void dw_mipi_dsi_phy_write(struct dw_mipi_dsi_mchp *dsi,
				  u8 test_code,
				  u8 test_data)
{
	/*
	 * With the falling edge on TESTCLK, the TESTDIN[7:0] signal content
	 * is latched internally as the current test code. Test data is
	 * programmed internally by rising edge on TESTCLK.
	 */

	/* General DPHY control operation */

	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK | PHY_UNTESTCLR);
	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_TESTEN | PHY_TESTDOUT(1) |
						PHY_TESTDIN(test_code));
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_UNTESTCLK | PHY_UNTESTCLR);
	dsi_write(dsi, DSI_PHY_TST_CTRL1, PHY_UNTESTEN | PHY_TESTDOUT(0) |
						PHY_TESTDIN(test_data));
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_TESTCLK | PHY_UNTESTCLR);
	dsi_write(dsi, DSI_PHY_TST_CTRL0, PHY_UNTESTCLK | PHY_UNTESTCLR);
}

static int phy_init(void *priv_data)
{
	struct dw_mipi_dsi_mchp *dsi = priv_data;
	int ret, i, vco;

	vco = (dsi->lane_mbps < 200) ? 0 : (dsi->lane_mbps + 100) / 200;

	i = max_mbps_to_parameter(dsi->lane_mbps);
	if (i < 0)
		return i;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret)
		return ret;
	/*
	 * Added only the required data codes defined in the datasheet and
	 * removing the unwanted datacodes
	 */

	/* D-PHY in Shutdown mode */
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_SHUTDOWNZ);

	dw_mipi_dsi_phy_write(dsi, PLL_BIAS_CUR_SEL_CAP_VCO_CONTROL,
			      BYPASS_VCO_RANGE |
			      VCO_RANGE_CON_SEL(vco) |
			      VCO_IN_CAP_CON_LOW |
			      REF_BIAS_CUR_SEL);

	dw_mipi_dsi_phy_write(dsi, PLL_CP_CONTROL_PLL_LOCK_BYPASS,
			      CP_CURRENT_SEL(dppa_map[i].icpctrl));
	dw_mipi_dsi_phy_write(dsi, PLL_LPF_AND_CP_CONTROL,
			      CP_PROGRAM_EN | LPF_PROGRAM_EN |
			      LPF_RESISTORS_SEL(dppa_map[i].lpfctrl));
	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_DIVIDER_RATIO,
			      INPUT_DIVIDER(dsi->input_div));
	dw_mipi_dsi_phy_write(dsi, PLL_LOOP_DIVIDER_RATIO,
			      LOOP_DIV_LOW_SEL(dsi->feedback_div) |
			      LOW_PROGRAM_EN);

	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL,
			      PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);
	dw_mipi_dsi_phy_write(dsi, PLL_LOOP_DIVIDER_RATIO,
			      LOOP_DIV_HIGH_SEL(dsi->feedback_div) |
			      HIGH_PROGRAM_EN);
	dw_mipi_dsi_phy_write(dsi, PLL_INPUT_AND_LOOP_DIVIDER_RATIOS_CONTROL,
			      PLL_LOOP_DIV_EN | PLL_INPUT_DIV_EN);
	return ret;
}

static int phy_get_lane_mbps(void *priv_data,
			     const struct drm_display_mode *mode,
			     unsigned long mode_flags, u32 lanes, u32 format,
			     unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_mchp *dsi = priv_data;
	int bpp;
	unsigned long mpclk, tmp;
	unsigned int target_mbps = 1000;
	unsigned int max_mbps = dppa_map[ARRAY_SIZE(dppa_map) - 1].max_mbps;
	unsigned long best_freq = 0;
	unsigned long fvco_min, fvco_max, fin, fout;
	unsigned int min_prediv, max_prediv;
	unsigned int _prediv, best_prediv;
	unsigned long _fbdiv, best_fbdiv;
	unsigned long min_delta = ULONG_MAX;

	dsi->format = format;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (bpp < 0)
		return bpp;

	mpclk = DIV_ROUND_UP(mode->clock, MSEC_PER_SEC);
	if (mpclk) {
		/* take 1/0.8, since mbps must be bigger than bandwidth of RGB */
		tmp = mpclk * (bpp / lanes) * 10 / 8;
		if (tmp < max_mbps)
			target_mbps = tmp;
		else
			DRM_DEV_ERROR(dsi->dev,
				      "DPHY clock frequency is out of range\n");
	}

	fin = clk_get_rate(dsi->pllref_clk);
	fout = target_mbps * USEC_PER_SEC;

	/* constraint: 5Mhz <= Fref / N <= 40MHz */
	min_prediv = DIV_ROUND_UP(fin, 40 * USEC_PER_SEC);
	max_prediv = fin / (5 * USEC_PER_SEC);

	/* constraint: 80MHz <= Fvco <= 1500Mhz */
	fvco_min = 80 * USEC_PER_SEC;
	fvco_max = 1500 * USEC_PER_SEC;

	for (_prediv = min_prediv; _prediv <= max_prediv; _prediv++) {
		u64 tmp;
		u32 delta;
		/* Fvco = Fref * M / N */
		tmp = (u64)fout * _prediv;
		do_div(tmp, fin);
		_fbdiv = tmp;
		/*
		 * Due to the use of a "by 2 pre-scaler," the range of the
		 * feedback multiplication value M is limited to even division
		 * numbers, and m must be greater than 6, not bigger than 512.
		 */
		if (_fbdiv < 6 || _fbdiv > 512)
			continue;

		_fbdiv += _fbdiv % 2;

		tmp = (u64)_fbdiv * fin;
		do_div(tmp, _prediv);
		if (tmp < fvco_min || tmp > fvco_max)
			continue;

		delta = abs(fout - tmp);
		if (delta < min_delta) {
			best_prediv = _prediv;
			best_fbdiv = _fbdiv;
			min_delta = delta;
			best_freq = tmp;
		}
	}

	if (best_freq) {
		dsi->lane_mbps = DIV_ROUND_UP(best_freq, USEC_PER_SEC);
		*lane_mbps = dsi->lane_mbps;
		dsi->input_div = best_prediv;
		dsi->feedback_div = best_fbdiv;
	} else {
		DRM_DEV_ERROR(dsi->dev, "Can not find best_freq for DPHY\n");
		return -EINVAL;
	}

	return 0;
}

struct hstt {
	unsigned int maxfreq;
	struct dw_mipi_dsi_dphy_timing timing;
};

#define HSTT(_maxfreq, _c_lp2hs, _c_hs2lp, _d_lp2hs, _d_hs2lp)	\
{					\
	.maxfreq = _maxfreq,		\
	.timing = {			\
		.clk_lp2hs = _c_lp2hs,	\
		.clk_hs2lp = _c_hs2lp,	\
		.data_lp2hs = _d_lp2hs,	\
		.data_hs2lp = _d_hs2lp,	\
	}				\
}

struct hstt hstt_table[] = {
	HSTT(90,  32, 20,  26, 13),
	HSTT(100,  35, 23,  28, 14),
	HSTT(110,  32, 22,  26, 13),
	HSTT(130,  31, 20,  27, 13),
	HSTT(140,  33, 22,  26, 14),
	HSTT(150,  33, 21,  26, 14),
	HSTT(170,  32, 20,  27, 13),
	HSTT(180,  36, 23,  30, 15),
	HSTT(200,  40, 22,  33, 15),
	HSTT(220,  40, 22,  33, 15),
	HSTT(240,  44, 24,  36, 16),
	HSTT(250,  48, 24,  38, 17),
	HSTT(270,  48, 24,  38, 17),
	HSTT(300,  50, 27,  41, 18),
	HSTT(330,  56, 28,  45, 18),
	HSTT(360,  59, 28,  48, 19),
	HSTT(400,  61, 30,  50, 20),
	HSTT(450,  67, 31,  55, 21),
	HSTT(500,  73, 31,  59, 22),
	HSTT(550,  79, 36,  63, 24),
	HSTT(600,  83, 37,  68, 25),
	HSTT(650,  90, 38,  73, 27),
	HSTT(700,  95, 40,  77, 28),
	HSTT(750, 102, 40,  84, 28),
	HSTT(800, 106, 42,  87, 30),
	HSTT(850, 113, 44,  93, 31),
	HSTT(900, 118, 47,  98, 32),
	HSTT(950, 124, 47, 102, 34),
	HSTT(1000, 130, 49, 107, 35),
};

static int phy_get_timing(void *priv_data, unsigned int lane_mbps,
			  struct dw_mipi_dsi_dphy_timing *timing)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hstt_table); i++)
		if (lane_mbps < hstt_table[i].maxfreq)
			break;

	if (i == ARRAY_SIZE(hstt_table))
		i--;

	*timing = hstt_table[i].timing;

	return 0;
}

struct dw_mipi_dsi_phy_ops dw_mipi_dsi_mchp_phy_ops = {
	.init = phy_init,
	.get_lane_mbps = phy_get_lane_mbps,
	.get_timing = phy_get_timing,
};

static int dw_mipi_dsi_mchp_probe(struct platform_device *pdev)
{
	struct dw_mipi_dsi_mchp *dsi;
	struct resource *res;
	struct regmap *sfr;
	int ret;

	dsi = devm_kzalloc(&pdev->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;
	platform_set_drvdata(pdev, dsi);
	dsi->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dsi->base))
		return PTR_ERR(dsi->base);

	dsi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dsi->pclk))
		return PTR_ERR(dsi->pclk);
	ret = clk_prepare_enable(dsi->pclk);
	if (ret)
		return ret;

	dsi->pllref_clk = devm_clk_get(&pdev->dev, "refclk");
	if (IS_ERR(dsi->pllref_clk))
		return PTR_ERR(dsi->pllref_clk);
	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret)
		return ret;

	if (dsi->config->has_sfr) {
		sfr = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "microchip,sfr");
		if (IS_ERR_OR_NULL(sfr))
			return PTR_ERR(sfr);
		ret = regmap_write(sfr, SFR_ISS_CFG, 1);
		if (ret)
			return ret;
	}

	/*debug*/
	syscon_node_to_regmap(pdev->dev.of_node);

	dsi->pdata.base = dsi->base;
	dsi->pdata.max_data_lanes = 4;
	dsi->pdata.phy_ops = &dw_mipi_dsi_mchp_phy_ops;
	dsi->pdata.priv_data = dsi;

	/* call synopsis probe */
	dw_mipi_dsi_probe(pdev, &dsi->pdata);

	return ret;
}

const struct mipi_dsi_config sam9x7_config = {
	.has_sfr = true;
};

const struct mipi_dsi_config sama7d65_config = {
	.has_sfr = false;
};

static const struct of_device_id dw_mipi_dsi_mchp_dt_ids[] = {
	{
		.compatible = "microchip,sam9x7-mipi-dsi",
		.data = &sam9x7_config,
	},
	{
		.compatible = "microchip,sama7d65-mipi-dsi",
		.data = &sama7d65_config,
	},
	{ /* sentinel */ }
};

struct platform_driver dw_mipi_dsi_mchp_driver = {
	.probe = dw_mipi_dsi_mchp_probe,
	.driver = {
		.of_match_table = dw_mipi_dsi_mchp_dt_ids,
		.name = "dw-mipi-dsi-mchp",
	},
};
module_platform_driver(dw_mipi_dsi_mchp_driver);
