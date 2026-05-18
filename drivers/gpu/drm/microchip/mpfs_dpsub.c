// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Display Subsystem Driver
 *
 * Copyright (C) 2025-2026 Microchip Technology Inc. and its subsidiaries
 *
 * Authors:
 * - Shravan Chippa <shravan.chippa@microchip.com>
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "mpfs_dpsub.h"

static irqreturn_t mpfs_dp_irq_handler(int irq, void *data)
{
	struct mpfs_dpsub *dpsub = data;
	u32 irq_status;

	irq_status = readl_relaxed(dpsub->base + MCHP_DRM_INT_STATUS);

	if (!(irq_status & MCHP_DRM_INT_STATUS_VSYNC)) {
		return IRQ_NONE;
	} else {
		writel_relaxed(MCHP_DRM_INT_STATUS_VSYNC, dpsub->base + MCHP_DRM_INT_STATUS);
		drm_crtc_handle_vblank(&dpsub->crtc);
	}

	return IRQ_HANDLED;
}

static int mpfs_dpsub_dumb_create(struct drm_file *file_priv,
				  struct drm_device *drm,
				  struct drm_mode_create_dumb *args)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(drm);
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/* Enforce the alignment constraints of the DMA engine. */
	args->pitch = ALIGN(pitch, dpsub->dma_align);

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

static struct drm_framebuffer *
mpfs_dpsub_fb_create(struct drm_device *drm, struct drm_file *file_priv,
		     const struct drm_format_info *info,
		     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(drm);
	struct drm_mode_fb_cmd2 cmd = *mode_cmd;
	unsigned int i;
	struct drm_framebuffer *drm_fb;

	for (i = 0; i < ARRAY_SIZE(cmd.pitches); ++i)
		cmd.pitches[i] = ALIGN(cmd.pitches[i], dpsub->dma_align);

	if (cmd.pixel_format == DRM_FORMAT_INVALID)
		cmd.pixel_format = DRM_FORMAT_XRGB8888;

	drm_fb = drm_gem_fb_create(drm, file_priv, info, &cmd);
	if (IS_ERR(drm_fb))
		dev_err(dpsub->dev, "drm_gem_fb_create failed %ld\n", PTR_ERR(drm_fb));

	return drm_fb;
}

static const struct drm_mode_config_funcs mpfs_dpsub_mode_config_funcs = {
	.fb_create		= mpfs_dpsub_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static struct drm_display_mode mpfs_drm_mode(unsigned int width,
					      unsigned int height)
{
	const struct drm_display_mode mode = {
		DRM_MODE_INIT(60, width, height, 0, 0)
	};

	return mode;
}

static int mpfs_drm_connector_helper_get_modes(struct drm_connector *connector)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(connector->dev);

	dpsub->mode = mpfs_drm_mode(MPFS_DISP_DEF_WIDTH, MPFS_DISP_DEF_HEIGHT);

	return drm_connector_helper_get_modes_fixed(connector, &dpsub->mode);
}

static const struct drm_connector_helper_funcs mpfs_connector_helper_funcs = {
	.get_modes = mpfs_drm_connector_helper_get_modes,
};

static const struct drm_connector_funcs mpfs_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void mpfs_dpsub_init(struct mpfs_dpsub *dpsub)
{
	u32 hres, vres;

	writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base_dp + MCHP_DRM_CTRL_REG);
	writel_relaxed(MCHP_DRM_FRAME_START, dpsub->base_dp + MCHP_DRM_CTRL_REG);

	hres = readl_relaxed(dpsub->base_dp + MPFS_DISP_WIDTH_OFFSET);
	vres = readl_relaxed(dpsub->base_dp + MPFS_DISP_HEIGHT_OFFSET);
	dev_info(dpsub->dev, "Display controller resolution width %d height %d\n", hres, vres);

	writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base + MCHP_DRM_CTRL_REG);
	writel_relaxed(MCHP_DRM_FRAME_START, dpsub->base + MCHP_DRM_CTRL_REG);

	if (dpsub->base_overlay)
		writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base_overlay + MCHP_DRM_CTRL_REG);

	if (dpsub->base_cursor)
		writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base_cursor + MCHP_DRM_CTRL_REG);
}

/* -----------------------------------------------------------------------------
 * DRM/KMS Driver
 */

DEFINE_DRM_GEM_DMA_FOPS(mpfs_dpsub_drm_fops);

static const struct drm_driver mpfs_dpsub_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(mpfs_dpsub_dumb_create),
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops				= &mpfs_dpsub_drm_fops,
	.name				= "mpfs-dpsub",
	.desc				= "Microchip Display Subsystem Driver",
	.major				= 1,
	.minor				= 0,
};

