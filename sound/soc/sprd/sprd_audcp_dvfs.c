// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) AUDCP DVFS debug driver.
 *
 * Reconstructed from the vendor module sprd_audcp_dvfs.ko shipped in the
 * vendor_boot ramdisk of the Sunelan Q968 (UMS9157 / T157).  The binary keeps
 * no source path in .rodata, only the function names and the sysfs attribute
 * names below, so the reconstruction is driven by .text/.rodata/.data only.
 *
 * The driver does not talk to a register block: it exposes the audio DSP
 * (AUDCP) DVFS governor state over sysfs and forwards every read/write to the
 * audio DSP through the SIPC "audio message" channel exported by
 * audio_sipc.ko:
 *
 *	aud_ipc_ch_open()/aud_ipc_ch_close()  - open/close the message channel
 *	aud_send_cmd_result()                 - synchronous command with an
 *	                                        in/out payload of the same size
 *
 * Two attribute groups are created on and removed from the platform device:
 * the unnamed group (a single "status" attribute that just says that the
 * feature is not implemented on this product) and "audcp-governor" (the 25
 * real debug knobs).
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

/* audio DSP message channel API, exported by audio_sipc.ko */
extern int aud_ipc_ch_open(u32 channel);
extern int aud_ipc_ch_close(u32 channel);
extern int aud_send_cmd_result(u32 channel, u32 reserved0, u32 reserved1,
			       u32 command, void *in, u32 size, void *out,
			       int timeout);

/* command ids understood by the AUDCP DVFS governor */
#define AUDCP_CMD_GET_RUNNING_MODE	0x00
#define AUDCP_CMD_SET_RUNNING_MODE	0x01
#define AUDCP_CMD_GET_HW_DVFS_ENABLE	0x02
#define AUDCP_CMD_SET_HW_DVFS_ENABLE	0x03
#define AUDCP_CMD_GET_TABLE_INFO	0x04
#define AUDCP_CMD_GET_INDEX		0x05
#define AUDCP_CMD_SET_INDEX		0x06
#define AUDCP_CMD_GET_IDLE_INDEX	0x07
#define AUDCP_CMD_SET_IDLE_INDEX	0x08
#define AUDCP_CMD_GET_AUTO_DVFS		0x09
#define AUDCP_CMD_SET_AUTO_DVFS		0x0a
#define AUDCP_CMD_GET_FORCE_DVFS	0x0b
#define AUDCP_CMD_SET_FORCE_DVFS	0x0c
#define AUDCP_CMD_GET_HOLD_FSM		0x0d
#define AUDCP_CMD_SET_HOLD_FSM		0x0e
#define AUDCP_CMD_GET_SYS_BUSY		0x0f
#define AUDCP_CMD_GET_WINDOW_CNT	0x10
#define AUDCP_CMD_GET_STATUS		0x11
#define AUDCP_CMD_GET_CURRENT_VOLTAGE	0x12
#define AUDCP_CMD_GET_VOTE_VOLTAGE	0x13
#define AUDCP_CMD_GET_FIXED_VOLTAGE	0x14
#define AUDCP_CMD_SET_FIXED_VOLTAGE	0x15
#define AUDCP_CMD_GET_VOLTAGE_MEET	0x16
#define AUDCP_CMD_GET_VOLTAGE_MEET_BYPASS 0x17
#define AUDCP_CMD_SET_VOLTAGE_MEET_BYPASS 0x18
#define AUDCP_CMD_GET_IDLE_VOLTAGE	0x19
#define AUDCP_CMD_SET_IDLE_VOLTAGE	0x1a
#define AUDCP_CMD_GET_SW_TUNE		0x1b
#define AUDCP_CMD_SET_SW_TUNE		0x1c
#define AUDCP_CMD_GET_SW_DVFS		0x1d
#define AUDCP_CMD_SET_SW_DVFS		0x1e
#define AUDCP_CMD_GET_RUNNING_RECORD	0x1f
#define AUDCP_CMD_GET_DVFS_REGISTER	0x20
#define AUDCP_CMD_SET_DVFS_REGISTER	0x21

