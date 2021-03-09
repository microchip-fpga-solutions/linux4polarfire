// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the Microchip SAMA7G5 USB 2.0 PHY
 *
 * Copyright (C) 2020 Microchip Technology, Inc. and its subsidiaries
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/at91/at91-sama7g5-sfr.h>
#include <soc/at91/microchip-rstc.h>

struct sama7g5_usb_phy {
	struct phy *phy;
	struct regmap *rstc;
	struct regmap *sfr;
	enum phy_mode mode;
	int port;
};

int sama7g5_usb_phy_set_mode(struct phy *phy, enum phy_mode mode,
			     int submode)
{
	struct sama7g5_usb_phy *sama7g5_phy = phy_get_drvdata(phy);

	sama7g5_phy->mode = PHY_MODE_INVALID;

	if (mode > 0)
		sama7g5_phy->mode = mode;

	/* Notify the controller when VBUS is present */
	if (mode == PHY_MODE_USB_DEVICE) {
		switch (sama7g5_phy->port) {
		case 0:
			regmap_update_bits(sama7g5_phy->sfr,
				SAMA7G5_SFR_UTMI0R0, SAMA7G5_SFR_UTMI_RX_VBUS,
				(submode ? SAMA7G5_SFR_UTMI_RX_VBUS : 0));
			break;
		case 1:
			regmap_update_bits(sama7g5_phy->sfr,
				SAMA7G5_SFR_UTMI0R1, SAMA7G5_SFR_UTMI_RX_VBUS,
				(submode ? SAMA7G5_SFR_UTMI_RX_VBUS : 0));
			break;
		case 2:
			regmap_update_bits(sama7g5_phy->sfr,
				SAMA7G5_SFR_UTMI0R2, SAMA7G5_SFR_UTMI_RX_VBUS,
				(submode ? SAMA7G5_SFR_UTMI_RX_VBUS : 0));
			break;
		default:
			return -ENODEV;
		}
	}

	dev_dbg(&sama7g5_phy->phy->dev, "USB PHY Set Mode port=%d, mode=%d\n",
		sama7g5_phy->port, sama7g5_phy->mode);

	return 0;
}

int sama7g5_usb_phy_init(struct phy *phy)
{
	struct sama7g5_usb_phy *sama7g5_phy = phy_get_drvdata(phy);

	/* Set TXPREEMPAMPTUNE to 1X */
	switch (sama7g5_phy->port) {
	case 0:
		regmap_update_bits(sama7g5_phy->sfr, SAMA7G5_SFR_UTMI0R0,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X);
		break;
	case 1:
		regmap_update_bits(sama7g5_phy->sfr, SAMA7G5_SFR_UTMI0R1,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X);
		break;
	case 2:
		regmap_update_bits(sama7g5_phy->sfr, SAMA7G5_SFR_UTMI0R1,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X,
				   SAMA7G5_SFR_UTMI_RX_TXPREEMPAMPTUNE_1X);
		break;
	default:
		return -ENODEV;
	}

	dev_dbg(&sama7g5_phy->phy->dev, "USB PHY Init , port=%d\n",
		sama7g5_phy->port);

	return 0;
}

int sama7g5_phy_power_on(struct phy *phy)
{
	struct sama7g5_usb_phy *sama7g5_phy = phy_get_drvdata(phy);

	if (!sama7g5_phy->rstc)
		return -ENODEV;

	/* OHCI is clocked by PHY 1 so enable it if we are in Host mode*/
	if (sama7g5_phy->mode == PHY_MODE_USB_HOST)
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST1, 0);

	/* Take the PHY out of reset */
	switch (sama7g5_phy->port) {
	case 0:
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST1, 0);
		break;
	case 1:
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST2, 0);
		break;
	case 2:
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST3, 0);
		break;
	default:
		return -ENODEV;
	}

	/* Datasheet states a minimum of 45 us before any USB operation */
	udelay(50);

	dev_dbg(&sama7g5_phy->phy->dev, "USB PHY Power On port=%d\n",
		sama7g5_phy->port);

	return 0;
}

int sama7g5_phy_power_off(struct phy *phy)
{
	struct sama7g5_usb_phy *sama7g5_phy = phy_get_drvdata(phy);

	if (!sama7g5_phy->rstc)
		return -ENODEV;

	/* Put the PHY in reset mode */
	switch (sama7g5_phy->port) {
	case 0:
		/* Host might still be running. Do not disable PHY 1 */
		break;
	case 1:
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST2, AT91_GRSTR_USB_RST2);
		break;
	case 2:
		regmap_update_bits(sama7g5_phy->rstc, AT91_RSTC_GRSTR,
				   AT91_GRSTR_USB_RST3, AT91_GRSTR_USB_RST3);
		break;
	default:
		return -ENODEV;
	}

	dev_dbg(&sama7g5_phy->phy->dev, "USB PHY Power Off port=%d\n",
		sama7g5_phy->port);

	return 0;
}

static struct phy_ops sama7g5_usb_phy_ops = {
	.init		= sama7g5_usb_phy_init,
	.power_on	= sama7g5_phy_power_on,
	.power_off	= sama7g5_phy_power_off,
	.set_mode	= sama7g5_usb_phy_set_mode,
	.owner		= THIS_MODULE,
};

int sama7g5_usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct sama7g5_usb_phy *sama7g5_phy;

	sama7g5_phy = devm_kzalloc(dev, sizeof(*sama7g5_phy), GFP_KERNEL);
	if (!sama7g5_phy)
		return -ENOMEM;

	sama7g5_phy->rstc =
		syscon_regmap_lookup_by_compatible("microchip,sama7g5-rstc");
	if (IS_ERR(sama7g5_phy->rstc)) {
		sama7g5_phy->rstc = NULL;
		dev_err(dev, "failed to get rstc\n");
		return -ENODEV;
	}

	sama7g5_phy->sfr =
		syscon_regmap_lookup_by_compatible("microchip,sama7g5-sfr");
	if (IS_ERR(sama7g5_phy->sfr)) {
		sama7g5_phy->sfr = NULL;
		dev_err(dev, "failed to get sfr\n");
		return -ENODEV;
	}

	if (of_property_read_u32(dev->of_node, "reg", &sama7g5_phy->port)) {
		dev_err(dev, "failed to get reg\n");
		return -ENODEV;
	}

	sama7g5_phy->phy = devm_phy_create(dev, NULL, &sama7g5_usb_phy_ops);
	if (IS_ERR(sama7g5_phy->phy))
		return PTR_ERR(sama7g5_phy->phy);

	phy_set_drvdata(sama7g5_phy->phy, sama7g5_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	dev_info(dev, "probed,  port=%d\n", sama7g5_phy->port);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id sama7g5_usb_phy_of_match[] = {
	{.compatible = "microchip,sama7g5-usb-phy",},
	{ },
};
MODULE_DEVICE_TABLE(of, sama7g5_usb_phy_of_match);

static struct platform_driver sama7g5_usb_phy_driver = {
	.probe	= sama7g5_usb_phy_probe,
	.driver = {
		.name	= "sama7g5-usb-phy",
		.of_match_table	= sama7g5_usb_phy_of_match,
	}
};
module_platform_driver(sama7g5_usb_phy_driver);

MODULE_AUTHOR("Cristian Birsan <cristian.birsan@microchip.com>");
MODULE_DESCRIPTION("Microchip SAMA7G5 USB PHY driver");
MODULE_LICENSE("GPL");