static int mpfs_dpsub_probe(struct platform_device *pdev)
{
	struct mpfs_dpsub *dpsub;
	struct drm_device *drm;
	struct drm_connector *connector;
	int ret;

	dpsub = devm_drm_dev_alloc(&pdev->dev, &mpfs_dpsub_drm_driver,
				struct mpfs_dpsub, drm);
	if (IS_ERR(dpsub))
		return dev_err_probe(&pdev->dev, PTR_ERR(dpsub), "could not alloc mem\n");

	drm = &dpsub->drm;

	dpsub->dev = &pdev->dev;
	platform_set_drvdata(pdev, dpsub);

	ret = dma_set_mask(dpsub->dev, DMA_BIT_MASK(MPFS_DISP_MAX_DMA_BIT));
	if (ret)
		return ret;

	of_reserved_mem_device_init(&pdev->dev);

	dpsub->base = devm_platform_ioremap_resource_byname(pdev, "primary");
	if (IS_ERR(dpsub->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(dpsub->base),
				     "could not get mem resource\n");

	dpsub->base_dp = devm_platform_ioremap_resource_byname(pdev, "display-controller");
	if (IS_ERR(dpsub->base_dp))
		return dev_err_probe(&pdev->dev, PTR_ERR(dpsub->base_dp),
				     "could not get mem resource display controller\n");

	dpsub->base_overlay = devm_platform_ioremap_resource_byname(pdev, "overlay");
	if (IS_ERR(dpsub->base_overlay)) {
		dev_info(&pdev->dev, "could not get mem resource for overlay plane\n");
		dpsub->base_overlay = NULL;
	}

	dpsub->base_cursor = devm_platform_ioremap_resource_byname(pdev, "cursor");
	if (IS_ERR(dpsub->base_cursor)) {
		dev_info(&pdev->dev, "could not get mem resource for cursor plane\n");
		dpsub->base_cursor = NULL;
	}

	dpsub->irq = platform_get_irq(pdev, 0);
	if (dpsub->irq < 0) {
		ret = dpsub->irq;
		return ret;
	}

	of_get_display_timing(dpsub->dev->of_node, "panel-timing", &dpsub->timing);

	mpfs_dpsub_init(dpsub);

	ret = drmm_mode_config_init(drm);
	if (ret < 0)
		return ret;

	drm->mode_config.funcs = &mpfs_dpsub_mode_config_funcs;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = MPFS_DISP_MAX_WIDTH;
	drm->mode_config.max_height = MPFS_DISP_MAX_HEIGHT;
	dpsub->dma_align = DMAENGINE_ALIGN_4_BYTES;

	mpfs_kms_init(dpsub);

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;

	drm_kms_helper_poll_init(drm);

	/* Connector */
	connector = &dpsub->connector;
	ret = drm_connector_init(drm, connector, &mpfs_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		goto error_ret;

	drm_connector_helper_add(connector, &mpfs_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       MPFS_DISP_DEF_WIDTH, MPFS_DISP_DEF_HEIGHT);

	ret = drm_connector_attach_encoder(connector, &dpsub->encoder);
	if (ret)
		goto error_ret;

	drm_mode_config_reset(drm);

	ret = devm_request_threaded_irq(dpsub->dev, dpsub->irq, mpfs_dp_irq_handler,
					NULL, IRQF_NO_SUSPEND,
					dev_name(dpsub->dev), dpsub);
	if (ret < 0)
		goto error_ret;

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto error_ret;

	/* Initialize fbdev generic emulation. */
	drm_client_setup_with_fourcc(drm, DRM_FORMAT_XRGB8888);

	return 0;

error_ret:
	drm_kms_helper_poll_fini(drm);

	return ret;
}

static void mpfs_dpsub_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_dpsub *dpsub = platform_get_drvdata(pdev);
	struct drm_device *drm = &dpsub->drm;

	of_reserved_mem_device_release(dev);
	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
}

static const struct of_device_id mpfs_dpsub_of_match[] = {
	{ .compatible = "microchip,mpfs-dpsub-2.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, mpfs_dpsub_of_match);

static struct platform_driver mpfs_dpsub_driver = {
	.probe			= mpfs_dpsub_probe,
	.remove			= mpfs_dpsub_remove,
	.driver			= {
		.name		= "mpfs-dpsub",
		.of_match_table	= mpfs_dpsub_of_match,
	},
};

drm_module_platform_driver(mpfs_dpsub_driver);

MODULE_AUTHOR("Microchip Technology Inc. and its subsidiaries");
MODULE_DESCRIPTION("Microchip Display Subsystem Driver");
MODULE_LICENSE("GPL");
