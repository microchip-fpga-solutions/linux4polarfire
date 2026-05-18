// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Display Subsystem Driver
 *
 * Copyright (C) 2025-2026 Microchip Technology Inc. and its subsidiaries
 *
 * Authors:
 * - Shravan Chippa <shravan.chippa@microchip.com>
 */

#include <linux/io.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "mpfs_dpsub.h"

static int mpfs_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	return drm_atomic_add_affected_planes(state, crtc);
}

static void mpfs_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_pending_vblank_event *event;

	event = crtc->state->event;
	crtc->state->event = NULL;

	if (!event)
		return;

	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void mpfs_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void mpfs_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	drm_crtc_vblank_off(crtc);
}

static int mpfs_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(crtc->dev);

	/* Enable SOF (Start Of Frame) interrupt for vblank counting */
	writel_relaxed(MCHP_DRM_GLBL_INT_EN_BIT, dpsub->base + MCHP_DRM_GLBL_INT_EN);
	writel_relaxed(MCHP_DRM_INT_EN_VSYNC, dpsub->base + MCHP_DRM_INT_EN);

	return 0;
}

static void mpfs_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(crtc->dev);
	u32 irq_vsync;

	writel_relaxed(0x00, dpsub->base + MCHP_DRM_GLBL_INT_EN);

	irq_vsync = readl_relaxed(dpsub->base + MCHP_DRM_INT_EN);
	irq_vsync = irq_vsync & ~(MCHP_DRM_INT_EN_VSYNC);
	writel_relaxed(irq_vsync, dpsub->base + MCHP_DRM_INT_EN);
}

static const struct drm_crtc_helper_funcs mpfs_crtc_helper_funcs = {
	.atomic_check = mpfs_crtc_atomic_check,
	.atomic_flush = mpfs_crtc_atomic_flush,
	.atomic_enable = mpfs_crtc_atomic_enable,
	.atomic_disable = mpfs_crtc_atomic_disable,
};

static const struct drm_crtc_funcs mpfs_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = mpfs_crtc_enable_vblank,
	.disable_vblank = mpfs_crtc_disable_vblank,
};

/* -----------------------------------------------------------------------------
 * Encoder
 */

static const struct drm_encoder_funcs mpfs_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

/* -----------------------------------------------------------------------------
 * Planes
 */

static int mpfs_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct mpfs_dpsub *mpfs = to_mpfs_dpsub(plane->dev);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state,
						   &mpfs->crtc);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static int mpfs_plane_secondary_atomic_check(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct mpfs_dpsub *mpfs = to_mpfs_dpsub(plane->dev);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state, &mpfs->crtc);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void mpfs_plane_primary_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state, plane);
	dma_addr_t dma_addr;

	if (!new_pstate->fb)
		return;

	dma_addr = drm_fb_dma_get_gem_addr(new_pstate->fb, new_pstate, 0);

	writel_relaxed(dma_addr >> 6, dpsub->base + MCHP_DRM_BUFF_ADDR_FIFO_DATA);
}

static u32 mpfs_plane_fb_dma_get_addr_offset(struct drm_framebuffer *fb, struct drm_plane_state *state,
				      unsigned int plane)
{
	u32 addr_offset;
	u8 h_div = 1, v_div = 1;
	u32 block_w = drm_format_info_block_width(fb->format, plane);
	u32 block_h = drm_format_info_block_height(fb->format, plane);
	u32 block_size = fb->format->char_per_block[plane];
	u32 sample_x;
	u32 sample_y;
	u32 block_start_y;
	u32 num_hblocks;

	if (plane > 0) {
		h_div = fb->format->hsub;
		v_div = fb->format->vsub;
	}

	sample_x = (state->src_x >> MAX_RES_FIELD) / h_div;
	sample_y = (state->src_y >> MAX_RES_FIELD) / v_div;
	block_start_y = (sample_y / block_h) * block_h;
	num_hblocks = sample_x / block_w;

	addr_offset = fb->pitches[plane] * block_start_y;
	addr_offset += block_size * num_hblocks;

	return addr_offset;
}

