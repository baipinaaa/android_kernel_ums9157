// SPDX-License-Identifier: GPL-2.0
/*
 * sprd_audcp_boot.c - UNISOC Audio CP (AGDSP) boot driver
 *
 * Reconstructed from the vendor binary sprd_audcp_boot.ko shipped in the
 * UMS9157 (T157 / Sunelan Q968) vendor_boot ramdisk
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt mod_unload
 *  modversions aarch64).
 *
 * The driver maps twelve audio-CP control syscons out of the device tree,
 * reserves the AGDSP binary / IRAM / SIPC shared-memory regions through
 * audio_mem.ko and exports a sysfs group which lets the userspace audio HAL
 * push the AGDSP firmware and start/stop the DSP:
 *
 *   /sys/devices/.../sprd_audcp_boot/
 *       ldinfo   (0444)  binary load info blob, 40 bytes
 *       status   (0444)  binary status blob, 3 x u32 == 12 bytes
 *       agdsp    (0200)  firmware upload, 4 KiB chunks
 *       start    (0200)  release the DSP out of reset
 *       stop     (0200)  put the DSP back into reset
 *
 * Both "ldinfo" and "status" hand back raw binary blobs instead of ASCII
 * text; that is what the vendor HAL mmaps, so it is kept byte for byte.
 *
 * Binary layout of the driver private data (devm_kmalloc of 512 bytes,
 * deliberately not zeroed, exactly like the vendor build):
 *
 *   0x000  struct regmap *map[12]
 *   0x060  reserved (never touched)
 *   0x0f0  u32 reg[12]                 syscon offsets, args[0] of each phandle
 *   0x12c  u32 mask[12]                syscon masks,    args[1] of each phandle
 *   0x168  u32 ctrl_num                always 12
 *   0x16c  struct audcp_ldinfo ldinfo  (see below)
 *   0x194  u32 bin_phy                 phys addr of the AGDSP image
 *   0x198  void *bin_virt              cpu mapping of the AGDSP image
 *   0x1a0  u32 bin_chunk               4 KiB chunk index used by ->agdsp
 *   0x1a8  unsigned long bin_written   bytes already uploaded
 *   0x1b0  void *iram_virt             cpu mapping of the AUDCP AON IRAM
 *   0x1b8  unsigned long iram_phy
 *   0x1c0  u32 iram_size
 *   0x1c8  void *smsg_virt             SIPC message buffer
 *   0x1d0  unsigned long smsg_phy
 *   0x1d8  u32 smsg_size
 *   0x1e0  void *smsg_para_virt        SIPC parameter buffer
 *   0x1e8  unsigned long smsg_para_phy
 *   0x1f0  u32 smsg_para_size
 *   0x1f8  u32 boot_addr               (bin_phy + 128) >> 1, boot vector payload
 *   0x1fc  u32 dsp_reboot_mode         "dsp-reboot-mode", 0 == reboot through DDR
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

/* audio memory types, see audio_mem.ko */
#define MEM_AUDCP_DSPBIN	20
#define MEM_IRAM_AUDCP_AON	21

#define AUDCP_CTRL_MAX		12

/* Memory identifiers handed to audio_mem.ko, these are module interfaces of
 * the vendor audio_driver package and are not part of the mainline tree.
 */
extern unsigned int audio_mem_alloc(int type, unsigned int *size);
extern void *audio_mem_vmap(unsigned int phy);
extern void audio_mem_unmap(void *virt);
extern void audio_mem_free(int type, unsigned int phy, unsigned int size);

/* provided by audio_sipc.ko */
extern int aud_get_aud_ipc_smsg_addr(unsigned long *phy, void **virt,
				     unsigned int *size);
extern int aud_get_aud_ipc_smsg_para_addr(unsigned long *phy, void **virt,
					  unsigned int *size);

/**
 * struct audcp_ldinfo - blob returned through the "ldinfo" attribute
 *
 * 40 bytes, copied straight to userspace.  Only @name, @phy_addr and @size are
 * ever written by this driver; the reserved words stay uninitialised just like
 * in the vendor binary because the buffer is allocated with devm_kmalloc().
 */
struct audcp_ldinfo {
	char name[8];		/* 0x00 "agdsp" */
	unsigned int reserved[6];	/* 0x08 never written */
	unsigned int phy_addr;	/* 0x20 physical address of the image */
	unsigned int size;	/* 0x24 image size */
};

