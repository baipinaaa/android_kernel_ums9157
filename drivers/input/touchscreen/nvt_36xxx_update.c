/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Novatek NT36xxx SPI touchscreen driver - firmware / proc / flash support.
 *
 * Reconstructed from nvt_nt36xxx_spi_ts.ko (vendor kernel 5.4.210).
 *   .text layout (this file):
 *     nvt_flash_read           0x3204 (908)   nvt_flash_open      0x3590 (116)
 *     nvt_flash_close          0x3604 (40)    nvt_extra_proc_init 0x3d54 (516)
 *     nvt_diff_open            0x3f58 (560)   c_start             0x4188 (16)
 *     c_stop                   0x4198 (4)     c_next              0x419c (20)
 *     c_show                   0x41b0 (232)   nvt_raw_open        0x4298 (560)
 *     nvt_baseline_open        0x44c8 (456)   nvt_fw_version_open 0x4690 (184)
 *     c_fw_version_show        0x4748 (64)    nvt_extra_proc_deinit 0x4788 (336)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/spi/spi.h>

#include "nvt_36xxx.h"

/* ------------------------------------------------------------------ */
/* flash device ( /proc/NVTSPI )                                      */
/* ------------------------------------------------------------------ */

static struct proc_dir_entry *NVT_proc_entry;

#define NVT_FLASH_DEV_SIZE	(64 * 1024 + 1)

static ssize_t nvt_flash_read(struct file *file, char __user *buf, size_t count,
			      loff_t *pos)
{
	int ret = 0;
	u8 *str;
	u8 *rbuf;
	u16 length;
	int i;

	if (count < 3 || count > NVT_FLASH_DEV_SIZE) {
		NVT_ERR("invalid transfer len!");
		return -EFAULT;
	}

	str = kmalloc(count, GFP_KERNEL);
	if (!str) {
		NVT_ERR("kzalloc for buf failed!");
		return -ENOMEM;
	}

	rbuf = kmalloc(count, GFP_KERNEL | __GFP_ZERO);
	if (!rbuf) {
		NVT_ERR("kzalloc for buf failed!");
		kfree(str);
		return -ENOMEM;
	}

	if (copy_from_user(str, buf, count)) {
		NVT_ERR("copy from user error");
		ret = -EFAULT;
		goto out;
	}

	length = ((u16)(str[0] & 0x7f) << 8) | str[1];
	if (length > (u16)count) {
		NVT_ERR("lenth is too long for str point size");
		ret = -EFAULT;
		goto out;
	}

	memcpy(rbuf, str + 2, length - 2);

	if ((s8)str[0] < 0) {
		for (i = 0; i < 0x13; i++) {
			ret = CTP_SPI_READ(ts->client, rbuf, length);
			if (!ret)
				break;
			NVT_ERR("error, retries=%d, ret=%d", i + 1, ret);
		}
	} else {
		for (i = 0; i < 0x14; i++) {
			ret = CTP_SPI_WRITE(ts->client, rbuf, length);
			if (!ret)
				break;
			NVT_ERR("error, retries=%d, ret=%d", i + 1, ret);
		}
		if (i == 0x14)
			NVT_ERR("error, ret = %d", ret);
	}

	if (!ret) {
		if (copy_to_user(buf, str, count))
			ret = -EFAULT;
	}

out:
	kfree(str);
	kfree(rbuf);

	return ret;
}

static int nvt_flash_open(struct inode *inode, struct file *file)
{
	void *p;

	p = kmalloc(8, GFP_KERNEL);
	if (!p) {
		NVT_ERR("Failed to allocate memory for nvt flash data");
		return -ENOMEM;
	}

	memset(p, 0x00, 8);
	file->private_data = p;

	return 0;
}