static void mpfs_plane_secondary_atomic_update(struct drm_plane *plane,
					       struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state, plane);
	struct drm_gem_dma_object *obj;
	void __iomem *secondary_base;
	dma_addr_t dma_addr = 0;
	u32 block_w, block_h, sample_x, sample_y, scan_xy, vres_hres;
	u32 block_size, line_gap, addr_offset;

	if (!new_pstate->fb)
		return;

	block_w = (new_pstate->src_w >> MAX_RES_FIELD) / new_pstate->fb->format->hsub;
	block_h = (new_pstate->src_h >> MAX_RES_FIELD) / new_pstate->fb->format->vsub;
	sample_x = (new_pstate->crtc_x) / new_pstate->fb->format->hsub;
	sample_y = (new_pstate->crtc_y) / new_pstate->fb->format->vsub;
	scan_xy = sample_x & RESOLUTION_MASK;
	vres_hres = block_w & RESOLUTION_MASK;
	block_size = new_pstate->fb->format->char_per_block[0];
	line_gap = new_pstate->fb->pitches[0] - (block_w * block_size);
	addr_offset = mpfs_plane_fb_dma_get_addr_offset(new_pstate->fb, new_pstate, 0);

	scan_xy |= (sample_y << MAX_RES_FIELD);
	vres_hres |= (block_h << MAX_RES_FIELD);

	obj = drm_fb_dma_get_gem_obj(new_pstate->fb, 0);
	if (!obj)
		return;

	dma_addr = obj->dma_addr + new_pstate->fb->offsets[0];

	if (plane->type == DRM_PLANE_TYPE_OVERLAY)
		secondary_base = dpsub->base_overlay;
	else
		secondary_base = dpsub->base_cursor;

	writel_relaxed(scan_xy, secondary_base + MCHP_DRM_SCAN_XY);
	writel_relaxed(vres_hres, secondary_base + MCHP_DRM_VRES_HRES);
	writel_relaxed(dma_addr >> 6, secondary_base + MCHP_DRM_BUFF_ADDR_FIFO_DATA);
	writel_relaxed(line_gap, secondary_base + MCHP_DRM_OVERLAY_LINE_GAP);
	writel_relaxed(addr_offset, secondary_base + MCHP_DRM_START_ADDR_OFFSET);
}

static void mpfs_plane_overlay_atomic_disable(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);

	writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base_overlay + MCHP_DRM_CTRL_REG);
}

static void mpfs_plane_overlay_atomic_enable(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state, plane);
	int g_alpha;

	writel_relaxed(MCHP_DRM_FRAME_START, dpsub->base_overlay + MCHP_DRM_CTRL_REG);

	/* This code will support only 8bit alpha with lower byte */
	if (new_pstate->alpha != 0 && new_pstate->alpha != G_ALPHA_MAX)
		g_alpha = G_ALPHA_EN | (new_pstate->alpha & G_ALPHA_MASK);
	else
		g_alpha = G_ALPHA_DISABLE;

	writel_relaxed(g_alpha, dpsub->base_overlay + MCHP_DRM_G_ALFA);
}

static void mpfs_plane_cursor_atomic_disable(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);

	writel_relaxed(MCHP_DRM_CORE_RESET, dpsub->base_cursor + MCHP_DRM_CTRL_REG);
}

static void mpfs_plane_cursor_atomic_enable(struct drm_plane *plane,
					    struct drm_atomic_state *state)
{
	struct mpfs_dpsub *dpsub = to_mpfs_dpsub(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state, plane);
	int g_alpha;

	writel_relaxed(MCHP_DRM_FRAME_START, dpsub->base_cursor + MCHP_DRM_CTRL_REG);

	/* This code will support only 8bit alpha with lower byte */
	if (new_pstate->alpha != 0 && new_pstate->alpha != G_ALPHA_MAX)
		g_alpha = G_ALPHA_EN | (new_pstate->alpha & G_ALPHA_MASK);
	else
		g_alpha = G_ALPHA_DISABLE;

	writel_relaxed(g_alpha, dpsub->base_cursor + MCHP_DRM_G_ALFA);
}

