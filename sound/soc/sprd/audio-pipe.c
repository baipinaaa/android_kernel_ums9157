// SPDX-License-Identifier: GPL-2.0
/*
 * audio-pipe.c - Unisoc SIPC based audio pipe driver
 *
 * Reconstructed from the vendor binary module `audio-pipe.ko`
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50, .text 0x16f8 = 5880 bytes,
 *  14 functions, module name `audio_pipe`).
 *
 * modinfo of the binary:
 *   author=SPRD
 *   description=SIPC/AUDIO_PIPE driver
 *   license=GPL
 *   depends=audio_sipc
 *
 * Build path embedded in .rodata:
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd_audio/audiopipe/audio-pipe.c
 *
 * One platform device per pipe.  Every pipe exports a misc character device
 * whose name is fixed by its compatible string, and it exchanges 20 byte
 * messages (struct aud_smsg) with the audio DSP through the `audio_sipc`
 * module:
 *
 *   unisoc,audio_pipe_voice        -> "audio_pipe_voice"        type 0
 *   unisoc,audio_pipe_effect       -> "audio_pipe_effect"       type 1
 *   unisoc,audio_pipe_recordproc   -> "audio_pipe_recordproc"   type 2
 *   unisoc,audio_pipe_bthal        -> "audio_pipe_bthal"        type 3
 *   unisoc,audio_pipe_trigger_rx   -> "audio_pipe_trigger_rx"   type 4
 *   unisoc,audio_pipe_trigger_tx   -> "audio_pipe_trigger_tx"   type 5
 *
 * write() accepts either one 20 byte command (count == sizeof(struct aud_smsg)
 * and !writesync, sent with aud_send_cmd_no_wait()) or a raw buffer of up to
 * maxuserwritebufsize bytes (copied into a vmalloc'ed bounce buffer and sent
 * with aud_send_cmd()).  read() always takes exactly 20 bytes and blocks in
 * aud_smsg_recv() until the DSP answers on the pipe channel.
 *
 * ---------------------------------------------------------------------------
 * Binary layout of struct aud_pipe_data (devm_kmalloc(&pdev->dev, 0xc8, ...))
 *   0x00 struct miscdevice misc;          (0x50 bytes)
 *   0x50 char              name[0x40];    ("audio_pipe_voice", ...)
 *   0x90 int               open_count;
 *   0x94 u32               channel;       (sprd,channel)
 *   0x98 void             *buf;           (vmalloc(maxuserwritebufsize))
 *   0xa0 u32               writesync;     (sprd,writesync)
 *   0xa4 u32               maxuserwritebufsize;
 *   0xa8 struct mutex      mutex;         ("&aud_pipe_dev->mutex")
 *   total 0xc8
 *
 * ioctl command set (type 'A'), decoded from the dispatch in aud_pipe_ioctl():
 *   0x40044100 _IOW('A', 0, int)  AUD_PIPE_IOC_WAKEUP_CH  aud_smsg_wakeup_ch(1, channel)
 *   0x40044101 _IOW('A', 1, int)  AUD_PIPE_IOC_BTHAL_SET  g_bthal_state = arg
 *   0x80044103 _IOR('A', 3, int)  AUD_PIPE_IOC_BTHAL_GET  *arg = g_bthal_state
 * (there is no command number 2)
 * ---------------------------------------------------------------------------
 */

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define AUD_PIPE_MISC_PREFIX	"audio_pipe_"
#define AUD_PIPE_MAX_NAME	0x40
#define AUD_PIPE_DRV_NAME	"sprd_audio_pipe"

/* SIPC destination used by the audio pipe: 1 (audio DSP) */
#define AUD_PIPE_SIPC_DST	1
/* timeout passed to aud_smsg_recv()/aud_send_cmd(): -1 -> wait forever */
#define AUD_PIPE_TIMEOUT	(-1)