static int nvt_flash_close(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations nvt_flash_fops = {
	.owner = THIS_MODULE,
	.read = nvt_flash_read,
	.open = nvt_flash_open,
	.release = nvt_flash_close,
};

int nvt_flash_proc_init(void)
{
	NVT_proc_entry = proc_create("NVTSPI", 0444, NULL, &nvt_flash_fops);
	if (!NVT_proc_entry) {
		NVT_ERR("Create /proc/%s", "NVTSPI");
		NVT_ERR("Failed!");
		return -ENOMEM;
	}

	NVT_LOG("create proc/%s Succeeded!", "NVTSPI");
	NVT_LOG("Create /proc/%s", "NVTSPI");
	NVT_LOG("nvt_flash_proc_init Succeeded!");

	return 0;
}

void nvt_flash_proc_deinit(void)
{
	if (NVT_proc_entry) {
		remove_proc_entry("NVTSPI", NULL);
		NVT_proc_entry = NULL;
	}
	NVT_LOG("Removed /proc/%s", "NVTSPI");
}

/* ------------------------------------------------------------------ */
/* proc entries: raw / baseline / diff / fw_version                   */
/* ------------------------------------------------------------------ */

static int nvt_raw_open(struct inode *inode, struct file *file);
static int nvt_diff_open(struct inode *inode, struct file *file);
static int nvt_baseline_open(struct inode *inode, struct file *file);
static int nvt_fw_version_open(struct inode *inode, struct file *file);

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

/* 0x41b0, 232 bytes */
static int c_show(struct seq_file *m, void *v)
{
	int i, j;
	u8 *xdata_u8 = (u8 *)xdata;

	for (i = 0; i < ts->y_num; i++) {
		if (!ts->x_num) {
			seq_puts(m, "\n");
			continue;
		}

		for (j = 0; j < ts->x_num; j++) {
			int idx = i * ts->x_num + j;

			if (idx >= 0x800) {
				WARN_ON(1);
				break;
			}
			seq_printf(m, "%5d, ", *(u32 *)(xdata_u8 + idx * 4));
		}
		seq_puts(m, "\n");
	}
	seq_printf(m, "\n\n");

	return 0;
}

/* 0x4748, 64 bytes */
static int c_fw_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n",
		   ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);

	return 0;
}

static struct seq_operations nvt_seq_ops = {
	.start = c_start,
	.stop = c_stop,
	.next = c_next,
	.show = c_show,
};

static struct seq_operations nvt_fw_version_seq_ops = {
	.start = c_start,
	.stop = c_stop,
	.next = c_next,
	.show = c_fw_version_show,
};