static bool mpfs_format_mod_supported(struct drm_plane *plane, u32 format, u64 modifier)
{
	return modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_helper_funcs mpfs_plane_primary_helper_funcs = {
	.atomic_check = mpfs_plane_atomic_check,
	.atomic_update = mpfs_plane_primary_atomic_update,
};

static const struct drm_plane_helper_funcs mpfs_plane_overlay_helper_funcs = {
	.atomic_check = mpfs_plane_secondary_atomic_check,
	.atomic_update = mpfs_plane_secondary_atomic_update,
	.atomic_disable = mpfs_plane_overlay_atomic_disable,
	.atomic_enable = mpfs_plane_overlay_atomic_enable,
};

static const struct drm_plane_helper_funcs mpfs_plane_cursor_helper_funcs = {
	.atomic_check = mpfs_plane_secondary_atomic_check,
	.atomic_update = mpfs_plane_secondary_atomic_update,
	.atomic_disable = mpfs_plane_cursor_atomic_disable,
	.atomic_enable = mpfs_plane_cursor_atomic_enable,
};

static const struct drm_plane_funcs mpfs_plane_funcs = {
	.format_mod_supported	= mpfs_format_mod_supported,
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const u32 mpfs_primary_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
};

static const u32 mpfs_overlay_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
};

static const u32 mpfs_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

int mpfs_kms_init(struct mpfs_dpsub *dpsub)
{
	struct drm_encoder *encoder = &dpsub->encoder;
	struct drm_crtc *crtc = &dpsub->crtc;
	int ret;

	ret = drm_universal_plane_init(&dpsub->drm, &dpsub->planes.primary, 0,
				       &mpfs_plane_funcs,
				       mpfs_primary_plane_formats,
				       ARRAY_SIZE(mpfs_primary_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		dev_err(dpsub->dev, "drm_universal_plane_init failed\n");
		return ret;
	}

	drm_plane_helper_add(&dpsub->planes.primary,
			     &mpfs_plane_primary_helper_funcs);

	if (dpsub->base_overlay) {
		ret = drm_universal_plane_init(&dpsub->drm,
					       &dpsub->planes.overlay, 0,
					       &mpfs_plane_funcs,
					       mpfs_overlay_plane_formats,
					       ARRAY_SIZE(mpfs_overlay_plane_formats),
					       NULL, DRM_PLANE_TYPE_OVERLAY, NULL);
		if (ret) {
			dev_err(dpsub->dev, "drm_universal_plane_init overlay\n");
			return ret;
		}

		drm_plane_helper_add(&dpsub->planes.overlay,
				     &mpfs_plane_overlay_helper_funcs);
	}

	if (dpsub->base_cursor) {
		ret = drm_universal_plane_init(&dpsub->drm,
					       &dpsub->planes.cursor, 0,
					       &mpfs_plane_funcs,
					       mpfs_cursor_plane_formats,
					       ARRAY_SIZE(mpfs_cursor_plane_formats),
					       NULL, DRM_PLANE_TYPE_CURSOR, NULL);
		if (ret) {
			dev_err(dpsub->dev, "drm_universal_plane_init cursor\n");
			return ret;
		}

		drm_plane_helper_add(&dpsub->planes.cursor,
				     &mpfs_plane_cursor_helper_funcs);
	}

	drm_crtc_helper_add(crtc, &mpfs_crtc_helper_funcs);

	if (dpsub->base_cursor) {
		ret = drm_crtc_init_with_planes(&dpsub->drm, crtc,
						&dpsub->planes.primary, &dpsub->planes.cursor,
						&mpfs_crtc_funcs, NULL);
		if (ret) {
			dev_err(dpsub->dev, "drm_crtc_init_with_planes failed\n");
			return ret;
		}
	} else {
		ret = drm_crtc_init_with_planes(&dpsub->drm, crtc,
						&dpsub->planes.primary, NULL,
						&mpfs_crtc_funcs, NULL);
		if (ret) {
			dev_err(dpsub->dev, "drm_crtc_init_with_planes failed\n");
			return ret;
		}
	}

	drm_plane_create_zpos_immutable_property(&dpsub->planes.primary, 0);

	if (dpsub->base_overlay) {
		drm_plane_create_zpos_immutable_property(&dpsub->planes.overlay, 1);
		drm_plane_create_alpha_property(&dpsub->planes.overlay);
		dpsub->planes.overlay.possible_crtcs = drm_crtc_mask(crtc);
	}

	if (dpsub->base_cursor) {
		drm_plane_create_zpos_immutable_property(&dpsub->planes.cursor, 2);
		drm_plane_create_alpha_property(&dpsub->planes.cursor);
		dpsub->planes.cursor.possible_crtcs = drm_crtc_mask(crtc);
	}

	dpsub->planes.primary.possible_crtcs = drm_crtc_mask(crtc);

	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_encoder_init(&dpsub->drm, encoder, &mpfs_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret) {
		dev_err(dpsub->dev, "drm_encoder_init failed\n");
		return ret;
	}

	return 0;
}