struct sprd_audcp_boot_data {
	struct regmap *map[AUDCP_CTRL_MAX];	/* 0x000 */
	unsigned long reserved0[18];		/* 0x060..0x0ef */
	unsigned int reg[AUDCP_CTRL_MAX];	/* 0x0f0 */
	unsigned int reserved1[3];		/* 0x120..0x12b */
	unsigned int mask[AUDCP_CTRL_MAX];	/* 0x12c */
	unsigned int reserved2[3];		/* 0x15c..0x167 */
	unsigned int ctrl_num;			/* 0x168 */
	struct audcp_ldinfo ldinfo;		/* 0x16c */
	unsigned int bin_phy;			/* 0x194 */
	void *bin_virt;				/* 0x198 */
	unsigned int bin_chunk;			/* 0x1a0 */
	unsigned int reserved3;			/* 0x1a4 */
	unsigned long bin_written;		/* 0x1a8 */
	void *iram_virt;			/* 0x1b0 */
	unsigned long iram_phy;			/* 0x1b8 */
	unsigned int iram_size;			/* 0x1c0 */
	void *smsg_virt;			/* 0x1c8 */
	unsigned long smsg_phy;			/* 0x1d0 */
	unsigned int smsg_size;			/* 0x1d8 */
	void *smsg_para_virt;			/* 0x1e0 */
	unsigned long smsg_para_phy;		/* 0x1e8 */
	unsigned int smsg_para_size;		/* 0x1f0 */
	unsigned int boot_addr;			/* 0x1f8 */
	unsigned int dsp_reboot_mode;		/* 0x1fc */
};

/*
 * Order matters: it is both the order of the device tree properties looked up
 * in the probe function and the order of the "audio-cp" syscon list.
 */
static const char *const bootctrl_name[AUDCP_CTRL_MAX] = {
	"sysshutdown",
	"coreshutdown",
	"deepsleep",
	"corereset",
	"sysreset",
	"reset_sel",
	"sysstatus",
	"corestatus",
	"sleepstatus",
	"bootprotect",
	"bootvector",
	"bootaddress_sel",
};

/*
 * Every register access shifts the payload down to the position of the lowest
 * set bit of the mask.  The compiler emitted "rbit/clz" plus "csinv w9, w9, wzr,
 * ne" for that shift, i.e. the source guarded the zero mask case, so keep the
 * very same shape here.
 */
static inline int audcp_shift(unsigned int mask)
{
	return mask ? __ffs(mask) : -1;
}

/* defined below, installed by the probe function */
static const struct attribute_group *sprd_audcp_boot_groups[];

#define AUDCP_CTRL(_idx)	((_idx))
#define AUDCP_SYSSHUTDOWN	AUDCP_CTRL(0)
#define AUDCP_CORESHUTDOWN	AUDCP_CTRL(1)
#define AUDCP_DEEPSLEEP		AUDCP_CTRL(2)
#define AUDCP_CORERESET		AUDCP_CTRL(3)
#define AUDCP_SYSRESET		AUDCP_CTRL(4)
#define AUDCP_RESET_SEL		AUDCP_CTRL(5)
#define AUDCP_SYSSTATUS		AUDCP_CTRL(6)
#define AUDCP_CORESTATUS	AUDCP_CTRL(7)
#define AUDCP_SLEEPSTATUS	AUDCP_CTRL(8)
#define AUDCP_BOOTPROTECT	AUDCP_CTRL(9)
#define AUDCP_BOOTVECTOR	AUDCP_CTRL(10)
#define AUDCP_BOOTADDRESS_SEL	AUDCP_CTRL(11)