static const struct file_operations nvt_raw_fops = {
	.owner = THIS_MODULE,
	.open = nvt_raw_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations nvt_baseline_fops = {
	.owner = THIS_MODULE,
	.open = nvt_baseline_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations nvt_diff_fops = {
	.owner = THIS_MODULE,
	.open = nvt_diff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations nvt_fw_version_fops = {
	.owner = THIS_MODULE,
	.open = nvt_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 0x3f58, 560 bytes */
static int nvt_diff_open(struct inode *inode, struct file *file)
{
	int ret;
	u8 buf[4];
	u16 cmd;

	ret = mutex_lock_interruptible(&ts->lock);
	if (ret)
		return -ERESTARTSYS;

	NVT_LOG("++");

	ret = nvt_clear_fw_status();
	if (ret) {
		ret = -EAGAIN;
		goto unlock;
	}

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x2250;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	ret = nvt_check_fw_status();
	if (ret)
		goto unlock_err;

	ret = nvt_get_fw_info();
	if (ret)
		goto unlock_err;

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x51;
	ret = CTP_SPI_READ(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	if (cmd & 0x100)
		nvt_read_mdata(*(u32 *)((u8 *)ts->mmap + 0x14), NULL, 0);
	else
		nvt_read_mdata(*(u32 *)((u8 *)ts->mmap + 0x18), NULL, 0);

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x50;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	cmd = 0xbb51;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	msleep(20);
	mutex_unlock(&ts->lock);

	NVT_LOG("--");

	ret = seq_open(file, &nvt_seq_ops);
	if (ret)
		return ret;

	return 0;

unlock_err:
	mutex_unlock(&ts->lock);
	return -EAGAIN;

unlock:
	mutex_unlock(&ts->lock);
	return ret;
}

/* 0x4298, 560 bytes */
static int nvt_raw_open(struct inode *inode, struct file *file)
{
	int ret;
	u8 buf[4];
	u16 cmd;

	ret = mutex_lock_interruptible(&ts->lock);
	if (ret)
		return -ERESTARTSYS;

	NVT_LOG("++");

	ret = nvt_clear_fw_status();
	if (ret) {
		ret = -EAGAIN;
		goto unlock;
	}

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x2250;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	ret = nvt_check_fw_status();
	if (ret)
		goto unlock_err;

	ret = nvt_get_fw_info();
	if (ret)
		goto unlock_err;

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x51;
	ret = CTP_SPI_READ(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	if (cmd & 0x100)
		nvt_read_mdata(*(u32 *)((u8 *)ts->mmap + 0x04), NULL, 0);
	else
		nvt_read_mdata(*(u32 *)((u8 *)ts->mmap + 0x08), NULL, 0);

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x50;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	cmd = 0xbb51;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	msleep(20);
	mutex_unlock(&ts->lock);

	NVT_LOG("--");

	ret = seq_open(file, &nvt_seq_ops);
	if (ret)
		return ret;

	return 0;

unlock_err:
	mutex_unlock(&ts->lock);
	return -EAGAIN;

unlock:
	mutex_unlock(&ts->lock);
	return ret;
}

/* 0x44c8, 456 bytes */
static int nvt_baseline_open(struct inode *inode, struct file *file)
{
	int ret;
	u8 buf[4];
	u16 cmd;

	ret = mutex_lock_interruptible(&ts->lock);
	if (ret)
		return -ERESTARTSYS;

	NVT_LOG("++");

	ret = nvt_clear_fw_status();
	if (ret) {
		ret = -EAGAIN;
		goto unlock;
	}

	buf[0] = 0xff;
	buf[1] = *(u32 *)ts->mmap >> 15;
	buf[2] = *(u32 *)ts->mmap >> 7;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret)
		goto unlock_err;

	cmd = 0x2250;
	ret = CTP_SPI_WRITE(ts->client, (u8 *)&cmd, 2);
	if (ret)
		goto unlock_err;

	ret = nvt_check_fw_status();
	if (ret)
		goto unlock_err;

	ret = nvt_get_fw_info();
	if (ret)
		goto unlock_err;

	mutex_unlock(&ts->lock);

	NVT_LOG("--");

	ret = seq_open(file, &nvt_seq_ops);
	if (ret)
		return ret;

	return 0;

unlock_err:
	mutex_unlock(&ts->lock);
	return -EAGAIN;

unlock:
	mutex_unlock(&ts->lock);
	return ret;
}

/* 0x4690, 184 bytes */
static int nvt_fw_version_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = mutex_lock_interruptible(&ts->lock);
	if (ret)
		return -ERESTARTSYS;

	NVT_LOG("++");

	ret = nvt_get_fw_info();
	mutex_unlock(&ts->lock);

	if (ret)
		return -EAGAIN;

	NVT_LOG("--");

	ret = seq_open(file, &nvt_fw_version_seq_ops);
	if (ret)
		return ret;

	return 0;
}

/* 0x3d54, 516 bytes */
int nvt_extra_proc_init(void)
{
	NVT_proc_fw_version_entry = proc_create("nvt_fw_version", 0444, NULL,
						&nvt_fw_version_fops);
	if (!NVT_proc_fw_version_entry) {
		NVT_ERR("Create /proc/%s", "nvt_fw_version");
		NVT_ERR("create proc/%s Failed!", "nvt_fw_version");
		return -ENOMEM;
	}
	NVT_LOG("create proc/%s Succeeded!", "nvt_fw_version");

	NVT_proc_baseline_entry = proc_create("nvt_baseline", 0444, NULL,
					      &nvt_baseline_fops);
	if (!NVT_proc_baseline_entry) {
		NVT_ERR("Create /proc/%s", "nvt_baseline");
		return -ENOMEM;
	}
	NVT_LOG("create proc/%s Succeeded!", "nvt_baseline");

	NVT_proc_raw_entry = proc_create("nvt_raw", 0444, NULL, &nvt_raw_fops);
	if (!NVT_proc_raw_entry) {
		NVT_ERR("Create /proc/%s", "nvt_raw");
		return -ENOMEM;
	}
	NVT_LOG("create proc/%s Succeeded!", "nvt_raw");

	NVT_proc_diff_entry = proc_create("nvt_diff", 0444, NULL, &nvt_diff_fops);
	if (!NVT_proc_diff_entry) {
		NVT_ERR("Create /proc/%s", "nvt_diff");
		return -ENOMEM;
	}
	NVT_LOG("create proc/%s Succeeded!", "nvt_diff");

	return 0;
}

/* 0x4788, 336 bytes */
void nvt_extra_proc_deinit(void)
{
	if (NVT_proc_fw_version_entry) {
		remove_proc_entry("nvt_fw_version", NULL);
		NVT_proc_fw_version_entry = NULL;
		NVT_LOG("Removed /proc/%s", "nvt_fw_version");
	}

	if (NVT_proc_baseline_entry) {
		remove_proc_entry("nvt_baseline", NULL);
		NVT_proc_baseline_entry = NULL;
		NVT_LOG("Removed /proc/%s", "nvt_baseline");
	}

	if (NVT_proc_raw_entry) {
		remove_proc_entry("nvt_raw", NULL);
		NVT_proc_raw_entry = NULL;
		NVT_LOG("Removed /proc/%s", "nvt_raw");
	}

	if (NVT_proc_diff_entry) {
		remove_proc_entry("nvt_diff", NULL);
		NVT_proc_diff_entry = NULL;
		NVT_LOG("Removed /proc/%s", "nvt_diff");
	}
}

/* ------------------------------------------------------------------ */
/* firmware update                                                    */
/* ------------------------------------------------------------------ */

/*
 * The vendor module keeps all of the update state in file-scope statics that
 * live at the tail of .bss:
 *
 *   .bss+0x28c0  const struct firmware *fw_entry
 *   .bss+0x28c8  size_t fw_need_write_size
 *   .bss+0x28d0  u32 partition
 *   .bss+0x28d8  struct nvt_bin_map *bin_map
 *   .bss+0x28e0  u8 *fwbuf
 */
static const struct firmware *fw_entry;
static size_t fw_need_write_size;
static u32 partition;
static struct nvt_bin_map *bin_map;
static u8 *fwbuf;

static int nvt_check_fw_checksum(u32 len);
static int nvt_page_write(u32 addr, u8 b0, u8 b1);

/* helper used by the hw crc path: set the internal page then write 2 bytes */
static int nvt_page_write(u32 addr, u8 b0, u8 b1)
{
	u8 page[4];
	u8 data[4];

	page[0] = 0xff;
	page[1] = addr >> 15;
	page[2] = addr >> 7;
	CTP_SPI_WRITE(ts->client, page, 3);

	data[0] = addr & 0x7f;
	data[1] = b0;
	data[2] = b1;
	return CTP_SPI_WRITE(ts->client, data, 3);
}

#define NVT_FWBUF_SIZE		0xfc01
#define NVT_SRAM_BLOCK_SIZE	0x400

/* 0x4f88 area (inlined into nvt_update_firmware) */
static int nvt_download_init(void)
{
	if (fwbuf)
		return 0;

	/* kmalloc_order(0xfc02, GFP_KERNEL, order 4) */
	fwbuf = (u8 *)__get_free_pages(GFP_KERNEL, 4);
	if (!fwbuf) {
		NVT_ERR("kzalloc for fwbuf failed!");
		return -ENOMEM;
	}

	return 0;
}

/* 0x49e0..0x4acc (inlined): locate the "NVT"/"MOD" end flag */
static int nvt_get_fw_need_write_size(const struct firmware *fw)
{
	size_t blocks = fw->size >> 12;
	size_t size = blocks << 12;
	const u8 *p;

	if (!blocks)
		return -EINVAL;

	p = fw->data + size - 3;
	while (blocks) {
		if (!strncmp((const char *)p, "NVT", 3)) {
			fw_need_write_size = size;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), NVT end flag",
				size, size);
			break;
		}
		if (!strncmp((const char *)p, "MOD", 3)) {
			fw_need_write_size = size;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), MOD end flag",
				size, size);
			break;
		}
		blocks--;
		size -= 0x1000;
		p -= 0x1000;
	}

	if (!fw_need_write_size) {
		NVT_ERR("end flag \"NVT\" \"MOD\" not found!");
		NVT_ERR("get fw need to write size fail!");
		return -EINVAL;
	}

	return 0;
}