/* pipe flavours, the value is the .data field of the of_device_id table */
enum aud_pipe_type {
	AUD_PIPE_VOICE = 0,
	AUD_PIPE_EFFECT,
	AUD_PIPE_RECORDPROC,
	AUD_PIPE_BTHAL,
	AUD_PIPE_TRIGGER_RX,
	AUD_PIPE_TRIGGER_TX,
	AUD_PIPE_MAX,
};

/* 20 byte SIPC message, field order verified against the debug printks */
struct aud_smsg {
	u16 command;
	u16 channel;
	u32 parameter[4];
};

struct aud_pipe_data {
	struct miscdevice misc;
	char name[AUD_PIPE_MAX_NAME];
	int open_count;
	u32 channel;
	void *buf;
	u32 writesync;
	u32 maxuserwritebufsize;
	struct mutex mutex;
};

/* exported by audio_sipc.ko ------------------------------------------------ */
extern int aud_ipc_ch_open(u32 channel);
extern int aud_ipc_ch_close(u32 channel);
/*
 * aud_send_cmd(): the binary sets x0=channel, w1=command, w2=parameter0,
 * w3=parameter1, x4=data, x5=length, w6=timeout.
 */
extern int aud_send_cmd(u16 channel, u16 command, u32 parameter0, u32 parameter1,
			void *data, size_t length, int timeout);
extern int aud_send_cmd_no_wait(u16 channel, u16 command, u32 parameter0,
				u32 parameter1, u32 parameter2, u32 parameter3);
extern int aud_smsg_recv(u8 dst, struct aud_smsg *msg, int timeout);
extern int aud_smsg_wakeup_ch(u8 dst, u16 channel);

/* BT HAL state, .data+0xf0, initialised to 1 by the binary */
static int g_bthal_state = 1;

#define AUD_PIPE_IOC_WAKEUP_CH	_IOW('A', 0, int)
#define AUD_PIPE_IOC_BTHAL_SET	_IOW('A', 1, int)
#define AUD_PIPE_IOC_BTHAL_GET	_IOR('A', 3, int)

/*
 * Byte wise copy used by both read() and write() when the user buffer is not
 * naturally aligned: the vendor code does one copy_{to,from}_user() per byte
 * instead of using the unaligned slow path of the kernel helpers.
 */
static unsigned long aud_pipe_copy_to_user_unalign(void __user *to,
						   const void *from,
						   unsigned long n)
{
	unsigned long left = 0;

	while (n--)
		left |= __copy_to_user(to++, from++, 1);

	return left;
}

static unsigned long aud_pipe_copy_from_user_unalign(void *to,
						     const void __user *from,
						     unsigned long n)
{
	unsigned long left = 0;

	while (n--)
		left |= __copy_from_user(to++, from++, 1);

	return left;
}

/*
 * Receive one command from the DSP on @channel.  ENODATA on channel 2 (the
 * voice pipe) is not an error, the driver fabricates command 0x27 so that the
 * reader still gets a wake up event.
 */
static int aud_pipe_recv_cmd(u32 channel, struct aud_smsg *msg)
{
	struct aud_smsg rmsg;
	int ret;

	memset(&rmsg, 0, sizeof(rmsg));
	rmsg.channel = (u16)channel;

	ret = aud_smsg_recv(AUD_PIPE_SIPC_DST, &rmsg, AUD_PIPE_TIMEOUT);
	if (ret < 0) {
		if ((u16)channel == 2 && ret == -EPIPE) {
			printk(KERN_ERR "[Audio:PIPE] %s, Failed to recv,ret(%d) and notify\n",
				 __func__, ret);
			rmsg.command = 0x27;
			rmsg.parameter[0] = 0x27;
		} else if (ret == -ENODATA) {
			printk(KERN_WARNING "[Audio:PIPE] %s  ENODATA\n", __func__);
			return ret;
		} else {
			printk(KERN_ERR "[Audio:PIPE] %s, Failed to recv,ret(%d)\n", __func__, ret);
			return ret;
		}
	}

	pr_debug("[Audio:PIPE] %s, chan: 0x%x, cmd: 0x%x\n", __func__,
		 rmsg.channel, rmsg.command);
	pr_debug("[Audio:PIPE] value0: 0x%x, value1: 0x%x\n",
		 rmsg.parameter[0], rmsg.parameter[1]);
	pr_debug("[Audio:PIPE]  value2: 0x%x, value3: 0x%x, timeout: %d\n",
		 rmsg.parameter[2], rmsg.parameter[3], AUD_PIPE_TIMEOUT);

	if (rmsg.channel != (u16)channel) {
		printk(KERN_ERR "[Audio:PIPE] %s,wrong chan(0x%x) from agdsp\n", __func__,
			 rmsg.channel);
		return ret;
	}

	*msg = rmsg;
	return 0;
}