/* "wait forever" timeout for the SIPC transaction */
#define AUDCP_IPC_TIMEOUT		(-1)

#define AUDCP_MAX_TABLE_INDEX		8
#define AUDCP_MAX_VOLTAGE_INDEX		3
#define AUDCP_MAX_CLOCK_INDEX		6
#define AUDCP_MAX_RECORD		8
#define AUDCP_RECORD_HISTORY		20

struct audcp_dvfs {
	struct device *dev;		/* 0x00 */
	const char *name;		/* 0x08, "compatible" property */
	u32 reserved_10;		/* 0x10 */
	u32 reserved_14;		/* 0x14 */
	u32 reserved_18;		/* 0x18 */
	u32 dvfs_register;		/* 0x1c */
	u32 fixed_voltage_enable;	/* 0x20 */
	u32 fixed_voltage_valid;	/* 0x24 */
	u32 channel;			/* 0x28, "sprd,channel" */
	u32 ipc_on;			/* 0x2c */
	u32 enable;			/* 0x30 */
	u32 reserved_34;		/* 0x34 */
};

/*
 * Voltage table reported by the governor.  The vendor stores the value in two
 * "int.frac" words and prints them with "%u.%u", the numbers are kept as they
 * are found in .rodata (0x8c0) so that the sysfs output is bit identical.
 */
static const u32 dvfs_voltage[AUDCP_MAX_VOLTAGE_INDEX][2] = {
	{ 7, 0 },
	{ 75, 0 },
	{ 8, 26 },
};

/* clock table, same encoding (0x8d8, 6 entries of 8 bytes) */
static const u32 dvfs_clock[AUDCP_MAX_CLOCK_INDEX][2] = {
	{ 0, 153 },
	{ 6, 256 },
	{ 0, 384 },
	{ 0, 512 },
	{ 614, 0 },
	{ 0, 0 },
};

/* governor running modes, 0x908 */
static const char *const running_mode_table[] = {
	"ADCP_RUNNING_DEEP_SLEEP",
	"AUDCP_RUNNING_LIGHT_SLEEP",
	"AUDCP_RUNNING_NO_SLEEP",
	"AUDCP_RUNNING_NORMAL",
};

/* one entry of the table_info payload (stride 0xc) */
struct audcp_table_entry {
	u32 index;
	u32 voltage;
	u32 clock;
};

/* table_info payload: a count, 4 bytes of padding and up to 8 entries */
struct audcp_table_info {
	u32 count;
	u32 reserved;
	struct audcp_table_entry entry[AUDCP_MAX_TABLE_INDEX];
};

/* one entry of the running_record payload (stride 8) */
struct audcp_running_entry {
	u32 first;
	u32 second;
};

/* running_record payload: count, 8 records and 20 words of history */
struct audcp_running_record {
	u32 count;
	u32 reserved;
	struct audcp_running_entry record[AUDCP_MAX_RECORD];
	u32 history[AUDCP_RECORD_HISTORY];
};

/*
 * Make sure the AUDCP message channel is up.  The vendor open codes this block
 * in front of every single attribute callback; it is factored out here, the
 * log order is preserved (the "ipc enabled" line is only printed when the
 * channel was already open or when it could be opened).
 */
static int audcp_dvfs_ipc_get(struct device *dev, struct audcp_dvfs *dvfs)
{
	int ret;

	if (dvfs->ipc_on)
		goto enabled;

	ret = aud_ipc_ch_open(dvfs->channel);
	if (ret) {
		dev_err(dev, "channel(%u)_open failed\n", dvfs->channel);
		if (ret < 0) {
			dev_err(dev, "enable audcp ipc failed\n");
			return ret;
		}
		return 0;
	}
	dvfs->ipc_on = 1;

enabled:
	dev_info(dev, "audcp dvfs ipc enabled, channel = %d\n", dvfs->channel);
	return 0;
}