static u32 nvt_checksum_sum(const u8 *data, u32 addr, u32 size)
{
	u32 sum = 0;
	u32 i;

	for (i = 0; i < size; i++)
		sum += data[addr + i];

	return -(size + sum);
}

/* 0x4ad0..0x4f20 (inlined): parse the bin header into bin_map[] */
static int nvt_bin_header_parser(const struct firmware *fw)
{
	const u8 *data = fw->data;
	size_t size = fw_need_write_size;
	u32 fw_ver, fw_ver_bar;
	u32 ovly_info, ovly_sec_num, ilm_dlm_num, info_sec_num;
	u32 i, idx;

	fw_ver = data[0];
	fw_ver_bar = data[size - 0xfff];

	if ((u8)(fw_ver + fw_ver_bar) != 0xff) {
		NVT_ERR("bin file FW_VER + FW_VER_BAR should be 0xFF!");
		NVT_ERR("FW_VER=0x%02X, FW_VER_BAR=0x%02X", fw_ver, fw_ver_bar);
		return -ENOEXEC;
	}

	/* fw_ver is '0'..'9' based: info sections are counted in steps of 0x10 */
	if (fw_ver < 0x31) {
		info_sec_num = 0;
	} else {
		u32 v = 0x30;
		u32 n = 0;

		while (v < fw_ver) {
			v += 0x10;
			n++;
		}
		info_sec_num = n & 0xff;
	}

	ovly_info = data[0x28];
	ilm_dlm_num = (ovly_info & 0x10) ? (ovly_info & 0x0f) : 0;
	ovly_sec_num = (ovly_info >> 4) & 0x01;

	partition = info_sec_num + 2 + ilm_dlm_num;
	NVT_LOG("ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d",
		ovly_info, ilm_dlm_num, ovly_sec_num, info_sec_num, partition);

	bin_map = kmalloc((partition + 1) * sizeof(struct nvt_bin_map),
			  GFP_KERNEL);
	if (!bin_map) {
		NVT_ERR("kzalloc for bin_map failed!");
		return -ENOMEM;
	}

	/* bin_map[0] = ILM, bin_map[1] = DLM */
	for (i = 0; i < 2; i++) {
		struct nvt_bin_map *e = &bin_map[i];
		u32 addr = *(u32 *)(data + i * 0x0c);

		e->sram = *(u32 *)(data + i * 0x0c + 4);
		e->size = *(u32 *)(data + i * 0x0c + 8);
		e->addr = addr;

		if (addr + e->size > fw_need_write_size) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!",
				addr, addr + e->size);
			return -EINVAL;
		}

		if (ts->hw_crc)
			e->checksum = *(u32 *)(data + i * 4 + 0x18);
		else
			e->checksum = nvt_checksum_sum(data, addr, e->size);

		if (i == 0)
			strcpy(e->name, "ILM");
		else
			strcpy(e->name, "DLM");
	}

	/* overlay sections */
	for (idx = 2; idx < partition; idx++) {
		struct nvt_bin_map *e = &bin_map[idx];
		u32 base;

		if (idx < info_sec_num + 2) {
			base = (idx - 2) * 0x10;

			e->sram = *(u32 *)(data + base + 0x10);
			e->size = *(u32 *)(data + base + 0x14);
			e->addr = *(u32 *)(data + base + 0x18);

			if (ts->hw_crc)
				e->checksum = *(u32 *)(data + base + 0x1c);
			else
				e->checksum = nvt_checksum_sum(data, e->addr,
							       e->size);

			sprintf(e->name, "Overlay-%d", idx - 2);
		} else {
			base = (idx - info_sec_num - 2) * 0x10 + 0x28;

			e->sram = *(u32 *)(data + base);
			e->size = *(u32 *)(data + base + 4);
			e->addr = *(u32 *)(data + base + 8);

			if (ts->hw_crc)
				e->checksum = *(u32 *)(data + base + 0x0c);
			else
				e->checksum = nvt_checksum_sum(data, e->addr,
							       e->size);

			strcpy(e->name, "Header");
		}

		if (e->addr + e->size > fw_need_write_size) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!",
				e->addr, e->addr + e->size);
			return -EINVAL;
		}
	}

	return 0;
}

