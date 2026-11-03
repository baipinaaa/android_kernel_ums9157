// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) camera sensor driver - misc device and file operations.
 *
 * Reconstructed from sprd_sensor.ko (sprd_sensor_core.c of the vendor tree).
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/wakelock.h>
#include <linux/pm_wakeup.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "sprd_sensor.h"

/* exported by sprd_camsys_pw_domain.ko */
extern int sprd_glb_mm_pw_on_cfg(void);
extern int sprd_glb_mm_pw_off_cfg(void);

/* ------------------------------------------------------------------ */
/* module wide objects                                                 */
/* ------------------------------------------------------------------ */

/* context of every registered sensor, indexed by sensor_id */
struct sensor_dev_data *s_sensor_dev_data[SENSOR_MAX_NUM];

/* name reported through /sys/class/misc/sprd_sensor/camera_sensor_name */
static char _sensor_type_info[255];

/* 0x4004521a is accepted before the private key check */
static u32 s_private_key_unlock;

struct sensor_dev_data *sprd_sensor_get_dev_context(u32 sensor_id)
{
	if (sensor_id >= SENSOR_MAX_NUM) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor_id %d error!\n",
		       current->pid, __func__, __LINE__, sensor_id);
		return NULL;
	}

	return s_sensor_dev_data[sensor_id];
}

int sprd_sensor_find_dcam_id(u32 sensor_id)
{
	int i;

	for (i = 0; i < SENSOR_MAX_NUM; i++) {
		struct sensor_dev_data *p = s_sensor_dev_data[i];

		if (!p)
			continue;
		if (p->sensor_id == sensor_id && p->dcam_id != (u32)-1) {
			pr_debug("SENSOR_DRV: %d: %s L %d: find sensor %d attached dcam id %d\n",
				 current->pid, __func__, __LINE__, sensor_id,
				 p->dcam_id);
			return p->dcam_id;
		}
	}

	pr_info("SENSOR_CORE: %d: %s line=%d: sensor misc device not found\n",
		current->pid, __func__, __LINE__);

	pr_err("SENSOR_DRV: %d: %s L %d: find sensor %d attached dcam id fail!\n",
	       current->pid, __func__, __LINE__, sensor_id);

	return -1;
}
EXPORT_SYMBOL_GPL(sprd_sensor_find_dcam_id);

static ssize_t sprd_get_sensor_name_info(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	pr_info("SENSOR_CORE: %d: %s line=%d: sprd_sensor: _sensor_type_info %s\n",
		current->pid, __func__, __LINE__, _sensor_type_info);

	return scnprintf(buf, PAGE_SIZE, "%s\n", _sensor_type_info);
}

static ssize_t sprd_sensor_set_sensor_name_info(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	if (strlen(buf) >= sizeof(_sensor_type_info)) {
		pr_info("SENSOR_CORE: %d: %s line=%d: out of the maxnum 255.\n",
			current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	memset(_sensor_type_info, 0, sizeof(_sensor_type_info));
	memcpy(_sensor_type_info, buf, strlen(buf));

	return count;
}

static DEVICE_ATTR(camera_sensor_name, 0644, sprd_get_sensor_name_info,
		   sprd_sensor_set_sensor_name_info);

static int sprd_sensor_file_open(struct inode *inode, struct file *file)
{
	struct sensor_dev_data *sensor = s_sensor_dev_data[0];
	struct sensor_file_ctx *ctx;
	struct sensor_glb *p_data;
	struct miscdevice *misc;
	struct device *dev;
	int ret = 0;

	if (!sensor || !sensor->client) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor misc device not found\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	misc = file->private_data;
	if (!misc) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor misc device not found\n",
		       current->pid, __func__, __LINE__);
		goto err_nomem;
	}

	dev = misc->this_device;
	p_data = dev_get_drvdata(dev);
	if (!p_data) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor: no module data\n",
		       current->pid, __func__, __LINE__);
		goto err_nomem;
	}

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor: no memory\n",
		       current->pid, __func__, __LINE__);
		goto err_nomem;
	}

	if (atomic_inc_return(&p_data->open_count) == 1) {
		ret = sprd_glb_mm_pw_on_cfg();
		__pm_runtime_resume(&sensor->client->dev, RPM_GET_PUT);
		__pm_stay_awake(p_data->ws);
	}

	ctx->private_key_ok = 0;
	ctx->misc = misc;
	ctx->sensor_id = 0xffff;
	ctx->p_data = p_data;
	file->private_data = ctx;

	pr_info("SENSOR_CORE: %d: %s line=%d: open sensor file successfully\n",
		current->pid, __func__, __LINE__);

	return ret;

