/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip Display Subsystem Driver
 *
 * Copyright (C) 2025-2026 Microchip Technology Inc. and its subsidiaries
 *
 * Authors:
 * - Shravan Chippa <shravan.chippa@microchip.com>
 */

#ifndef _MPFS_DPSUB_H_
#define _MPFS_DPSUB_H_

#define MPFS_DISP_MAX_WIDTH		8192
#define MPFS_DISP_MAX_HEIGHT		8192
#define MPFS_DISP_DEF_WIDTH		1280
#define MPFS_DISP_DEF_HEIGHT		720

#define MPFS_DISP_WIDTH_OFFSET		0x08
#define MPFS_DISP_HEIGHT_OFFSET		0x0C

#define MPFS_DISP_MAX_DMA_BIT		64

#define MCHP_DRM_IP_VER			0x00
#define MCHP_DRM_CTRL_REG		0x04
#define MCHP_DRM_CORE_ENABLE		BIT(0)
#define MCHP_DRM_CORE_RESET		BIT(1)
#define MCHP_DRM_FLASH_FIFO		BIT(2)

#define MCHP_DRM_GLBL_INT_EN		0x08
#define MCHP_DRM_GLBL_INT_EN_BIT	BIT(0)

#define MCHP_DRM_INT_STATUS		0x0C
#define MCHP_DRM_INT_STATUS_EOF		BIT(0)
#define MCHP_DRM_INT_STATUS_VSYNC	BIT(5)

#define MCHP_DRM_INT_EN			0x10
#define MCHP_DRM_INT_EN_EOF		BIT(0)
#define MCHP_DRM_INT_EN_FIFO_FULL	BIT(1)
#define MCHP_DRM_INT_EN_FIFO_MT		BIT(2)
#define MCHP_DRM_INT_EN_VSYNC		BIT(5)

#define MCHP_DRM_BUFF_ADDR_FIFO_DATA	0x1C

#define MCHP_DRM_SCAN_XY		0x30
#define MCHP_DRM_VRES_HRES		0x34
#define MCHP_DRM_G_ALFA			0x38
#define MCHP_DRM_START_ADDR_OFFSET	0x3C
#define MCHP_DRM_OVERLAY_LINE_GAP	0x40

#define MCHP_DRM_FRAME_START		0x1
#define MCHP_DRM_FRAME_STOP		0x0

#define MAX_RES_FIELD			16
#define RESOLUTION_MASK			0xFFFF

#define G_ALPHA_MAX			0xFF
#define G_ALPHA_MASK			0xFF
#define G_ALPHA_EN			BIT(8)
#define G_ALPHA_DISABLE			0

/**
 * struct mpfs_dpsub - PolarFire SoC Display Subsystem DRM device
 * @drm:            Embedded DRM device instance (must be first for container).
 * @dev:            Backing Linux device (platform device's &struct device).
 * @base:           Mapped register base of the DPSUB top-level block.
 * @base_dp:        Mapped register base for the DP controller sub-block.
 * @base_overlay:   Mapped register base for the overlay plane sub-block.
 * @base_cursor:    Mapped register base for the hardware cursor sub-block.
 * @planes:         Plane objects owned by this device:
 *                  - @planes.primary: primary scanout plane
 *                  - @planes.overlay: overlay plane (if supported)
 *                  - @planes.cursor:  cursor plane (if supported)
 * @mode:           Cached/active DRM display mode currently programmed.
 * @crtc:           Single CRTC driving the output pipeline.
 * @encoder:        DRM encoder representing the DP output encoder.
 * @connector:      DRM connector representing the DP sink connection.
 * @timing:         Parsed display timing (e.g. from DT) used to build modes.
 * @irq:            Interrupt line for the DPSUB hardware.
 * @dma_align:      Required DMA alignment (bytes) for framebuffer/plane buffers.
 */
struct mpfs_dpsub {
	struct drm_device drm;
	struct device *dev;

	void __iomem *base;
	void __iomem *base_dp;
	void __iomem *base_overlay;
	void __iomem *base_cursor;
	struct {
		struct drm_plane	primary;
		struct drm_plane	overlay;
		struct drm_plane	cursor;
	} planes;
	struct drm_display_mode mode;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct display_timing timing;
	int irq;
	unsigned int dma_align;
};

int mpfs_kms_init(struct mpfs_dpsub *dpsub);

static inline struct mpfs_dpsub *to_mpfs_dpsub(struct drm_device *drm)
{
	return container_of(drm, struct mpfs_dpsub, drm);
}

#endif /* _MPFS_DPSUB_H_ */
