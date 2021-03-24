// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic on-chip SRAM allocation driver
 *
 * Copyright (C) 2012 Philipp Zabel, Pengutronix
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/list_sort.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <soc/at91/atmel-secumod.h>

#include "sram.h"

#define SRAM_GRANULARITY	32

struct work_struct	workq_cpu;

static ssize_t sram_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr,
			 char *buf, loff_t pos, size_t count)
{
	struct sram_partition *part;

	part = container_of(attr, struct sram_partition, battr);

	mutex_lock(&part->lock);
	memcpy_fromio(buf, part->base + pos, count);
	mutex_unlock(&part->lock);

	return count;
}

static ssize_t sram_write(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *attr,
			  char *buf, loff_t pos, size_t count)
{
	struct sram_partition *part;

	part = container_of(attr, struct sram_partition, battr);

	mutex_lock(&part->lock);
	memcpy_toio(part->base + pos, buf, count);
	mutex_unlock(&part->lock);

	return count;
}

static int sram_add_pool(struct sram_dev *sram, struct sram_reserve *block,
			 phys_addr_t start, struct sram_partition *part)
{
	int ret;

	part->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
					  NUMA_NO_NODE, block->label);
	if (IS_ERR(part->pool))
		return PTR_ERR(part->pool);

	ret = gen_pool_add_virt(part->pool, (unsigned long)part->base, start,
				block->size, NUMA_NO_NODE);
	if (ret < 0) {
		dev_err(sram->dev, "failed to register subpool: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sram_add_export(struct sram_dev *sram, struct sram_reserve *block,
			   phys_addr_t start, struct sram_partition *part)
{
	sysfs_bin_attr_init(&part->battr);
	part->battr.attr.name = devm_kasprintf(sram->dev, GFP_KERNEL,
					       "%llx.sram",
					       (unsigned long long)start);
	if (!part->battr.attr.name)
		return -ENOMEM;

	part->battr.attr.mode = S_IRUSR | S_IWUSR;
	part->battr.read = sram_read;
	part->battr.write = sram_write;
	part->battr.size = block->size;

	return device_create_bin_file(sram->dev, &part->battr);
}

static int sram_add_partition(struct sram_dev *sram, struct sram_reserve *block,
			      phys_addr_t start)
{
	int ret;
	struct sram_partition *part = &sram->partition[sram->partitions];

	mutex_init(&part->lock);
	part->base = sram->virt_base + block->start;

	if (block->pool) {
		ret = sram_add_pool(sram, block, start, part);
		if (ret)
			return ret;
	}
	if (block->export) {
		ret = sram_add_export(sram, block, start, part);
		if (ret)
			return ret;
	}
	if (block->protect_exec) {
		ret = sram_check_protect_exec(sram, block, part);
		if (ret)
			return ret;

		ret = sram_add_pool(sram, block, start, part);
		if (ret)
			return ret;

		sram_add_protect_exec(part);
	}

	sram->partitions++;

	return 0;
}

static void sram_free_partitions(struct sram_dev *sram)
{
	struct sram_partition *part;

	if (!sram->partitions)
		return;

	part = &sram->partition[sram->partitions - 1];
	for (; sram->partitions; sram->partitions--, part--) {
		if (part->battr.size)
			device_remove_bin_file(sram->dev, &part->battr);

		if (part->pool &&
		    gen_pool_avail(part->pool) < gen_pool_size(part->pool))
			dev_err(sram->dev, "removed pool while SRAM allocated\n");
	}
}

static int sram_reserve_cmp(void *priv, struct list_head *a,
					struct list_head *b)
{
	struct sram_reserve *ra = list_entry(a, struct sram_reserve, list);
	struct sram_reserve *rb = list_entry(b, struct sram_reserve, list);

	return ra->start - rb->start;
}

static int sram_reserve_regions(struct sram_dev *sram, struct resource *res)
{
	struct device_node *np = sram->dev->of_node, *child;
	unsigned long size, cur_start, cur_size;
	struct sram_reserve *rblocks, *block;
	struct list_head reserve_list;
	unsigned int nblocks, exports = 0;
	const char *label;
	int ret = 0;

	INIT_LIST_HEAD(&reserve_list);

	size = resource_size(res);

	/*
	 * We need an additional block to mark the end of the memory region
	 * after the reserved blocks from the dt are processed.
	 */
	nblocks = (np) ? of_get_available_child_count(np) + 1 : 1;
	rblocks = kcalloc(nblocks, sizeof(*rblocks), GFP_KERNEL);
	if (!rblocks)
		return -ENOMEM;

	block = &rblocks[0];
	for_each_available_child_of_node(np, child) {
		struct resource child_res;

		ret = of_address_to_resource(child, 0, &child_res);
		if (ret < 0) {
			dev_err(sram->dev,
				"could not get address for node %pOF\n",
				child);
			goto err_chunks;
		}

		if (child_res.start < res->start || child_res.end > res->end) {
			dev_err(sram->dev,
				"reserved block %pOF outside the sram area\n",
				child);
			ret = -EINVAL;
			goto err_chunks;
		}

		block->start = child_res.start - res->start;
		block->size = resource_size(&child_res);
		list_add_tail(&block->list, &reserve_list);

		if (of_find_property(child, "export", NULL))
			block->export = true;

		if (of_find_property(child, "pool", NULL))
			block->pool = true;

		if (of_find_property(child, "protect-exec", NULL))
			block->protect_exec = true;

		if ((block->export || block->pool || block->protect_exec) &&
		    block->size) {
			exports++;

			label = NULL;
			ret = of_property_read_string(child, "label", &label);
			if (ret && ret != -EINVAL) {
				dev_err(sram->dev,
					"%pOF has invalid label name\n",
					child);
				goto err_chunks;
			}
			if (!label)
				label = child->name;

			block->label = devm_kstrdup(sram->dev,
						    label, GFP_KERNEL);
			if (!block->label) {
				ret = -ENOMEM;
				goto err_chunks;
			}

			dev_dbg(sram->dev, "found %sblock '%s' 0x%x-0x%x\n",
				block->export ? "exported " : "", block->label,
				block->start, block->start + block->size);
		} else {
			dev_dbg(sram->dev, "found reserved block 0x%x-0x%x\n",
				block->start, block->start + block->size);
		}

		block++;
	}
	child = NULL;

	/* the last chunk marks the end of the region */
	rblocks[nblocks - 1].start = size;
	rblocks[nblocks - 1].size = 0;
	list_add_tail(&rblocks[nblocks - 1].list, &reserve_list);

	list_sort(NULL, &reserve_list, sram_reserve_cmp);

	if (exports) {
		sram->partition = devm_kcalloc(sram->dev,
				       exports, sizeof(*sram->partition),
				       GFP_KERNEL);
		if (!sram->partition) {
			ret = -ENOMEM;
			goto err_chunks;
		}
	}

	cur_start = 0;
	list_for_each_entry(block, &reserve_list, list) {
dev_dbg(sram->dev, "un entry !\n");
		/* can only happen if sections overlap */
		if (block->start < cur_start) {
			dev_err(sram->dev,
				"block at 0x%x starts after current offset 0x%lx\n",
				block->start, cur_start);
			ret = -EINVAL;
			sram_free_partitions(sram);
			goto err_chunks;
		}

		if ((block->export || block->pool || block->protect_exec) &&
		    block->size) {
			ret = sram_add_partition(sram, block,
						 res->start + block->start);
			if (ret) {
dev_dbg(sram->dev, "error add partiotion %d\n", ret);
				sram_free_partitions(sram);
				goto err_chunks;
			}
		}

		/* current start is in a reserved block, so continue after it */
		if (block->start == cur_start) {
dev_dbg(sram->dev, "dubios 1\n");
			cur_start = block->start + block->size;
			continue;
		}

		/*
		 * allocate the space between the current starting
		 * address and the following reserved block, or the
		 * end of the region.
		 */
		cur_size = block->start - cur_start;

		dev_dbg(sram->dev, "adding chunk 0x%lx-0x%lx\n",
			cur_start, cur_start + cur_size);

		ret = gen_pool_add_virt(sram->pool,
				(unsigned long)sram->virt_base + cur_start,
				res->start + cur_start, cur_size, -1);
		if (ret < 0) {
			sram_free_partitions(sram);
			goto err_chunks;
		}

		/* next allocation after this reserved block */
		cur_start = block->start + block->size;
	}

err_chunks:
//dev_dbg(sram->dev, "error !!\n");
	of_node_put(child);
	kfree(rblocks);

	return ret;
}

static int atmel_securam_wait(void)
{
	struct regmap *regmap;
	u32 val;

	regmap = syscon_regmap_lookup_by_compatible("atmel,sama5d2-secumod");
	if (IS_ERR(regmap))
		return -ENODEV;

	return regmap_read_poll_timeout(regmap, AT91_SECUMOD_RAMRDY, val,
					val & AT91_SECUMOD_RAMRDY_READY,
					10000, 500000);
}

static void *sram_mem, *sram_mem2;

static const struct of_device_id sram_dt_ids[] = {
	{ .compatible = "mmio-sram" },
	{ .compatible = "atmel,sama5d2-securam", .data = atmel_securam_wait },
	{}
};

enum { FOO_SIZE_MAX = 4 };
static int foo_size;
static char foo_tmp[FOO_SIZE_MAX];
static char pattern_tmp[5];
static int pattern_size;
static u32 pattern;

static u32 noverify = 0;

static struct kobject *kobj;
static ssize_t foo_show(struct kobject *kobj, struct kobj_attribute *attr,
        char *buff)
{
	strncpy(buff, foo_tmp, foo_size);
	return foo_size;
}
static ssize_t pattern_show(struct kobject *kobj, struct kobj_attribute *attr,
        char *buff)
{
	strncpy(buff, pattern_tmp, pattern_size);
	return pattern_size;
}
static ssize_t noverify_show(struct kobject *kobj, struct kobj_attribute *attr,
        char *buff)
{
	char tmp[2];
	tmp[0] = '0';
	tmp[1] = 0;
	tmp[0] += noverify;
	strncpy(buff, tmp, 2);
	return 2;
}

static int cpu_work = 0;
static void * DDR;

static void workq_handler_cpu(struct work_struct *workq)
{
	if(cpu_work) {
		u32 *sram1_u32 = (u32*) sram_mem;
		u32 *sram2_u32 = (u32*) sram_mem2;
		int i, err = 0;

		/* wiping SRAM1 and SRAM2*/
		memset(sram_mem, 0, 8*1024);
		memset(sram_mem2, 0, 8*1024);
		/* filling sram1 with pattern */
		for (i = 0; i < 2*1024;i++)
			*sram1_u32++ = pattern;

		memcpy(DDR, sram_mem, 8*1024);
		memcpy(sram_mem2, DDR, 8*1024);

		/* check pattern presence in second buffer */
		if (!noverify)
		for (i = 0; i < 2*1024;i++)
			if (*sram2_u32++ != pattern)
				err = 1;

		schedule_work(&workq_cpu);

		if (err)
			pr_err("Pattern mismatch in SRAM buffers on SRAM->DDR->SRAM copy\n");
	} else pr_info("stopping copy SRAM->DDR, DDR->SRAM\n");
}

static ssize_t pattern_store(struct  kobject *kobj, struct kobj_attribute *attr,
        const char *buff, size_t count)
{
	unsigned long val;

	pattern_size = min(count, (size_t)11);
	strncpy(pattern_tmp, buff, pattern_size);
	pattern_tmp[pattern_size] = 0;

	if (kstrtoul(pattern_tmp, 16, &val))
		return 0;
	pattern =  val;
	return count;
}

static ssize_t foo_store(struct  kobject *kobj, struct kobj_attribute *attr,
        const char *buff, size_t count)
{
	unsigned long val;

	foo_size = min(count, (size_t)FOO_SIZE_MAX);
	strncpy(foo_tmp, buff, foo_size);
	if (kstrtoul(foo_tmp, 10, &val))
		return 0;

	if (val == 1) {
		cpu_work = 1;

	pr_info("starting %s copy SRAM->DDR, DDR->SRAM with pattern 0x%x\n",
		 noverify ? "non-verified": "verified", pattern);

		schedule_work(&workq_cpu);
	} else if (val == 0) {
		cpu_work = 0;
	}

	return count;
}


static ssize_t noverify_store(struct  kobject *kobj, struct kobj_attribute *attr,
        const char *buff, size_t count)
{
	unsigned long val;
	char tmp[2];
	strncpy(tmp, buff, 1);
	tmp[1] = 0;

	if (kstrtoul(tmp, 10, &val))
		return 0;
	noverify = val;

	return count;
}

static struct kobj_attribute foo_attribute =
    __ATTR(cpu, S_IRUGO | S_IWUSR, foo_show, foo_store);

static struct kobj_attribute pattern_attribute =
    __ATTR(pattern, S_IRUGO | S_IWUSR, pattern_show, pattern_store);

static struct kobj_attribute noverify_attribute =
    __ATTR(noverify, S_IRUGO | S_IWUSR, noverify_show, noverify_store);

static struct attribute *attrs[] = {
    &foo_attribute.attr, &pattern_attribute.attr, &noverify_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int sram_probe(struct platform_device *pdev)
{
	struct sram_dev *sram;
	int ret;
	int (*init_func)(void);

	sram = devm_kzalloc(&pdev->dev, sizeof(*sram), GFP_KERNEL);
	if (!sram)
		return -ENOMEM;

	sram->dev = &pdev->dev;

	if (of_property_read_bool(pdev->dev.of_node, "no-memory-wc"))
		sram->virt_base = devm_platform_ioremap_resource(pdev, 0);
	else
		sram->virt_base = devm_platform_ioremap_resource_wc(pdev, 0);
	if (IS_ERR(sram->virt_base)) {
		dev_err(&pdev->dev, "could not map SRAM registers\n");
		return PTR_ERR(sram->virt_base);
	}

	sram->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
					  NUMA_NO_NODE, NULL);
	if (IS_ERR(sram->pool))
		return PTR_ERR(sram->pool);

	sram->clk = devm_clk_get(sram->dev, NULL);
	if (IS_ERR(sram->clk))
		sram->clk = NULL;
	else
		clk_prepare_enable(sram->clk);

	ret = sram_reserve_regions(sram,
			platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (ret)
		goto err_disable_clk;

	platform_set_drvdata(pdev, sram);

	init_func = of_device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func();
		if (ret)
			goto err_free_partitions;
	}

	dev_dbg(sram->dev, "SRAM pool: %zu KiB @ 0x%p\n",
		gen_pool_size(sram->pool) / 1024, sram->virt_base);

	INIT_WORK(&workq_cpu, workq_handler_cpu);

	memcpy (foo_tmp, "0", sizeof ("0"));
	memcpy (pattern_tmp, "0x0", sizeof ("0"));
	pattern_size = 4;
	pattern = 0;
	kobj = kobject_create_and_add("sram_ops", kernel_kobj);
	if (!kobj)
		return -ENOMEM;
	ret = sysfs_create_group(kobj, &attr_group);
	if (ret)
		kobject_put(kobj);

	/* allocating 8 KB */
	sram_mem = ioremap(0x100000, 8 * 1024);
	sram_mem2 = ioremap(0x110000, 8 * 1024);

	DDR = devm_kzalloc(&pdev->dev, 8*1024, GFP_KERNEL);
	if (IS_ERR_OR_NULL(sram_mem) || IS_ERR_OR_NULL(sram_mem2))
		dev_err(sram->dev,"sram mem allocation failure !\n");


	dev_dbg(sram->dev, "allocated @ %x\n", virt_to_phys(sram_mem));

	return 0;
err_free_partitions:
	sram_free_partitions(sram);
err_disable_clk:
	if (sram->clk)
		clk_disable_unprepare(sram->clk);

	return ret;
}

static int sram_remove(struct platform_device *pdev)
{
	struct sram_dev *sram = platform_get_drvdata(pdev);

	sram_free_partitions(sram);

	if (gen_pool_avail(sram->pool) < gen_pool_size(sram->pool))
		dev_err(sram->dev, "removed while SRAM allocated\n");

	if (sram->clk)
		clk_disable_unprepare(sram->clk);
	kobject_put(kobj);
	return 0;
}

static struct platform_driver sram_driver = {
	.driver = {
		.name = "sram",
		.of_match_table = sram_dt_ids,
	},
	.probe = sram_probe,
	.remove = sram_remove,
};

static int __init sram_init(void)
{
	return platform_driver_register(&sram_driver);
}

postcore_initcall(sram_init);