static int aud_pipe_open(struct inode *inode, struct file *file)
{
	struct aud_pipe_data *aud_pipe_data = file->private_data;
	int ret = 0;

	mutex_lock(&aud_pipe_data->mutex);

	if (aud_pipe_data->open_count) {
		aud_pipe_data->open_count++;
		goto out;
	}

	ret = aud_ipc_ch_open(aud_pipe_data->channel);
	if (ret) {
		printk(KERN_ERR "[Audio:PIPE] %s channel(%u)_open failed\n", __func__,
			 aud_pipe_data->channel);
		goto out;
	}

	aud_pipe_data->open_count++;
	printk(KERN_INFO "[Audio:PIPE] %s channel opened %u\n", __func__, aud_pipe_data->channel);

	if (aud_pipe_data->maxuserwritebufsize) {
		aud_pipe_data->buf =
			vmalloc(aud_pipe_data->maxuserwritebufsize);
		if (!aud_pipe_data->buf) {
			aud_ipc_ch_close(aud_pipe_data->channel);
			printk(KERN_ERR "[Audio:PIPE] %s memory alloc  error\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
	}

out:
	mutex_unlock(&aud_pipe_data->mutex);
	return ret;
}

static int aud_pipe_release(struct inode *inode, struct file *file)
{
	struct aud_pipe_data *aud_pipe_data = file->private_data;
	int ret = 0;

	mutex_lock(&aud_pipe_data->mutex);

	if (aud_pipe_data->open_count) {
		if (--aud_pipe_data->open_count)
			goto out;

		ret = aud_ipc_ch_close(aud_pipe_data->channel);
		if (ret)
			printk(KERN_ERR "[Audio:PIPE] %s, Failed to close channel(%d)\n", __func__,
				 aud_pipe_data->channel);

		if (aud_pipe_data->buf)
			vfree(aud_pipe_data->buf);

		file->private_data = NULL;
	}

out:
	mutex_unlock(&aud_pipe_data->mutex);
	return ret;
}

static ssize_t aud_pipe_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct aud_pipe_data *aud_pipe_data = file->private_data;
	struct aud_smsg msg;
	ssize_t ret = sizeof(msg);

	if (count != sizeof(msg)) {
		printk(KERN_ERR "[Audio:PIPE] input not a struct aud_smsg type\n");
		ret = -EINVAL;
		goto out;
	}

	memset(&msg, 0, sizeof(msg));

	if (aud_pipe_recv_cmd(aud_pipe_data->channel, &msg)) {
		/*
		 * vendor: the message is still copied to user space and the
		 * error code is returned afterwards
		 */
		ret = -EINVAL;
		printk(KERN_ERR "[Audio:PIPE] %s aud_pipe_recv_cmd failed\n", __func__);
		goto copy;
	}

copy:
	if ((unsigned long)buf & 7) {
		if (aud_pipe_copy_to_user_unalign(buf, &msg, sizeof(msg))) {
			printk(KERN_ERR "[Audio:PIPE] %s: failed to copy to user!\n", __func__);
			ret = -EFAULT;
			goto out;
		}
	} else {
		if (copy_to_user(buf, &msg, sizeof(msg))) {
			printk(KERN_ERR "[Audio:PIPE] %s: failed to copy to user!\n", __func__);
			ret = -EFAULT;
			goto out;
		}
	}

	printk(KERN_INFO "[Audio:PIPE] user_msg_out.channel=%#hx\n", msg.channel);
	printk(KERN_INFO "[Audio:PIPE] user_msg_out.command=%#hx\n", msg.command);
	printk(KERN_INFO "[Audio:PIPE] user_msg_out.parameter0=%#x\n", msg.parameter[0]);
	printk(KERN_INFO "[Audio:PIPE] user_msg_out.parameter1=%#x\n", msg.parameter[1]);
	printk(KERN_INFO "[Audio:PIPE] user_msg_out.parameter2=%#x\n", msg.parameter[2]);
	printk(KERN_INFO "[Audio:PIPE] user_msg_out.parameter3=%#x\n", msg.parameter[3]);

out:
	return ret;
}

static ssize_t aud_pipe_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct aud_pipe_data *aud_pipe_data = file->private_data;
	struct aud_smsg msg;
	ssize_t ret;
	int err;

	printk(KERN_INFO "[Audio:PIPE] %s:aud_pipe_data->channel:%d,count:%zu\n", __func__,
		  aud_pipe_data->channel, count);

	if (file->f_flags & O_NONBLOCK)
		printk(KERN_INFO "[Audio:PIPE] %s noblock %u,timeout = %d\n", __func__,
			  file->f_flags & O_NONBLOCK, 0);

	memset(&msg, 0, sizeof(msg));

	/* single message and no write synchronisation: fire and forget */
	if (count == sizeof(msg) && !aud_pipe_data->writesync) {
		if ((unsigned long)buf & 7) {
			if (aud_pipe_copy_from_user_unalign(&msg, buf,
							    sizeof(msg)))
				goto err_copy;
		} else if (copy_from_user(&msg, buf, sizeof(msg))) {
			goto err_copy;
		}

		printk(KERN_INFO "[Audio:PIPE] user_msg_in.channel=%#hx\n", msg.channel);
		printk(KERN_INFO "[Audio:PIPE] user_msg_in.command=%#hx\n", msg.command);
		printk(KERN_INFO "[Audio:PIPE] user_msg_in.parameter0=%#x\n", msg.parameter[0]);
		printk(KERN_INFO "[Audio:PIPE] user_msg_in.parameter1=%#x\n", msg.parameter[1]);
		printk(KERN_INFO "[Audio:PIPE] user_msg_in.parameter2=%#x\n", msg.parameter[2]);
		printk(KERN_INFO "[Audio:PIPE] user_msg_in.parameter3=%#x\n", msg.parameter[3]);

		err = aud_send_cmd_no_wait(aud_pipe_data->channel, msg.command,
					   msg.parameter[0], msg.parameter[1],
					   msg.parameter[2], msg.parameter[3]);
		if (err) {
			printk(KERN_ERR "[Audio:PIPE] %s aud_send_msg failed\n", __func__);
			ret = -1;
			goto err;
		}

		return sizeof(msg);
	}

	/* streaming write: bounce through aud_pipe_data->buf */
	if (aud_pipe_data->maxuserwritebufsize < count) {
		printk(KERN_ERR "[Audio:PIPE] %s failed unalign_copy_from_user count:%zu,maxwritesize:%d\n",
			 __func__, count, aud_pipe_data->maxuserwritebufsize);
		ret = -EFAULT;
		goto err;
	}

	if ((((unsigned long)aud_pipe_data->buf ^ (unsigned long)buf) & 7) == 0 &&
	    !((unsigned long)aud_pipe_data->buf & 7)) {
		if (copy_from_user(aud_pipe_data->buf, buf, count))
			goto err_copy;
	} else if (aud_pipe_copy_from_user_unalign(aud_pipe_data->buf, buf,
						   count)) {
		goto err_copy;
	}

	err = aud_send_cmd(aud_pipe_data->channel, 0, -1, 0,
			   aud_pipe_data->buf, count, AUD_PIPE_TIMEOUT);
	if (err) {
		printk(KERN_ERR "[Audio:PIPE] %s aud_send_cmd failed\n", __func__);
		ret = -1;
		goto err;
	}

	printk(KERN_INFO "[Audio:PIPE] %s aud_pipe_data->channel:%d,count:%zu\n", __func__,
		  aud_pipe_data->channel, count);
	return count;

err_copy:
	printk(KERN_ERR "[Audio:PIPE] %s failed unalign_copy_from_user\n", __func__);
	ret = -EFAULT;
err:
	printk(KERN_ERR "[Audio:PIPE] %s failed\n", __func__);
	return ret;
}