static int sprd_audcp_boot_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct sprd_audcp_boot_data *data;
	unsigned int args[2] = { 0, 0 };
	unsigned int size = 0;
	unsigned long smsg_phy = 0;
	void *smsg_virt = NULL;
	unsigned int smsg_size = 0;
	unsigned long smsg_para_phy = 0;
	void *smsg_para_virt = NULL;
	unsigned int smsg_para_size = 0;
	unsigned long iram_phy;
	int reboot_mode = 0;
	int i, ret;

	data = devm_kmalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (of_property_read_u32(np, "dsp-reboot-mode", &reboot_mode)) {
		dev_info(dev, "dsp reboot by DDR!\n");
		reboot_mode = 0;
	} else {
		dev_info(dev, "dsp_reboot_mode = %d!\n", reboot_mode);
	}
	data->dsp_reboot_mode = reboot_mode;

	for (i = 0; i < AUDCP_CTRL_MAX; i++) {
		data->map[i] = syscon_regmap_lookup_by_phandle_args(np,
								    bootctrl_name[i],
								    2, args);
		if (IS_ERR(data->map[i])) {
			dev_err(dev, "failed to map ctrl reg\n");
			return -EINVAL;
		}
		data->reg[i] = args[0];
		data->mask[i] = args[1];
	}
	data->ctrl_num = AUDCP_CTRL_MAX;

	data->bin_phy = audio_mem_alloc(MEM_AUDCP_DSPBIN, &size);
	if (!data->bin_phy) {
		dev_err(dev, "alloc MEM_AUDCP_DSPBIN failed\n");
		return -ENOMEM;
	}
	data->ldinfo.phy_addr = data->bin_phy;
	strcpy(data->ldinfo.name, "agdsp");
	data->boot_addr = (data->bin_phy + 128) >> 1;
	data->ldinfo.size = size;

	data->bin_virt = audio_mem_vmap(data->bin_phy);
	if (!data->bin_virt) {
		dev_err(dev, "memory map for dsp bin failed\n");
		ret = -ENOMEM;
		goto err_free_bin;
	}
	dev_info(dev, "base_addr_phy = %#x, base_addr_virt=%p, bin size =%#x\n",
		 data->bin_phy, data->bin_virt, data->ldinfo.size);

	iram_phy = audio_mem_alloc(MEM_IRAM_AUDCP_AON, &size);
	if (!iram_phy) {
		dev_err(dev, "alloc IRAM AUDCP AON failed\n");
		ret = -ENOMEM;
		goto err_unmap_bin;
	}
	data->iram_phy = iram_phy;
	data->iram_size = size;
	data->iram_virt = audio_mem_vmap(iram_phy);
	if (!data->iram_virt) {
		dev_err(dev, "audio vmap iram cp aon failed\n");
		ret = -ENOMEM;
		goto err_free_iram;
	}

	ret = aud_get_aud_ipc_smsg_addr(&smsg_phy, &smsg_virt, &smsg_size);
	if (ret < 0) {
		dev_err(dev, "get aud ipc smsg address failed\n");
		goto err_unmap_iram;
	}
	data->smsg_virt = smsg_virt;
	data->smsg_phy = smsg_phy;
	data->smsg_size = smsg_size;

	ret = aud_get_aud_ipc_smsg_para_addr(&smsg_para_phy, &smsg_para_virt,
					     &smsg_para_size);
	if (ret < 0) {
		dev_err(dev, "get aud ipc smsg para address failed\n");
		goto err_unmap_iram;
	}
	data->smsg_para_virt = smsg_para_virt;
	data->smsg_para_phy = smsg_para_phy;
	data->smsg_para_size = smsg_para_size;

	ret = sysfs_create_groups(dev, sprd_audcp_boot_groups);
	if (ret) {
		dev_err(dev, "Failed to add audcp boot attributes\n");
		goto err_unmap_iram;
	}

	platform_set_drvdata(pdev, data);
	return 0;

err_unmap_iram:
	audio_mem_unmap(data->iram_virt);
err_free_iram:
	audio_mem_free(MEM_IRAM_AUDCP_AON, data->iram_phy, data->iram_size);
err_unmap_bin:
	audio_mem_unmap(data->bin_virt);
err_free_bin:
	audio_mem_free(MEM_AUDCP_DSPBIN, data->bin_phy, data->ldinfo.size);
	return ret;
}

static int sprd_audcp_boot_remove(struct platform_device *pdev)
{
	struct sprd_audcp_boot_data *data = platform_get_drvdata(pdev);

	if (!data) {
		dev_err(&pdev->dev, "data is null\n");
		return -EINVAL;
	}

	sysfs_remove_groups(&pdev->dev, sprd_audcp_boot_groups);
	audio_mem_unmap(data->iram_virt);
	audio_mem_free(MEM_IRAM_AUDCP_AON, data->iram_phy, data->iram_size);
	if (data->bin_virt)
		audio_mem_unmap(data->bin_virt);
	if (data->bin_phy)
		audio_mem_free(MEM_AUDCP_DSPBIN, data->bin_phy,
			       data->ldinfo.size);
	return 0;
}

/*
 * "agdsp" - AGDSP firmware download.  The image is copied in 4 KiB chunks into
 * the pre-mapped DSP binary area; @bin_chunk is used as the destination index
 * and @bin_written as the running total (and as the clamp against the region
 * size).  Exactly like the vendor build, the source offset advances by a full
 * 4 KiB per iteration even for the final partial chunk.
 */
