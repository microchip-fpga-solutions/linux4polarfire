/*
 * sam9n12ek_wm8904 - SoC audio for AT91SAM9N12 based boards
 *                    which use WM8904 as codec.
 *
 * Copyright (C) 2011 Atmel
 *
 * Author: Hong Xu <hong.xu@atmel.com>
 * Based on sam9g20_wm8731.c by Sedji Gaouaou
 *
 */
#define DEBUG   1

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>

#include "../codecs/wm8904.h"
#include "atmel_ssc_dai.h"

#define SAM9N12_WM8904_USE_FLL

#ifdef SAM9N12_WM8904_USE_FLL
#define MCLK_RATE 32768
#else
#define MCLK_RATE 12500000
#endif

static struct clk *mclk;

static int at91sam9n12_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		pr_err("%s - Failed to set CODEC DAI format.", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		pr_err("%s - Failed to set CPU DAI format.", __func__);
		return ret;
	}

#ifdef SAM9N12_WM8904_USE_FLL
	ret = snd_soc_dai_set_pll(codec_dai, WM8904_FLL_MCLK, WM8904_FLL_MCLK,
		32768, params_rate(params) * 256);
	if (ret < 0) {
		pr_err("%s - Failed to set CODEC PLL.", __func__);
		return ret;
	}

	/*ret = snd_soc_dai_set_sysclk(codec_dai, WM8904_CLK_MCLK,*/
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8904_CLK_FLL,
		12000000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("%s - Failed to set WM8904 SYSCLK\n", __func__);
		return ret;
	}
#else
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8904_CLK_MCLK,
		MCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("%s - Failed to set WM8904 SYSCLK\n", __func__);
		return ret;
	}
#endif

	return 0;
}

static struct snd_soc_ops at91sam9n12_soc_ops = {
	.hw_params = at91sam9n12_hw_params,
};

static int at91sam9n12_wm8904_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	pr_debug("ASoC: at91sam9n12_wm8904_init() called\n");

	snd_soc_dapm_nc_pin(dapm, "IN1R");
	snd_soc_dapm_nc_pin(dapm, "IN3L");
	snd_soc_dapm_nc_pin(dapm, "IN3R");
	
	snd_soc_dapm_nc_pin(dapm, "LINEOUTL");
	snd_soc_dapm_nc_pin(dapm, "LINEOUTR");

	snd_soc_dapm_enable_pin(dapm, "HPOUTL");
	snd_soc_dapm_enable_pin(dapm, "HPOUTR");
	snd_soc_dapm_enable_pin(dapm, "IN1L");
	snd_soc_dapm_enable_pin(dapm, "IN2L");
	snd_soc_dapm_enable_pin(dapm, "IN2R");

	snd_soc_dapm_sync(dapm);

	return 0;
}

int at91sam9n12_snd_suspend_pre(struct snd_soc_card *card)
{
	clk_disable(mclk);
	return 0;
}

int at91sam9n12_snd_resume_pre(struct snd_soc_card *card)
{
	clk_enable(mclk);
	return 0;
}

static struct snd_soc_dai_link at91sam9n12_dai = {
	.name = "WM8904",
	.stream_name = "WM8904 PCM",
	.cpu_dai_name = "atmel-ssc-dai.0",
	.codec_dai_name = "wm8904-hifi",
	.init = at91sam9n12_wm8904_init,
	.platform_name = "atmel-pcm-audio",
	.codec_name = "wm8904-codec.0-001a",
	.ops = &at91sam9n12_soc_ops,
};

static struct snd_soc_card snd_soc_at91sam9n12 = {
	.name = "WM8904 @ AT91SAM9N12",
	.dai_link = &at91sam9n12_dai,
	.num_links = 1,
	.suspend_pre = at91sam9n12_snd_suspend_pre,
	.resume_pre = at91sam9n12_snd_resume_pre,
};

static struct platform_device *at91sam9n12_snd_device;

static int __init at91sam9n12_init(void)
{
	int ret;
	struct clk *clk_src;

	if (!cpu_is_at91sam9n12())
		return -ENODEV;

	ret = atmel_ssc_set_audio(0);
	if (ret != 0) {
		pr_err("ASoC: Failed to set SSC 0 for audio: %d\n", ret);
		goto err;
	}

	mclk = clk_get(NULL, "pck0");
	if (IS_ERR(mclk)) {
		pr_err("ASoC: Failed to get pck0\n");
		return -ENODEV;
	}
#ifdef SAM9N12_WM8904_USE_FLL
	clk_src = clk_get(NULL, "clk32k");
#else
	clk_src = clk_get(NULL, "plla");
#endif
	if (IS_ERR(clk_src)) {
		pr_err("ASoC: Failed to get pck0\n");
		return -ENODEV;
	}
	ret = clk_set_parent(mclk, clk_src);
	clk_put(clk_src);
	if (ret != 0) {
		pr_err("ASoC: Failed to set MCLK parent\n");
		return -ENODEV;
	}
	
	pr_info("ASoC: Setting pck0 to %dHz\n", MCLK_RATE);

	clk_set_rate(mclk, MCLK_RATE);
	clk_enable(mclk);

	at91sam9n12_snd_device = platform_device_alloc("soc-audio", -1);
	if (!at91sam9n12_snd_device) {
		pr_err("ASoC: Platform device allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}

	platform_set_drvdata(at91sam9n12_snd_device,
			&snd_soc_at91sam9n12);

	ret = platform_device_add(at91sam9n12_snd_device);
	if (ret) {
		pr_err("ASoC: Platform device allocation failed\n");
		goto err_device_add;
	}

	pr_info("ASoC: at91sam9n12_init ok\n");

	return ret;

err_device_add:
	platform_device_put(at91sam9n12_snd_device);
err:
	return ret;
}

static void __exit at91sam9n12_exit(void)
{
	platform_device_unregister(at91sam9n12_snd_device);
	at91sam9n12_snd_device = NULL;
}

module_init(at91sam9n12_init);
module_exit(at91sam9n12_exit);

/* Module information */
MODULE_AUTHOR("Hong Xu <hong.xu@atmel.com>");
MODULE_DESCRIPTION("ALSA SoC machine driver for AT91SAM9N12 - WM8904");
MODULE_LICENSE("GPL");