static long aud_pipe_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct aud_pipe_data *aud_pipe_data = file->private_data;
	unsigned int value;

	if (!aud_pipe_data)
		return -EFAULT;

	switch (cmd) {
	case AUD_PIPE_IOC_WAKEUP_CH:
		aud_smsg_wakeup_ch(AUD_PIPE_SIPC_DST, aud_pipe_data->channel);
		return 0;

	case AUD_PIPE_IOC_BTHAL_SET:
		if (get_user(value, (unsigned int __user *)arg)) {
			g_bthal_state = 0;
			return -EFAULT;
		}
		g_bthal_state = value;
		printk(KERN_INFO "[Audio:PIPE] BTHAL_STATE_SET:%d\n", value);
		return 0;

	case AUD_PIPE_IOC_BTHAL_GET:
		printk(KERN_INFO "[Audio:PIPE] BTHAL_STATE_GET:%d\n", g_bthal_state);
		if (put_user(g_bthal_state, (unsigned int __user *)arg))
			return -EFAULT;
		return 0;
	}

	return -EFAULT;
}

static const struct file_operations audio_pipe_fops = {
	.owner		= THIS_MODULE,
	.llseek		= default_llseek,
	.read		= aud_pipe_read,
	.write		= aud_pipe_write,
	.unlocked_ioctl	= aud_pipe_ioctl,
	.compat_ioctl	= aud_pipe_ioctl,
	.open		= aud_pipe_open,
	.release	= aud_pipe_release,
};