/* inlined in the vendor binary as nvt_write_sram() */
static int nvt_write_sram(u32 sram, const u8 *src, u32 chunk)
{
	u8 page[4];
	int ret;

	page[0] = 0xff;
	page[1] = sram >> 15;
	page[2] = sram >> 7;
	ret = CTP_SPI_WRITE(ts->client, page, 3);
	if (ret) {
		NVT_ERR("set page failed, ret = %d", ret);
		return ret;
	}

	fwbuf[0] = sram & 0x7f;
	memcpy(fwbuf + 1, src, chunk);
	ret = CTP_SPI_WRITE(ts->client, fwbuf, chunk + 1);
	if (ret) {
		NVT_ERR("write to sram failed, ret = %d", ret);
		return ret;
	}

	return 0;
}

/* 0x58c0, 624 bytes */
int nvt_write_firmware(const u8 *fw_image, u32 fwlen)
{
	u32 i;

	memset(fwbuf, 0, NVT_FWBUF_SIZE);

	if (!partition)
		return 0;

	for (i = 0; i < partition; i++) {
		struct nvt_bin_map *e = &bin_map[i];
		u32 addr, sram, remain, chunks, ret;

		if (e->addr + e->size > fwlen) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!",
				e->addr, e->addr + e->size);
			return -EINVAL;
		}

		if (!e->size)
			continue;

		addr = e->addr;
		sram = e->sram;
		remain = e->size + 1;
		chunks = DIV_ROUND_UP(remain, NVT_SRAM_BLOCK_SIZE);

		while (chunks--) {
			u32 chunk = min(remain, (u32)NVT_SRAM_BLOCK_SIZE);

			ret = nvt_write_sram(sram, fw_image + addr, chunk);
			if (ret) {
				NVT_ERR("sram program failed, ret = %d", ret);
				return ret;
			}

			sram += NVT_SRAM_BLOCK_SIZE;
			addr += NVT_SRAM_BLOCK_SIZE;
			remain -= NVT_SRAM_BLOCK_SIZE;
		}
	}

	return 0;
}