err_nomem:
	pr_err("SENSOR_CORE: %d: %s line=%d: fail to open sensor file %d\n",
	       current->pid, __func__, __LINE__, -ENOMEM);

	return -ENOMEM;
}

static int sprd_sensor_file_release(struct inode *inode, struct file *file)
{
	struct sensor_dev_data *sensor = s_sensor_dev_data[0];
	struct sensor_file_ctx *ctx = file->private_data;
	struct sensor_glb *p_data;
	int ret = 0;

	if (!sensor || !sensor->client) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor misc device not found\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	if (!ctx || !ctx->p_data)
		return -EINVAL;

	p_data = ctx->p_data;

	sprd_sensor_set_mclk(0, ctx->sensor_id);

	if (ctx->mipi_on == 1)
		pr_info("SENSOR_CORE: %d: %s line=%d: sensor %d mipi close\n",
			current->pid, __func__, __LINE__, ctx->sensor_id);

	if (sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, 0, SENSOR_GPIO_AVDD))
		sprd_sensor_set_voltage(ctx->sensor_id, 0, SENSOR_REG_AVDD);

	if (sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, 0, SENSOR_GPIO_DVDD))
		sprd_sensor_set_voltage(ctx->sensor_id, 0, SENSOR_REG_DVDD);

	if (sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, 0, SENSOR_GPIO_IOVDD))
		ret = sprd_sensor_set_voltage(ctx->sensor_id, 0, SENSOR_REG_IOVDD);

	if (atomic_dec_and_test(&p_data->open_count)) {
		sprd_glb_mm_pw_off_cfg();
		__pm_runtime_idle(&sensor->client->dev, RPM_GET_PUT);
		__pm_relax(p_data->ws);
	}

	ctx->private_key_ok = 0;
	kfree(ctx);
	file->private_data = NULL;

	pr_info("SENSOR_CORE: %d: %s line=%d: sensor: release %d\n",
		current->pid, __func__, __LINE__, ret);

	return ret;
}

/* lock/unlock the per sensor context, used around every ioctl */
void sprd_sensor_sync_lock(u32 sensor_id)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);

	if (p) {
		mutex_lock(&p->sync_lock);
		return;
	}

	pr_err("SENSOR_CORE: %d: %s line=%d: %s, error\n", current->pid,
	       __func__, __LINE__, __func__);
}

void sprd_sensor_sync_unlock(u32 sensor_id)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);

	if (p) {
		mutex_unlock(&p->sync_lock);
		return;
	}

	pr_err("SENSOR_CORE: %d: %s line=%d: %s, error\n", current->pid,
	       __func__, __LINE__, __func__);
}

static int sprd_sensor_io_set_private_key(struct sensor_file_ctx *ctx,
					  unsigned long arg)
{
	u32 key = 0;
	int ret;

	ret = _copy_from_user(&key, (void __user *)arg, sizeof(key));
	if (ret) {
		pr_info("SENSOR_CORE: %d: %s line=%d: copy private_key from user failed.\n",
			current->pid, __func__, __LINE__);
		return ret;
	}

	if (key != SENSOR_PRIVATE_KEY) {
		pr_info("SENSOR_CORE: %d: %s line=%d: sensor set private key faild\n",
			current->pid, __func__, __LINE__);
		return 1;
	}

	ctx->private_key_ok = 1;

	pr_info("SENSOR_CORE: %d: %s line=%d: sensor set private key successfully\n",
		current->pid, __func__, __LINE__);

	return 0;
}

static int sprd_sensor_io_set_sensor_id(struct sensor_file_ctx *ctx,
					unsigned long arg)
{
	int ret;

	mutex_lock(&ctx->p_data->sensor_id_lock);

	ret = _copy_from_user(&ctx->sensor_id, (void __user *)arg,
			      sizeof(ctx->sensor_id));

	pr_debug("SENSOR_CORE: %d: %s line=%d: sensor id %d cmd 0x%x\n",
		 current->pid, __func__, __LINE__, ctx->sensor_id,
		 SENSOR_IO_SET_SENSOR_ID);

	mutex_unlock(&ctx->p_data->sensor_id_lock);

	sprd_sensor_sync_lock(ctx->sensor_id);
	sprd_sensor_sync_unlock(ctx->sensor_id);

	return ret;
}