static int audcp_dvfs_send(struct audcp_dvfs *dvfs, u32 command, void *in,
			   u32 size, void *out)
{
	return aud_send_cmd_result(dvfs->channel, 0, 0, command, in, size, out,
				   AUDCP_IPC_TIMEOUT);
}

static int audcp_dvfs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct audcp_dvfs *dvfs;
	int ret;

	dvfs = devm_kmalloc(&pdev->dev, sizeof(*dvfs), GFP_KERNEL | __GFP_ZERO);
	if (!dvfs)
		return -ENOMEM;

	ret = of_property_read_string(np, "compatible", &dvfs->name);
	if (ret) {
		dev_err(&pdev->dev, "fail to get name\n");
		dev_err(&pdev->dev, "failed parse dts\n");
		return ret;
	}

	ret = of_property_read_variable_u32_array(np, "sprd,channel",
						  &dvfs->channel, 1, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "sprd,channel,core failed\n");
		dev_err(&pdev->dev, "failed parse dts\n");
		return ret;
	}

	platform_set_drvdata(pdev, dvfs);

	ret = sysfs_create_groups(&pdev->dev, audcp_groups);
	if (ret) {
		dev_err(&pdev->dev,
			"audio failed to create audcpdvfs_sys device attributes\n");
		return ret;
	}

	dvfs->dev = &pdev->dev;
	return 0;
}

static int audcp_dvfs_remove(struct platform_device *pdev)
{
	sysfs_remove_groups(&pdev->dev, audcp_groups);
	return 0;
}

static ssize_t dvfs_register_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = dvfs->dvfs_register;
	u32 out[2] = { 0, 0 };
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_DVFS_REGISTER, &in,
			      sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x:%#x\n", out[0], out[1]);
}

static ssize_t dvfs_register_store(struct device *dev,
				   struct device_attribute *attr, const char *buf,
				   size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out = 0;
	u32 high, low;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u:%u\n", &high, &low) != 2) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	dev_warn(dev, "audio cp dvfs it may be a danger hols, just for debug !!!!!");

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_DVFS_REGISTER, in,
			      sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	dvfs->dvfs_register = out;
	return count;
}

static ssize_t running_record_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	struct audcp_running_record in = { 0 };
	struct audcp_running_record out;
	int ret, i, len;
	u32 n;

	memset(&out, 0, sizeof(out));
	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_RUNNING_RECORD, &in,
			      sizeof(out), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	n = out.count;
	if (n >= AUDCP_MAX_RECORD)
		return sprintf(buf, "invalid total index count\n");

	len = sprintf(buf, "index ---- times\n");
	for (i = 0; i < n; i++)
		len += sprintf(buf + len, "%u---- %u\n", out.record[i].second,
			       out.record[i].first);

	len += sprintf(buf + len, "index changed history\n");
	for (i = 0; i < AUDCP_RECORD_HISTORY; i++)
		len += sprintf(buf + len, "%u ", out.history[i]);
	buf[len++] = '\n';

	return len;
}

static ssize_t sw_dvfs_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_SW_DVFS, &in, sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t sw_dvfs_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_SW_DVFS, &in, sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t sw_tune_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_SW_TUNE, &in, sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t sw_tune_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_SW_TUNE, &in, sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t audcp_idle_voltage_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_IDLE_VOLTAGE, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t audcp_idle_voltage_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_IDLE_VOLTAGE, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t voltage_meet_bypass_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_VOLTAGE_MEET_BYPASS, &in,
			      sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t voltage_meet_bypass_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_VOLTAGE_MEET_BYPASS, &in,
			      sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t voltage_meet_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_VOLTAGE_MEET, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t fixed_voltage_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[3] = { 0, 0, 0 };
	u32 out[3] = { 0, 0, 0 };
	int ret;
	u32 index;

	if (!dvfs->fixed_voltage_enable)
		return sprintf(buf, "please enable fix_voltage_enable first\n");
	if (!dvfs->fixed_voltage_valid)
		return sprintf(buf, "please set fixed valtatge first\n");

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	in[0] = 1;
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_FIXED_VOLTAGE, in, sizeof(in),
			      out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	index = out[1];
	if (index >= AUDCP_MAX_VOLTAGE_INDEX)
		return sprintf(buf, "invalid voltage index from audio cp %u\n",
			       index);

	return sprintf(buf, "%u.%u\n", dvfs_voltage[index][0],
		       dvfs_voltage[index][1]);
}