static const struct of_device_id audio_pipe_match_table[] = {
	{ .compatible = "unisoc,audio_pipe_voice",	.data = (void *)AUD_PIPE_VOICE },
	{ .compatible = "unisoc,audio_pipe_effect",	.data = (void *)AUD_PIPE_EFFECT },
	{ .compatible = "unisoc,audio_pipe_recordproc",	.data = (void *)AUD_PIPE_RECORDPROC },
	{ .compatible = "unisoc,audio_pipe_bthal",	.data = (void *)AUD_PIPE_BTHAL },
	{ .compatible = "unisoc,audio_pipe_trigger_rx",	.data = (void *)AUD_PIPE_TRIGGER_RX },
	{ .compatible = "unisoc,audio_pipe_trigger_tx",	.data = (void *)AUD_PIPE_TRIGGER_TX },
	{ }
};
MODULE_DEVICE_TABLE(of, audio_pipe_match_table);

static const char * const audio_pipe_names[AUD_PIPE_MAX] = {
	[AUD_PIPE_VOICE]	= "audio_pipe_voice",
	[AUD_PIPE_EFFECT]	= "audio_pipe_effect",
	[AUD_PIPE_RECORDPROC]	= "audio_pipe_recordproc",
	[AUD_PIPE_BTHAL]	= "audio_pipe_bthal",
	[AUD_PIPE_TRIGGER_RX]	= "audio_pipe_trigger_rx",
	[AUD_PIPE_TRIGGER_TX]	= "audio_pipe_trigger_tx",
};

static int audio_pipe_parse_dt(struct aud_pipe_data *aud_pipe_data,
			       struct device_node *np)
{
	int ret;