static int sprd_sensor_mipi_if_switch(struct sensor_file_ctx *ctx, u32 off)
{
	int ret = csi_api_switch(ctx->sensor_id);

	pr_debug("SENSOR_CORE: %d: %s line=%d: switch csi successfully\n",
		 current->pid, __func__, __LINE__);

	return ret;
}

long sprd_sensor_file_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct sensor_file_ctx *ctx = file->private_data;
	u32 value = 0;
	u16 addr = 0;
	int ret = 0;

	/* the private key is the only command accepted before unlocking */
	if (cmd == SENSOR_IO_SET_PRIVATE_KEY)
		return sprd_sensor_io_set_private_key(ctx, arg);

	if (ctx->private_key_ok != 1) {
		pr_err("SENSOR_CORE: %d: %s line=%d: sensor match private key failed, permission deny!\n",
		       current->pid, __func__, __LINE__);
		return 0;
	}

	if (cmd == SENSOR_IO_SET_SENSOR_ID)
		return sprd_sensor_io_set_sensor_id(ctx, arg);

	sprd_sensor_sync_lock(ctx->sensor_id);

	switch (cmd) {
	case SENSOR_IO_SET_AVDD:
		ret = sprd_sensor_io_set_avdd(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_SET_DVDD:
		ret = sprd_sensor_io_set_dvdd(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_SET_IOVDD:
		ret = sprd_sensor_io_set_iovdd(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_SET_MCLK:
		if (!_copy_from_user(&value, (void __user *)arg, sizeof(value)))
			ret = sprd_sensor_set_mclk(value, ctx->sensor_id);
		break;
	case SENSOR_IO_RESET:
		{
			u32 cfg[2] = { 0, 0 };

			if (!_copy_from_user(cfg, (void __user *)arg,
					     sizeof(cfg)))
				ret = sprd_sensor_reset(ctx->sensor_id, cfg[0],
							cfg[1]);
		}
		break;
	case SENSOR_IO_SET_RST_LEVEL:
		if (!_copy_from_user(&value, (void __user *)arg, sizeof(value)))
			ret = sprd_sensor_set_rst_level(ctx->sensor_id, value);
		break;
	case SENSOR_IO_SET_VOLTAGE:
		if (!_copy_from_user(&value, (void __user *)arg, sizeof(value))) {
			int idx = (int)value;
			int vol;

			if (idx >= 13)
				vol = idx * 1000;
			else
				vol = c_sensor_voltage_tab[idx];

			ret = sprd_sensor_set_voltage_by_gpio(ctx->sensor_id,
							      vol,
							      SENSOR_GPIO_MOT_IDX);
			ret &= sprd_sensor_set_voltage(ctx->sensor_id, vol,
						       SENSOR_REG_MOT);
		}
		break;
	case SENSOR_IO_SET_I2C_CLK:
		if (!_copy_from_user(&value, (void __user *)arg, sizeof(value)))
			ret = sprd_sensor_set_i2c_clk(ctx->sensor_id, value);
		break;
	case SENSOR_IO_SET_MIPI_LEVEL:
		if (!_copy_from_user(&value, (void __user *)arg, sizeof(value)))
			ret = sprd_sensor_set_mipi_level(ctx->sensor_id, value);
		break;
	case SENSOR_IO_IF_DUMP:
		pr_info("SENSOR_CORE: %d: %s line=%d: E\n", current->pid,
			__func__, __LINE__);
		csi_reg_dump_flag = 1;
		ret = 0;
		break;
	case SENSOR_IO_SET_I2C_BURST:
		if (_copy_from_user(&value, (void __user *)arg, sizeof(value)))
			break;
		{
			struct sensor_dev_data *p =
				sprd_sensor_get_dev_context(ctx->sensor_id);

			if (!p) {
				pr_err("SENSOR_CORE: %d: %s line=%d: %s, error\n",
				       current->pid, __func__, __LINE__,
				       __func__);
				break;
			}
			p->i2c_burst_mode = value;
			pr_debug("SENSOR_DRV: %d: %s L %d: set i2c %d burst %d\n",
				 current->pid, __func__, __LINE__,
				 ctx->sensor_id, value);
			ret = 0;
		}
		break;
	case SENSOR_IO_SET_PD_LEVEL:
		if (!_copy_from_user(&addr, (void __user *)arg, 1))
			ret = sprd_sensor_set_pd_level(ctx->sensor_id, addr);
		break;
	case SENSOR_IO_SET_I2C_ADDR:
		ret = sprd_sensor_io_set_i2c_addr(ctx, arg);
		break;
	case SENSOR_IO_READ_I2C_REG:
	case SENSOR_IO_READ_I2C_REG64:
		{
			struct sensor_i2c_reg reg = { 0 };

			if (csi_pattern) {
				ret = 0;
				break;
			}
			if (_copy_from_user(&reg, (void __user *)arg,
					    sizeof(reg)))
				break;
			ret = sprd_sensor_read_reg(ctx->sensor_id, &reg);
			if (ret)
				break;
			ret = _copy_to_user((void __user *)arg, &reg,
					    sizeof(reg));
		}
		break;
	case SENSOR_IO_WRITE_I2C_REG:
		{
			struct sensor_i2c_reg reg = { 0 };

			if (csi_pattern) {
				ret = 0;
				break;
			}
			if (_copy_from_user(&reg, (void __user *)arg,
					    sizeof(reg)))
				break;
			ret = sprd_sensor_write_reg(ctx->sensor_id, &reg);
		}
		break;
	case SENSOR_IO_WRITE_I2C_REGS:
		ret = sprd_sensor_io_write_i2c_regs(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_GRC_WRITE_I2C:
		ret = sprd_sensor_io_grc_write_i2c(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_GRC_READ_I2C:
		ret = sprd_sensor_io_grc_read_i2c(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_IF_CFG:
		ret = sprd_sensor_io_if_cfg(ctx, (void __user *)arg);
		break;
	case SENSOR_IO_IF_CFG2:
		{
			u8 buf[0x20];

			memset(buf, 0, sizeof(buf));
			if (!_copy_from_user(buf, (void __user *)arg,
					     sizeof(buf)))
				ret = 0;
			else
				ret = -ENOMEM;
		}
		break;
	case SENSOR_IO_WRITE_MUTI_I2C:
		{
			struct sensor_muti_i2c_tab tab;

			memset(&tab, 0, sizeof(tab));
			ret = _copy_from_user(&tab, (void __user *)arg,
					      sizeof(tab));
			if (csi_pattern) {
				ret = 0;
				break;
			}
			if (ret)
				break;
			ret = sprd_sensor_write_muti_i2c(&tab);
			if (ret)
				break;
			ret = _copy_to_user((void __user *)arg, &tab,
					    sizeof(tab));
		}
		break;
	case SENSOR_IO_MIPI_IF:
		{
			u8 buf[0x5c];

			memset(buf, 0, sizeof(buf));
			ret = _copy_from_user(buf, (void __user *)arg,
					      sizeof(buf));
			if (!ret && !csi_pattern)
				ret = sprd_sensor_mipi_if_switch(ctx,
							   *(u32 *)(buf + 0x20));
		}
		break;
	default:
		break;
	}

	sprd_sensor_sync_unlock(ctx->sensor_id);

	return ret;
}

int compat_sensor_k_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case 0x400c5213:
		cmd = 0x40105213;
		break;
	case 0x40505217:
		cmd = 0x40505217;
		break;
	case 0x4010520e:
		cmd = 0x4018520e;
		break;
	case 0xc00c5214:
		cmd = 0xc0105214;
		break;
	case 0xc02852fe:
		cmd = 0xc04852fe;
		break;
	default:
		break;
	}

	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)(u32)arg);
}

static const struct file_operations sensor_fops = {
	.owner = THIS_MODULE,
	.open = sprd_sensor_file_open,
	.release = sprd_sensor_file_release,
	.unlocked_ioctl = sprd_sensor_file_ioctl,
	.compat_ioctl = compat_sensor_k_ioctl,
};

static struct miscdevice sensor_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sprd_sensor",
	.fops = &sensor_fops,
};