static ssize_t agdsp_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sprd_audcp_boot_data *data = dev_get_drvdata(dev);
	size_t size, chunk;
	void *dst;
	int left, done = 0;

	if (!data) {
		dev_err(dev, "private data is null\n");
		return -EINVAL;
	}

	dst = data->bin_virt;
	size = min_t(size_t, count,
		     (unsigned int)(data->ldinfo.size - (unsigned int)data->bin_written));

	left = size;
	do {
		chunk = (left >= 4096) ? 4096 : left;
		memcpy(dst + (data->bin_chunk << 12), buf + done, chunk);
		left -= chunk;
		done += 4096;
		data->bin_chunk++;
	} while (left > 0);

	data->bin_written += size;
	return size;
}

/* "status" - raw 3 x u32 blob: corestatus, sysstatus, sleepstatus bitfields. */
static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct sprd_audcp_boot_data *data = dev_get_drvdata(dev);
	unsigned int val;
	unsigned int *out = (unsigned int *)buf;

	if (!data)
		return -ENODEV;

	regmap_read(data->map[AUDCP_CORESTATUS], data->reg[AUDCP_CORESTATUS],
		    &val);
	out[0] = (val & data->mask[AUDCP_CORESTATUS]) >>
		 audcp_shift(data->mask[AUDCP_CORESTATUS]);

	regmap_read(data->map[AUDCP_SYSSTATUS], data->reg[AUDCP_SYSSTATUS], &val);
	out[1] = (val & data->mask[AUDCP_SYSSTATUS]) >>
		 audcp_shift(data->mask[AUDCP_SYSSTATUS]);

	regmap_read(data->map[AUDCP_SLEEPSTATUS], data->reg[AUDCP_SLEEPSTATUS],
		    &val);
	out[2] = (val & data->mask[AUDCP_SLEEPSTATUS]) >>
		 audcp_shift(data->mask[AUDCP_SLEEPSTATUS]);

	return 12;
}

/* "stop" - clear the reset select, assert both resets and shut the DSP down. */
static ssize_t stop_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct sprd_audcp_boot_data *data = dev_get_drvdata(dev);

	if (!data)
		return -ENODEV;

	data->bin_chunk = 0;
	data->bin_written = 0;

	regmap_update_bits(data->map[AUDCP_RESET_SEL], data->reg[AUDCP_RESET_SEL],
			   data->mask[AUDCP_RESET_SEL],
			   ~data->mask[AUDCP_RESET_SEL]);
	regmap_update_bits(data->map[AUDCP_CORERESET],
			   data->reg[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET],
			   ~data->mask[AUDCP_CORERESET]);
	regmap_update_bits(data->map[AUDCP_SYSRESET], data->reg[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET],
			   ~data->mask[AUDCP_SYSRESET]);
	regmap_update_bits(data->map[AUDCP_DEEPSLEEP], data->reg[AUDCP_DEEPSLEEP],
			   data->mask[AUDCP_DEEPSLEEP],
			   data->mask[AUDCP_DEEPSLEEP]);
	regmap_update_bits(data->map[AUDCP_SYSSHUTDOWN],
			   data->reg[AUDCP_SYSSHUTDOWN],
			   data->mask[AUDCP_SYSSHUTDOWN],
			   data->mask[AUDCP_SYSSHUTDOWN]);
	regmap_update_bits(data->map[AUDCP_CORESHUTDOWN],
			   data->reg[AUDCP_CORESHUTDOWN],
			   data->mask[AUDCP_CORESHUTDOWN],
			   data->mask[AUDCP_CORESHUTDOWN]);
	regmap_update_bits(data->map[AUDCP_CORERESET],
			   data->reg[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET]);
	regmap_update_bits(data->map[AUDCP_SYSRESET], data->reg[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET]);

	return count;
}

/*
 * "start" - release the DSP out of reset.
 *
 * In DDR reboot mode (dsp-reboot-mode == 0) the IRAM copy, the SIPC buffers and
 * the boot vector register are (re)initialised before the resets are released,
 * otherwise the DSP fetches the firmware straight from the DDR mapping.
 */