static ssize_t fixed_voltage_store(struct device *dev,
				   struct device_attribute *attr, const char *buf,
				   size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[3] = { 0, 0, 0 };
	u32 out[3] = { 0, 0, 0 };
	u32 index;
	int ret;

	if (!dvfs->fixed_voltage_enable) {
		dev_err(dev, "please enable fix_voltage_enable first\n");
		return -EINVAL;
	}

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &index) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	if (index >= AUDCP_MAX_VOLTAGE_INDEX) {
		dev_err(dev, "invalid voltage index\n");
		return -EINVAL;
	}

	in[0] = 1;
	in[1] = index;
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_FIXED_VOLTAGE, in, sizeof(in),
			      out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	dvfs->fixed_voltage_valid = 1;
	return count;
}

static ssize_t fixed_voltage_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", dvfs->fixed_voltage_enable);
}

static ssize_t fixed_voltage_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 val;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	dvfs->fixed_voltage_enable = val;
	return count;
}

static ssize_t voltage_table_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int len;

	len = sprintf(buf, "%u == %u.%u\n", 0, 0, 7);
	len += sprintf(buf + len, "%u == %u.%u\n", 1, 0, 75);
	len += sprintf(buf + len, "%u == %u.%u\n", 2, 0, 8);

	return len;
}

static ssize_t vote_voltage_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;
	u32 index;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_VOTE_VOLTAGE, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	index = out;
	if (index >= AUDCP_MAX_VOLTAGE_INDEX)
		return sprintf(buf, "invalid  voltage index %d\n", index);

	return sprintf(buf, "%u.%u\n", dvfs_voltage[index][0],
		       dvfs_voltage[index][1]);
}

static ssize_t current_voltage_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;
	u32 index;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_CURRENT_VOLTAGE, &in,
			      sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	index = out;
	if (index >= AUDCP_MAX_VOLTAGE_INDEX + 1)
		return sprintf(buf, "invalid voltage index %d\n", index);

	return sprintf(buf, "%u.%u\n", dvfs_voltage[index][0],
		       dvfs_voltage[index][1]);
}

static ssize_t dvfs_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_STATUS, &in, sizeof(in), &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t window_cnt_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_WINDOW_CNT, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%#x\n", out);
}

static ssize_t audcp_sys_busy_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_SYS_BUSY, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%u\n", out);
}

static ssize_t hold_fsm_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_HOLD_FSM, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%u\n", out);
}

static ssize_t hold_fsm_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_HOLD_FSM, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t force_dvfs_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_FORCE_DVFS, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%u\n", out);
}

static ssize_t force_dvfs_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_FORCE_DVFS, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t auto_dvfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_AUTO_DVFS, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%u\n", out[0]);
}

static ssize_t auto_dvfs_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in[0] = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_AUTO_DVFS, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t idle_index_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_IDLE_INDEX, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "index %u\n", out[0]);
}

static ssize_t idle_index_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	u32 index;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &index) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	if (index >= AUDCP_MAX_TABLE_INDEX) {
		dev_err(dev, "invalid index\n");
		return -EINVAL;
	}

	in[0] = index;
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_IDLE_INDEX, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t index_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_INDEX, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "index %u\n", out[0]);
}