	ret = of_property_read_u32(np, "sprd,writesync",
				   &aud_pipe_data->writesync);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:PIPE] %s  sprd,writesync, failed\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "sprd,maxuserwritebufsize",
				   &aud_pipe_data->maxuserwritebufsize);
	if (ret < 0) {
		/* vendor prints the wrong property name here, kept as is */
		printk(KERN_ERR "[Audio:PIPE] %s  sprd,maxwritebufsize, failed\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "sprd,channel", &aud_pipe_data->channel);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:PIPE] %s  sprd,channel,core failed\n", __func__);
		return ret;
	}

	printk(KERN_INFO "[Audio:PIPE] aud_pipe_data->channel:%d\n", aud_pipe_data->channel);
	return 0;
}

static int audio_pipe_probe(struct platform_device *pdev)
{
	struct aud_pipe_data *aud_pipe_data;
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	int type;
	int ret;

	aud_pipe_data = devm_kmalloc(&pdev->dev, sizeof(*aud_pipe_data),
				     GFP_KERNEL);
	if (!aud_pipe_data)
		return -ENOMEM;

	if (!np) {
		printk(KERN_ERR "[Audio:PIPE] %s defer\n", "aud_pipe_parse_dt");
		return -EPROBE_DEFER;
	}

	match = of_match_node(audio_pipe_match_table, np);
	if (!match) {
		printk(KERN_ERR "[Audio:PIPE] %s Get device id failed deffer!\n",
			 "aud_pipe_parse_dt");
		return -EPROBE_DEFER;
	}

	type = (int)(unsigned long)match->data;
	if (type >= AUD_PIPE_MAX) {
		printk(KERN_ERR "[Audio:PIPE] %s invalid pipe type %ld\n", "aud_pipe_parse_dt",
			 (long)type);
		return -EINVAL;
	}

	memset(aud_pipe_data->name, 0, sizeof(aud_pipe_data->name));
	strcpy(aud_pipe_data->name, audio_pipe_names[type]);

	ret = audio_pipe_parse_dt(aud_pipe_data, np);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:PIPE] %s failed\n", "aud_pipe_parse_dt");
		printk(KERN_ERR "[Audio:PIPE] %s parse dt failed\n", __func__);
		return ret;
	}

	printk(KERN_INFO "[Audio:PIPE] %s:name=%s\n", __func__, aud_pipe_data->name);

	aud_pipe_data->misc.minor = MISC_DYNAMIC_MINOR;
	aud_pipe_data->misc.name = aud_pipe_data->name;
	aud_pipe_data->misc.fops = &audio_pipe_fops;
	aud_pipe_data->misc.parent = NULL;

	ret = misc_register(&aud_pipe_data->misc);
	if (ret) {
		printk(KERN_ERR "[Audio:PIPE] Failed to alloc audio_pipe chrdev\n");
		return ret;
	}

	mutex_init(&aud_pipe_data->mutex);
	platform_set_drvdata(pdev, aud_pipe_data);

	return 0;
}

static int audio_pipe_remove(struct platform_device *pdev)
{
	struct aud_pipe_data *aud_pipe_data = platform_get_drvdata(pdev);

	if (aud_pipe_data) {
		misc_deregister(&aud_pipe_data->misc);
		devm_kfree(&pdev->dev, aud_pipe_data);
	}

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver audio_pipe_driver = {
	.probe = audio_pipe_probe,
	.remove = audio_pipe_remove,
	.driver = {
		.name = AUD_PIPE_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = audio_pipe_match_table,
	},
};

static int __init audio_pipe_init(void)
{
	return platform_driver_register(&audio_pipe_driver);
}

static void __exit audio_pipe_exit(void)
{
	platform_driver_unregister(&audio_pipe_driver);
}

module_init(audio_pipe_init);
module_exit(audio_pipe_exit);

MODULE_AUTHOR("SPRD");
MODULE_DESCRIPTION("SIPC/AUDIO_PIPE driver");
MODULE_LICENSE("GPL");