/* 0x5b30, 360 bytes */
int nvt_set_bld_crc_bank(u32 addr, u32 v1, u32 v2, u32 v3, u32 v4)
{
	u8 page[4];
	u32 w2;

	page[0] = 0xff;
	page[1] = addr >> 15;
	page[2] = addr >> 7;
	CTP_SPI_WRITE(ts->client, page, 3);

	fwbuf[0] = addr & 0x7f;
	fwbuf[1] = v1;
	fwbuf[2] = v1 >> 8;
	fwbuf[3] = v1 >> 16;
	CTP_SPI_WRITE(ts->client, fwbuf, 4);

	fwbuf[0] = v2 & 0x7f;
	fwbuf[1] = v3;
	fwbuf[2] = v3 >> 8;
	fwbuf[3] = (v3 >> 16) & 1;

	if (ts->hw_crc) {
		w2 = (ts->hw_crc == 1) ? 3 : 4;
		CTP_SPI_WRITE(ts->client, fwbuf, w2);
	}

	fwbuf[0] = v3 & 0x7f;
	fwbuf[1] = v4;
	fwbuf[2] = v4 >> 8;
	fwbuf[3] = v4 >> 16;
	fwbuf[4] = v4 >> 24;
	CTP_SPI_WRITE(ts->client, fwbuf, 5);

	return 0;
}

/* 0x5010..0x52a8 (inlined): download firmware w/o hardware CRC */
static int nvt_download_firmware(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < 3; i++) {
		nvt_write_addr(ENG_RST_ADDR, 0x5a);
		__const_udelay(0x418958);

		nvt_write_addr(SWRST_N8_ADDR, 0x69);
		__const_udelay(0x147aeb8);

		if (SPI_RD_FAST_ADDR)
			nvt_write_addr(SPI_RD_FAST_ADDR, 0);

		nvt_write_addr(ts->mmap->rchive_fw_i2c_addr | 0x60, 0);

		ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
		if (ret) {
			NVT_ERR("Write_Firmware failed. (%d)", ret);
			goto next;
		}

		nvt_write_addr(ts->mmap->boot_ready_clr_addr, 1);
		__const_udelay(0x147aeb8);

		if (!ts->hw_crc) {
			nvt_write_addr(ts->mmap->boot_ready_clr_addr, 0);
			nvt_write_addr(ts->mmap->boot_ready_val_addr, 0xa0);
		}

		ret = nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)", ret);
			goto next;
		}

		/* read back the checksums programmed by the firmware */
		memset(fwbuf, 0, partition * 4);

		ret = nvt_check_fw_checksum(partition * 4 + 1);
		if (ret)
			goto next;

		return 0;