static ssize_t index_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	u32 index;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &index) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	if (index >= AUDCP_MAX_TABLE_INDEX) {
		dev_err(dev, "invalid index\n");
		return -EINVAL;
	}

	in[0] = index;
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_INDEX, in, sizeof(in), out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t table_info_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	struct audcp_table_info in;
	struct audcp_table_info out;
	struct audcp_table_entry *entry;
	int ret, i, len;
	u32 n;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_TABLE_INFO, &in, sizeof(out),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	n = out.count;
	if (n >= AUDCP_MAX_TABLE_INDEX) {
		dev_err(dev, "invalid index count\n");
		return -EINVAL;
	}

	len = sprintf(buf, "index count=%u\n", n);
	for (i = 0; i < n; i++) {
		entry = &out.entry[i];
		len += sprintf(buf + len, "index-%d => %u.%uv:%u.%uMhz\n",
			       entry->index,
			       dvfs_voltage[entry->voltage][0],
			       dvfs_voltage[entry->voltage][1],
			       dvfs_clock[entry->clock][0],
			       dvfs_clock[entry->clock][1]);
	}

	return len;
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", dvfs->enable);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 val;
	int ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	if (!val) {
		if (dvfs->ipc_on) {
			ret = aud_ipc_ch_close(dvfs->channel);
			if (ret < 0) {
				dev_err(dev, "channel(%u) close failed\n",
					dvfs->channel);
				dev_err(dev, "disable audcp ipc failed\n");
				return ret;
			}
			dvfs->ipc_on = 0;
		}
		dvfs->enable = 0;
		return count;
	}

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	dvfs->enable = 1;
	return count;
}

static ssize_t hw_dvfs_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_HW_DVFS_ENABLE, in, sizeof(in),
			      out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return sprintf(buf, "%u\n", out[0]);
}

static ssize_t hw_dvfs_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in[2] = { 0, 0 };
	u32 out[2] = { 0, 0 };
	u32 val;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &val) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	in[0] = (val != 0);
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_HW_DVFS_ENABLE, in, sizeof(in),
			      out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t audcp_running_mode_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in = 0, out = 0;
	int ret;
	u32 mode;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_GET_RUNNING_MODE, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	mode = out;
	if (mode >= 5) {
		dev_err(dev, "audio cp returned invalid running mode\n");
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", running_mode_table[mode]);
}

static ssize_t audcp_running_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct audcp_dvfs *dvfs = dev_get_drvdata(dev);
	u32 in, out = 0;
	u32 mode;
	int ret;

	ret = audcp_dvfs_ipc_get(dev, dvfs);
	if (ret)
		return ret;

	if (sscanf(buf, "%u\n", &mode) != 1) {
		dev_err(dev, "failed get val\n");
		return -EINVAL;
	}

	if (mode >= 5) {
		dev_err(dev, "invalid running mode\n");
		return -EINVAL;
	}

	in = mode;
	ret = audcp_dvfs_send(dvfs, AUDCP_CMD_SET_RUNNING_MODE, &in, sizeof(in),
			      &out);
	if (ret) {
		dev_err(dev, "aud_send_cmd failed\n");
		return ret;
	}

	return count;
}

static ssize_t audcp_running_mode_table_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int len;

	len = sprintf(buf, "%u-----%s\n", 0, "ADCP_RUNNING_DEEP_SLEEP");
	len += sprintf(buf + len, "%u-----%s\n", 1, "AUDCP_RUNNING_LIGHT_SLEEP");
	len += sprintf(buf + len, "%u-----%s\n", 2, "AUDCP_RUNNING_NO_SLEEP");
	len += sprintf(buf + len, "%u-----%s\n", 3, "AUDCP_RUNNING_NORMAL");

	return len;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "audio cp not implement\n");
}