static int __init sprd_sensor_core_module_init(void)
{
	struct sensor_glb *p_data;

	p_data = kmalloc(sizeof(*p_data), GFP_KERNEL);
	if (!p_data)
		return -ENOMEM;

	mutex_init(&p_data->sensor_id_lock);
	p_data->ws = wakeup_source_create("Camera Sensor Wakeup Source");
	wakeup_source_add(p_data->ws);
	atomic_set(&p_data->open_count, 0);

	sprd_sensor_register_driver();

	pr_info("SENSOR_CORE: %d: %s line=%d: sensor register\n", current->pid,
		__func__, __LINE__);

	misc_register(&sensor_dev);

	pr_info("SENSOR_CORE: %d: %s line=%d: create device node\n",
		current->pid, __func__, __LINE__);

	dev_set_drvdata(sensor_dev.this_device, p_data);

	if (device_create_file(sensor_dev.this_device,
			       &dev_attr_camera_sensor_name))
		pr_err("SENSOR_CORE: %d: %s line=%d: fail to create sensor name list file",
		       current->pid, __func__, __LINE__);

	return 0;
}

static void __exit sprd_sensor_core_module_exit(void)
{
	struct sensor_glb *p_data = dev_get_drvdata(sensor_dev.this_device);

	device_remove_file(sensor_dev.this_device, &dev_attr_camera_sensor_name);
	sprd_sensor_unregister_driver();

	if (p_data) {
		wakeup_source_remove(p_data->ws);
		wakeup_source_destroy(p_data->ws);
		kfree(p_data);
	}

	dev_set_drvdata(sensor_dev.this_device, NULL);
	misc_deregister(&sensor_dev);
}