next:
		NVT_ERR("firmware checksum not match, retry=%d", i);
	}

	return ret ? ret : -EIO;
}

/* 0x51b0..0x5268 (inlined): compare read-back checksums */
static int nvt_check_fw_checksum(u32 len)
{
	u8 page[4];
	u32 i;
	int ret;

	page[0] = 0xff;
	page[1] = ts->mmap->fdbg_addr >> 15;
	page[2] = ts->mmap->fdbg_addr >> 7;
	CTP_SPI_WRITE(ts->client, page, 3);

	fwbuf[0] = ts->mmap->fdbg_addr & 0x7f;
	ret = CTP_SPI_READ(ts->client, fwbuf, len);
	if (ret) {
		NVT_ERR("Read fw checksum failed");
		return ret;
	}

	if (!partition)
		return 0;

	for (i = 0; i < partition; i++) {
		struct nvt_bin_map *e = &bin_map[i];
		u32 fw_checksum;

		if (!e->size)
			continue;

		fw_checksum = *(u32 *)(fwbuf + i * 4 + 1);
		if (e->checksum != fw_checksum) {
			NVT_ERR("[%d] BIN_checksum=0x%08X, FW_checksum=0x%08X",
				i, e->checksum, fw_checksum);
			return -EIO;
		}
	}

	return 0;
}

/* 0x52b0..0x57a4 (inlined): download firmware with hardware CRC */
static int nvt_download_firmware_hw_crc(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < 3; i++) {
		nvt_write_addr(SWRST_N8_ADDR, 0x69);
		__const_udelay(0x147aeb8);

		if (SPI_RD_FAST_ADDR)
			nvt_write_addr(SPI_RD_FAST_ADDR, 0);

		ret = nvt_write_firmware(fw_entry->data, fw_entry->size);
		if (ret) {
			NVT_ERR("Write_Firmware failed. (%d)", ret);
			goto next;
		}

		/* ILM / DLM crc banks */
		nvt_set_bld_crc_bank(ts->mmap->dlm_crc_len,
				     bin_map[0].sram, ts->mmap->swrst_n8_addr,
				     ts->mmap->sram_buf_addr,
				     bin_map[0].checksum);
		nvt_set_bld_crc_bank(ts->mmap->dlm_crc_buf_addr,
				     bin_map[1].sram, ts->mmap->hw_crc_addr,
				     ts->mmap->sram_buf_len,
				     bin_map[1].checksum);

		/* start bld crc check */
		nvt_page_write(ts->mmap->bld_crc_bank, 0x00, 0x00);

		nvt_read_bld_hw_crc();

		nvt_write_addr(ts->mmap->boot_ready_clr_addr, 1);
		__const_udelay(0x147aeb8);

		if (!ts->hw_crc) {
			nvt_write_addr(ts->mmap->boot_ready_clr_addr, 0);
			nvt_write_addr(ts->mmap->boot_ready_val_addr, 0xa0);
		}

		ret = nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)", ret);
			goto next;
		}

		return 0;

next:
		NVT_ERR("firmware checksum not match, retry=%d", i);
	}

	return -EIO;
}