static ssize_t start_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sprd_audcp_boot_data *data = dev_get_drvdata(dev);

	if (!data)
		return -ENODEV;

	if (data->dsp_reboot_mode == 0)
		memset_io(data->iram_virt, 0, data->iram_size);
	memset_io(data->smsg_virt, 0, data->smsg_size);
	memset_io(data->smsg_para_virt, 0, data->smsg_para_size);

	regmap_update_bits(data->map[AUDCP_RESET_SEL], data->reg[AUDCP_RESET_SEL],
			   data->mask[AUDCP_RESET_SEL],
			   ~data->mask[AUDCP_RESET_SEL]);
	regmap_update_bits(data->map[AUDCP_CORERESET],
			   data->reg[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET]);
	regmap_update_bits(data->map[AUDCP_SYSRESET], data->reg[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET]);
	regmap_update_bits(data->map[AUDCP_SYSSHUTDOWN],
			   data->reg[AUDCP_SYSSHUTDOWN],
			   data->mask[AUDCP_SYSSHUTDOWN],
			   ~data->mask[AUDCP_SYSSHUTDOWN]);
	regmap_update_bits(data->map[AUDCP_CORESHUTDOWN],
			   data->reg[AUDCP_CORESHUTDOWN],
			   data->mask[AUDCP_CORESHUTDOWN],
			   ~data->mask[AUDCP_CORESHUTDOWN]);
	regmap_update_bits(data->map[AUDCP_DEEPSLEEP], data->reg[AUDCP_DEEPSLEEP],
			   data->mask[AUDCP_DEEPSLEEP],
			   ~data->mask[AUDCP_DEEPSLEEP]);

	/* Boot protect unlock key, shifted into the documented bit field. */
	regmap_update_bits(data->map[AUDCP_BOOTPROTECT],
			   data->reg[AUDCP_BOOTPROTECT],
			   data->mask[AUDCP_BOOTPROTECT],
			   0x9620 << audcp_shift(data->mask[AUDCP_BOOTPROTECT]));

	if (data->dsp_reboot_mode == 0)
		regmap_update_bits(data->map[AUDCP_BOOTVECTOR],
				   data->reg[AUDCP_BOOTVECTOR],
				   data->mask[AUDCP_BOOTVECTOR],
				   data->boot_addr <<
				   audcp_shift(data->mask[AUDCP_BOOTVECTOR]));

	regmap_update_bits(data->map[AUDCP_BOOTADDRESS_SEL],
			   data->reg[AUDCP_BOOTADDRESS_SEL],
			   data->mask[AUDCP_BOOTADDRESS_SEL],
			   data->mask[AUDCP_BOOTADDRESS_SEL]);

	regmap_update_bits(data->map[AUDCP_CORERESET],
			   data->reg[AUDCP_CORERESET],
			   data->mask[AUDCP_CORERESET],
			   ~data->mask[AUDCP_CORERESET]);
	regmap_update_bits(data->map[AUDCP_SYSRESET], data->reg[AUDCP_SYSRESET],
			   data->mask[AUDCP_SYSRESET],
			   ~data->mask[AUDCP_SYSRESET]);

	return count;
}

/* "ldinfo" - raw 40 byte load info blob. */
static ssize_t ldinfo_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct sprd_audcp_boot_data *data = dev_get_drvdata(dev);

	if (!data)
		return -ENODEV;

	memcpy(buf, &data->ldinfo, sizeof(data->ldinfo));
	return sizeof(data->ldinfo);
}

static DEVICE_ATTR_RO(ldinfo);
static DEVICE_ATTR_WO(start);
static DEVICE_ATTR_WO(stop);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_WO(agdsp);

static struct attribute *sprd_audcp_boot_attrs[] = {
	&dev_attr_ldinfo.attr,
	&dev_attr_start.attr,
	&dev_attr_stop.attr,
	&dev_attr_status.attr,
	&dev_attr_agdsp.attr,
	NULL,
};

static const struct attribute_group sprd_audcp_boot_group = {
	.attrs = sprd_audcp_boot_attrs,
};

static const struct attribute_group *sprd_audcp_boot_groups[] = {
	&sprd_audcp_boot_group,
	NULL,
};

static const struct of_device_id sprd_audcp_boot_match_table[] = {
	{ .compatible = "unisoc,sharkl5-audcp-boot" },
	{ .compatible = "unisoc,roc1-audcp-boot" },
	{ .compatible = "unisoc,orca-audcp-boot" },
	{ .compatible = "unisoc,audcp-boot" },
	{ }
};

static struct platform_driver sprd_audcp_boot_driver = {
	.probe = sprd_audcp_boot_probe,
	.remove = sprd_audcp_boot_remove,
	.driver = {
		.name = "sprd_audcp_boot",
		.of_match_table = sprd_audcp_boot_match_table,
	},
};

module_platform_driver(sprd_audcp_boot_driver);

MODULE_AUTHOR("Lei Ning <lei.ning@unisoc.com>");
MODULE_DESCRIPTION("SPRD Audio CP boot Driver");
MODULE_LICENSE("GPL v2");