module_init(sprd_sensor_core_module_init);
module_exit(sprd_sensor_core_module_exit);

MODULE_DESCRIPTION("Spreadtrum Camera Sensor Driver");
MODULE_LICENSE("GPL");

static int sprd_sensor_mipi_if_open(struct sensor_file_ctx *ctx,
				    struct sensor_if_cfg *cfg);

/*
 * open or close the mipi interface of one sensor.  The HAL has to hand over
 * open == 1, everything else is ignored.
 */
int sprd_sensor_io_if_cfg(struct sensor_file_ctx *ctx, void __user *arg)
{
	struct sensor_if_cfg cfg;
	int ret;

	memset(&cfg, 0, sizeof(cfg));

	ret = copy_from_user(&cfg, arg, sizeof(cfg));
	if (ret)
		return ret;

	pr_info("SENSOR_CORE: %d: %s line=%d: ret = %d,type %d open %d mipi state %d  is cphy %d\n",
		current->pid, __func__, __LINE__, 0, cfg.type, cfg.open,
		ctx->mipi_on, cfg.use_phy_id);

	if (cfg.open != 1)
		return 0;

	if (cfg.type) {
		/* close request */
		if (ctx->mipi_on != 1) {
			pr_debug("SENSOR_CORE: %d: %s line=%d: mipi already off\n",
				 current->pid, __func__, __LINE__);
			return 0;
		}

		ret = csi_api_close(0, ctx->sensor_id);
		ctx->mipi_on = 0;

		return ret;
	}

	return sprd_sensor_mipi_if_open(ctx, &cfg);
}

/* open the mipi interface, the vendor logs this part with its own name */
static int sprd_sensor_mipi_if_open(struct sensor_file_ctx *ctx,
				    struct sensor_if_cfg *cfg)
{
	int ret;

	if (ctx->mipi_on) {
		pr_debug("SENSOR_CORE: %d: %s line=%d: mipi already on\n",
			 current->pid, __func__, __LINE__);
		return 0;
	}

	csi_pattern = (cfg->priv == SENSOR_IF_PATTERN);

	ret = csi_api_open(cfg->bps_per_lane, cfg->phy_id, cfg->lane_num,
			   ctx->sensor_id, csi_pattern, cfg->use_phy_id,
			   (void *)(unsigned long)cfg->priv);
	if (ret) {
		pr_err("SENSOR_CORE: %d: %s line=%d: fail to open csi %d\n",
		       current->pid, __func__, __LINE__, ret);
		return ret;
	}

	ctx->csi_param = cfg->bps_per_lane;
	ctx->mipi_state = 1;
	ctx->mipi_on = 1;

	pr_info("SENSOR_CORE: %d: %s line=%d: open csi successfully\n",
		current->pid, __func__, __LINE__);

	return 0;
}

/* switch the i2c slave address of one sensor, 0xee selects test pattern */
int sprd_sensor_io_set_i2c_addr(struct sensor_file_ctx *ctx, unsigned long arg)
{
	u16 addr;
	int ret;

	if (_copy_from_user(&addr, (void __user *)arg, sizeof(addr)))
		return -EFAULT;

	if (addr == SENSOR_PATTERN_ADDR) {
		csi_pattern = 1;
		pr_info("SENSOR_CORE: %d: %s line=%d: enable csi testpattern mode!\n",
			current->pid, __func__, __LINE__);
		return 0;
	}

	ret = sprd_sensor_set_i2c_addr(ctx->sensor_id, addr);

	return ret;
}

/* ask the next close() to dump the csi registers */
int sprd_sensor_io_if_dump(struct sensor_file_ctx *ctx)
{
	pr_info("SENSOR_CORE: %d: %s line=%d: dump csi reg\n", current->pid,
		__func__, __LINE__);

	csi_reg_dump_flag = 1;

	return 0;
}