/* 0x5538..0x5790 (inlined): read back the hw CRC results */
static int nvt_read_bld_hw_crc(void)
{
	u8 buf[8];
	u32 crc_done, ilm_crc_flag, dlm_crc_flag;
	u32 ilm_bin, ilm_golden, ilm_result;
	u32 dlm_bin, dlm_golden, dlm_result;
	int ret;

	/* crc_done / flags */
	nvt_page_write(ts->mmap->rsvd[0], 0x00, 0x00);
	buf[0] = ts->mmap->rsvd[0] & 0x7f;
	ret = CTP_SPI_READ(ts->client, buf, 2);
	if (ret)
		return ret;

	crc_done = (buf[1] >> 2) & 1;
	dlm_crc_flag = buf[1] & 1;
	ilm_crc_flag = (buf[1] >> 1) & 1;
	NVT_LOG("crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d",
		crc_done, ilm_crc_flag, dlm_crc_flag);

	/* ilm */
	nvt_page_write(ts->mmap->sram_buf_addr, 0x00, 0x00);
	memset(buf, 0, sizeof(buf));
	buf[0] = ts->mmap->sram_buf_addr & 0x7f;
	ret = CTP_SPI_READ(ts->client, buf, 5);
	if (ret)
		return ret;
	ilm_golden = *(u32 *)&buf[1];

	nvt_page_write(ts->mmap->fdbg_addr, 0x00, 0x00);
	memset(buf, 0, sizeof(buf));
	buf[0] = ts->mmap->fdbg_addr & 0x7f;
	ret = CTP_SPI_READ(ts->client, buf, 5);
	if (ret)
		return ret;
	ilm_result = *(u32 *)&buf[1];
	ilm_bin = bin_map[0].checksum;

	NVT_LOG("ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X",
		ilm_bin, ilm_golden, ilm_result);

	/* dlm */
	nvt_page_write(ts->mmap->sram_buf_len, 0x00, 0x00);
	memset(buf, 0, sizeof(buf));
	buf[0] = ts->mmap->sram_buf_len & 0x7f;
	ret = CTP_SPI_READ(ts->client, buf, 5);
	if (ret)
		return ret;
	dlm_golden = *(u32 *)&buf[1];

	nvt_page_write(ts->mmap->fdbg_len, 0x00, 0x00);
	memset(buf, 0, sizeof(buf));
	buf[0] = ts->mmap->fdbg_len & 0x7f;
	ret = CTP_SPI_READ(ts->client, buf, 5);
	if (ret)
		return ret;
	dlm_result = *(u32 *)&buf[1];
	dlm_bin = bin_map[1].checksum;

	NVT_LOG("dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X",
		dlm_bin, dlm_golden, dlm_result);

	return 0;
}


/* 0x48d8, 4072 bytes */
int nvt_update_firmware(const char *fw_name)
{
	static int in_progress;
	int ret = 0;
	int retry;
	unsigned long start;

	if (in_progress) {
		NVT_ERR("update_firmware_request failed. (%d)", -EBUSY);
		return -EBUSY;
	}
	in_progress = 1;
	start = jiffies;

	for (retry = 0; retry < 3; retry++) {
		NVT_LOG("filename is %s", fw_name);
		ret = request_firmware(&fw_entry, fw_name, ts->client->dev.parent);
		if (!ret)
			break;
		NVT_ERR("firmware load failed, ret=%d", ret);
	}
	if (ret) {
		NVT_ERR("error, retry=%d", 3);
		goto out;
	}

	ret = nvt_get_fw_need_write_size(fw_entry);
	if (ret)
		goto out;

	ret = nvt_bin_header_parser(fw_entry);
	if (ret) {
		NVT_ERR("bin header parser failed");
		goto out;
	}

	ret = nvt_download_init();
	if (ret) {
		NVT_ERR("Download Init failed. (%d)", ret);
		goto out;
	}

	if (ts->hw_crc)
		ret = nvt_download_firmware_hw_crc();
	else
		ret = nvt_download_firmware();

	if (ret) {
		NVT_ERR("Download Firmware failed. (%d)", ret);
		goto out;
	}

	NVT_LOG("Update firmware success! <%ld us>",
		(jiffies - start) * 1000000 / HZ);

	ret = nvt_get_fw_info();

out:
	if (bin_map) {
		kfree(bin_map);
		bin_map = NULL;
	}
	if (fw_entry) {
		release_firmware(fw_entry);
		fw_entry = NULL;
	}
	in_progress = 0;

	return ret;
}

/* 0x5c98, 72 bytes */
int Boot_Update_Firmware(struct device *dev)
{
	mutex_lock(&ts->lock);
	nvt_update_firmware(NVT_FW_FILE);
	mutex_unlock(&ts->lock);

	return 0;
}