static DEVICE_ATTR_RW(dvfs_register);
static DEVICE_ATTR_RO(running_record);
static DEVICE_ATTR_RW(sw_dvfs);
static DEVICE_ATTR_RW(sw_tune);
static DEVICE_ATTR_RW(audcp_idle_voltage);
static DEVICE_ATTR_RW(voltage_meet_bypass);
static DEVICE_ATTR_RO(voltage_meet);
static DEVICE_ATTR_RW(fixed_voltage);
static DEVICE_ATTR_RW(fixed_voltage_enable);
static DEVICE_ATTR_RO(voltage_table);
static DEVICE_ATTR_RO(vote_voltage);
static DEVICE_ATTR_RO(current_voltage);
static DEVICE_ATTR_RO(dvfs_status);
static DEVICE_ATTR_RO(window_cnt);
static DEVICE_ATTR_RO(audcp_sys_busy);
static DEVICE_ATTR_RW(hold_fsm);
static DEVICE_ATTR_RW(force_dvfs);
static DEVICE_ATTR_RW(auto_dvfs);
static DEVICE_ATTR_RW(idle_index);
static DEVICE_ATTR_RW(index);
static DEVICE_ATTR_RO(table_info);
static DEVICE_ATTR_RW(enable);
static DEVICE_ATTR_RW(hw_dvfs_enable);
static DEVICE_ATTR_RW(audcp_running_mode);
static DEVICE_ATTR_RO(audcp_running_mode_table);
static DEVICE_ATTR_RO(status);

static struct attribute *audcp_sys_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

static struct attribute *audcp_governor_attrs[] = {
	&dev_attr_audcp_running_mode_table.attr,
	&dev_attr_audcp_running_mode.attr,
	&dev_attr_hw_dvfs_enable.attr,
	&dev_attr_enable.attr,
	&dev_attr_table_info.attr,
	&dev_attr_index.attr,
	&dev_attr_idle_index.attr,
	&dev_attr_auto_dvfs.attr,
	&dev_attr_force_dvfs.attr,
	&dev_attr_hold_fsm.attr,
	&dev_attr_audcp_sys_busy.attr,
	&dev_attr_window_cnt.attr,
	&dev_attr_dvfs_status.attr,
	&dev_attr_current_voltage.attr,
	&dev_attr_vote_voltage.attr,
	&dev_attr_voltage_table.attr,
	&dev_attr_fixed_voltage_enable.attr,
	&dev_attr_fixed_voltage.attr,
	&dev_attr_voltage_meet.attr,
	&dev_attr_voltage_meet_bypass.attr,
	&dev_attr_audcp_idle_voltage.attr,
	&dev_attr_sw_tune.attr,
	&dev_attr_sw_dvfs.attr,
	&dev_attr_running_record.attr,
	&dev_attr_dvfs_register.attr,
	NULL,
};

static struct attribute_group audcp_sys_group = {
	.attrs = audcp_sys_attrs,
};

static struct attribute_group audcp_governor_group = {
	.name = "audcp-governor",
	.attrs = audcp_governor_attrs,
};

static const struct attribute_group *audcp_groups[] = {
	&audcp_sys_group,
	&audcp_governor_group,
	NULL,
};

static const struct of_device_id audcp_dvfs_match[] = {
	{ .compatible = "unisoc,sharkl5-audcp-dvfs" },
	{ .compatible = "unisoc,qogirl6-audcp-dvfs" },
	{ .compatible = "unisoc,qogirn6pro-audcp-dvfs" },
	{ },
};
MODULE_DEVICE_TABLE(of, audcp_dvfs_match);

static struct platform_driver audcp_dvfs_driver = {
	.probe = audcp_dvfs_probe,
	.remove = audcp_dvfs_remove,
	.driver = {
		.name = "sprd_audcp_dvfs",
		.of_match_table = audcp_dvfs_match,
	},
};

static int __init audcp_dvfs_init(void)
{
	return platform_driver_register(&audcp_dvfs_driver);
}

static void __exit audcp_dvfs_exit(void)
{
	platform_driver_unregister(&audcp_dvfs_driver);
}

module_init(audcp_dvfs_init);
module_exit(audcp_dvfs_exit);

MODULE_AUTHOR("Lei Ning <lei.ning@unisoc.com>");
MODULE_DESCRIPTION("dvfs driver for audio cp");
MODULE_LICENSE("GPL v2");
