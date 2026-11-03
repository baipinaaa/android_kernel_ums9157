// SPDX-License-Identifier: GPL-2.0
/*
 * microarray_fp.c - Microarray (AFS121) fingerprint sensor driver for
 *                   Sunelan Q968 / UMS9157 (T157)
 *
 * This file is a source-level reconstruction of the out-of-tree module
 * "microarray_fp.ko" which ships inside the vendor_boot ramdisk of the
 * Q968 firmware (build 10CN_0_480).  Everything below was derived from the
 * binary itself (recon/modules/microarray_fp.ko):
 *
 *   .text      = 0x196c   (6508 bytes, 31 symbols)
 *   .rodata    = 0x760
 *   .data      = 0x2e8
 *   .bss       = 0xc8
 *   vermagic   = 5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt
 *                mod_unload modversions aarch64
 *
 * The vendor built it from two sources; both paths are embedded in
 * .rodata of the binary:
 *
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/input/fingerprint/microarray/madev_tee.c
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/input/fingerprint/microarray/x86-settings.c
 *
 * ("x86-settings.c" is only a naming leftover, the vendor merged the
 *  platform part into it.)
 *
 * Notes:
 *  - All printk format strings are kept as single literals so that they can
 *    be compared byte for byte with the strings found in .rodata.
 *  - The vendor's quirks/bugs are reproduced on purpose; they are marked
 *    with "vendor quirk" comments.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/pm_wakeup.h>
#include <linux/fb.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/mm.h>

#define MAFP_DRIVER_NAME	"microarray_fp"
#define MAFP_COMPATIBLE		"microarray,afs121"
#define MAFP_DEV_NAME		"madev"
#define MAFP_DEV_NODE_NAME	"madev0"

/*
 * The three log helpers of the vendor driver.  They always print the fixed
 * prefix "MAFP_" and __func__, exactly like the binary does:
 *
 *   printk(KERN_DEBUG   "7%s%s: debug! %s!\n",     "MAFP_", func, msg)
 *   printk(KERN_WARNING "4%s%s: warning! %s's ret = %d\n", "MAFP_", func, msg, ret)
 *   printk(KERN_ERR     "3%s%s: error! %s is failed, ret = %d\n", "MAFP_", func, msg, ret)
 */
#define mas_log_debug(func, msg)					\
	printk(KERN_DEBUG "7%s%s: debug! %s!\n", "MAFP_", (func), (msg))

#define mas_log_warning(func, msg, ret)					\
	printk(KERN_WARNING "4%s%s: warning! %s's ret = %d\n",		\
	       "MAFP_", (func), (msg), (int)(ret))

#define mas_log_err(func, msg, ret)					\
	printk(KERN_ERR "3%s%s: error! %s is failed, ret = %d\n",	\
	       "MAFP_", (func), (msg), (int)(ret))

/* ------------------------------------------------------------------ */
/* ioctl command set (character device "madev0", type 'M')            */
/* ------------------------------------------------------------------ */

/* _IO('M', n), 1 .. 40 */
#define MAFP_IOCTL_WAKEUP		_IO('M', 1)
#define MAFP_IOCTL_WAIT_EVENT		_IO('M', 2)
#define MAFP_IOCTL_SIGNAL_EVENT		_IO('M', 3)
#define MAFP_IOCTL_NOP4			_IO('M', 4)
#define MAFP_IOCTL_NOP5			_IO('M', 5)
#define MAFP_IOCTL_ENABLE_IRQ		_IO('M', 6)
#define MAFP_IOCTL_DISABLE_IRQ		_IO('M', 7)
#define MAFP_IOCTL_KEY_F7_CLICK		_IO('M', 8)	/* 0x1d8 */
#define MAFP_IOCTL_KEY_F7_UP		_IO('M', 9)
#define MAFP_IOCTL_KEY_F7_DOWN		_IO('M', 11)
#define MAFP_IOCTL_KEY_F6_CLICK		_IO('M', 12)	/* 0x1d7 */
#define MAFP_IOCTL_KEY_F5_CLICK		_IO('M', 13)	/* 0x1d6 */
#define MAFP_IOCTL_KEY_FN1_CLICK	_IO('M', 18)	/* 0x1d2 */
#define MAFP_IOCTL_KEY_FN3_CLICK	_IO('M', 19)	/* 0x1d4 */
#define MAFP_IOCTL_KEY_FN2_CLICK	_IO('M', 20)	/* 0x1d3 */
#define MAFP_IOCTL_KEY_FN4_CLICK	_IO('M', 21)	/* 0x1d5 */
#define MAFP_IOCTL_REQUEST_IRQ		_IO('M', 31)
#define MAFP_IOCTL_FREE_IRQ		_IO('M', 32)
#define MAFP_IOCTL_NOP36		_IO('M', 36)
#define MAFP_IOCTL_NOP37		_IO('M', 37)
#define MAFP_IOCTL_WAIT_U2		_IO('M', 39)
#define MAFP_IOCTL_SIGNAL_U2		_IO('M', 40)

/* _IOW('M', n, int), n = 33 .. 54 */
#define MAFP_IOCTL_SET_REG		_IOW('M', 33, int)
#define MAFP_IOCTL_SET_SPEED		_IOW('M', 38, int)
#define MAFP_IOCTL_SET_REG_AND_WAKE	_IOW('M', 42, int)
#define MAFP_IOCTL_SET_FACTORY_FLAG	_IOW('M', 54, int)

/* _IOR('M', n, int), n = 34 .. 53 */
#define MAFP_IOCTL_GET_REG		_IOR('M', 34, int)
#define MAFP_IOCTL_GET_VERSION		_IOR('M', 35, int)
#define MAFP_IOCTL_WAIT_AND_GET_REG	_IOR('M', 41, int)
#define MAFP_IOCTL_GET_SCREEN_STATE	_IOR('M', 43, int)
#define MAFP_IOCTL_GET_INT_PIN_STATE	_IOR('M', 44, int)
#define MAFP_IOCTL_GET_FACTORY_FLAG	_IOR('M', 53, int)

/* value returned by MAFP_IOCTL_GET_VERSION (0x4006 in the binary) */
#define MAFP_IOCTL_VERSION		0x4006

/* the driver reports F1..F7 / POWER through the input device */
#define MAFP_KEY_FN1			KEY_FN_F1
#define MAFP_KEY_FN2			KEY_FN_F2
#define MAFP_KEY_FN3			KEY_FN_F3
#define MAFP_KEY_FN4			KEY_FN_F4
#define MAFP_KEY_F5			KEY_FN_F5
#define MAFP_KEY_F6			KEY_FN_F6
#define MAFP_KEY_F7			KEY_FN_F7

/* size of the mmap'd scratch buffer (128 KiB, order 5) */
#define MAFP_MMAP_SIZE			0x20000

/* request_threaded_irq() flags used by the vendor: 0x4001 */
#define MAFP_IRQ_FLAGS			(IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND)

/* ------------------------------------------------------------------ */
/* driver state (one global instance, as in the binary)               */
/* ------------------------------------------------------------------ */

struct mas_device {
	dev_t devno;
	u32 pad;
	struct cdev *cdev;
	struct class *class;
	struct device *device;
};

static struct device_node *node;

static int int_gpio;
static int en_gpio;

static void *kernel_memaddr;
/*
 * .bss+0xc0 holds a "kernel_memesize" global in the binary which is never
 * read nor written; it is only mentioned here for completeness.
 */

/* "ret" in the binary, .bss+0x18: result of the last copy_*_user() */
static int mafp_ret;

static struct mas_device *sdev;

/* "ma_drv_reg", .bss+0x28: 32bit register exchanged with the HAL */
static int ma_drv_reg;

static struct wakeup_source *gProcessWakeLock;
static struct work_struct gWork;
static struct workqueue_struct *gWorkq;

static int irq;
static struct notifier_block notifier;

static int is_screen_on = 0;
static struct input_dev *ma_input_t;

static u8 irq_flag;
static u8 irq_enabled;
static int is_enable_irq;
static int ma_speed;
static u8 u2_flag;
static u8 u1_flag;
static int int_pin_state;
static int g_factory_flag;

static wait_queue_head_t gWaitq;
static wait_queue_head_t U2_Waitq;
static wait_queue_head_t U1_Waitq;

static DEFINE_MUTEX(ioctl_lock);

static int mas_set_input(void);
static int mas_set_enable_gpio(struct platform_device *pdev);
static int mas_get_irq(struct platform_device *pdev);
static int mas_plat_probe(struct platform_device *pdev);
static int mas_plat_remove(struct platform_device *pdev);
static irqreturn_t mas_interrupt(int irq, void *dev_id);
static void mas_work(struct work_struct *work);
static long mas_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static long mas_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg);
static int mas_mmap(struct file *file, struct vm_area_struct *vma);
static int mas_fb_notifier_callback(struct notifier_block *self,
				    unsigned long event, void *data);

/* ------------------------------------------------------------------ */
/* tiny helpers, most of them dead code in the vendor driver          */
/* ------------------------------------------------------------------ */

static int __maybe_unused mas_finger_get_gpio_info(void)
{
	node = of_find_compatible_node(NULL, NULL, MAFP_COMPATIBLE);
	return 0;
}

static int __maybe_unused mas_select_transfer(void)
{
	printk("mas_select_transfer start");
	return 0;
}

static int __maybe_unused ma_spi_change(void)
{
	printk("ma_spi_change start");
	return 0;
}

/* all of the following are plain no-ops in the binary and are unreferenced */
static int __maybe_unused mas_finger_set_spi(void)		{ return 0; }
static int __maybe_unused mas_finger_set_power(void)		{ return 0; }
static int __maybe_unused mas_switch_power(void)			{ return 0; }
static int __maybe_unused mas_finger_set_eint(void)		{ return 0; }
static int __maybe_unused mas_finger_set_gpio_info(void)	{ return 0; }
static void __maybe_unused mas_enable_spi_clock(void)		{ }
static void __maybe_unused mas_disable_spi_clock(void)		{ }

/* ------------------------------------------------------------------ */
/* GPIO / IRQ setup                                                   */
/* ------------------------------------------------------------------ */

static int mas_get_irq(struct platform_device *pdev)
{
	const char *msg;
	int ret;

	int_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "fpint-gpios", 0,
					   NULL);

	ret = gpio_request(int_gpio, "microarray_eint");
	if (ret < 0) {
		msg = "gpio request error! ";
		goto err;
	}

	ret = gpiod_direction_input(gpio_to_desc(int_gpio));
	if (ret) {
		msg = "gpio_direction_input failed\n";
		goto err;
	}

	ret = gpiod_to_irq(gpio_to_desc(int_gpio));
	if (ret < 0) {
		msg = "gpio_to_irq failed\n";
		goto err;
	}

	return ret;

err:
	/* the vendor prints the error and reports "no irq" */
	mas_log_err("mas_get_irq", msg, 0);
	return 0;
}

static int mas_get_interrupt_gpio(void)
{
	int val;

	val = gpiod_get_raw_value(gpio_to_desc(int_gpio));
	pr_debug("MAFP guq int %d\n", val);
	return val;
}

static int mas_set_enable_gpio(struct platform_device *pdev)
{
	const char *msg;
	int ret;

	en_gpio = of_get_named_gpio_flags(pdev->dev.of_node, "fpen-gpios", 0,
					  NULL);
	if (en_gpio < 0) {
		/* vendor quirk: info level, not an error */
		mas_log_debug("mas_set_enable_gpio", "en gpio not exist");
		return 0;
	}

	ret = gpio_request(en_gpio, "microarray_en");
	if (ret) {
		msg = "en gpio request error! ";
		goto err;
	}

	ret = gpiod_direction_output_raw(gpio_to_desc(en_gpio), 1);
	if (ret) {
		msg = "en gpio set output error! ";
		gpio_free(en_gpio);
		goto err;
	}

	return 0;

err:
	mas_log_err("mas_set_enable_gpio", msg, ret);
	return ret;
}

static irqreturn_t mas_interrupt(int irq, void *dev_id)
{
	pr_debug("microarray fingerprint interrupt happened!\n");
	queue_work(gWorkq, &gWork);
	return IRQ_HANDLED;
}

static void mas_work(struct work_struct *work)
{
	irq_flag = 1;
	pm_wakeup_ws_event(gProcessWakeLock, jiffies_to_msecs(500), 0);
	__wake_up(&gWaitq, TASK_NORMAL, 1, NULL);
}

/* ------------------------------------------------------------------ */
/* input device                                                       */
/* ------------------------------------------------------------------ */

static int mas_set_input(void)
{
	struct input_dev *input;

	input = input_allocate_device();
	if (!input) {
		mas_log_err("mas_set_input", "input_allocate_device failed.",
			    mafp_ret);
		return mafp_ret;
	}

	input->name = MAFP_DEV_NAME;
	input->id.bustype = BUS_SPI;

	/* bit1 of evbit (EV_KEY) plus the key codes below */
	set_bit(EV_KEY, input->evbit);
	set_bit(MAFP_KEY_FN1, input->keybit);
	set_bit(MAFP_KEY_FN2, input->keybit);
	set_bit(MAFP_KEY_FN3, input->keybit);
	set_bit(MAFP_KEY_FN4, input->keybit);
	set_bit(MAFP_KEY_F5, input->keybit);
	set_bit(MAFP_KEY_F6, input->keybit);
	set_bit(MAFP_KEY_F7, input->keybit);
	set_bit(KEY_POWER, input->keybit);

	if (input_register_device(input)) {
		input_free_device(input);
		mas_log_err("mas_set_input", "failed to register input device.",
			    mafp_ret);
		return mafp_ret;
	}

	ma_input_t = input;
	return 0;
}

/* ------------------------------------------------------------------ */
/* frame buffer notifier (vendor registers it nowhere: dead code)     */
/* ------------------------------------------------------------------ */

static int mas_fb_notifier_callback(struct notifier_block *self,
				    unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int blank;

	if (event != FB_EVENT_BLANK)
		return 0;

	blank = *(int *)evdata->data;
	if (blank == FB_BLANK_UNBLANK)
		is_screen_on = 1;
	else if (blank == FB_BLANK_POWERDOWN)
		is_screen_on = 0;

	return 0;
}

/* ------------------------------------------------------------------ */
/* mmap of the shared scratch page                                    */
/* ------------------------------------------------------------------ */

static int mas_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!kernel_memaddr) {
		kernel_memaddr = kmalloc(MAFP_MMAP_SIZE, GFP_KERNEL);
		if (!kernel_memaddr)
			return -1;
	}

	/* vendor maps the buffer uncached and non executable */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    virt_to_phys(kernel_memaddr) >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -1;

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;

	return 0;
}

/* ------------------------------------------------------------------ */
/* ioctl                                                              */
/* ------------------------------------------------------------------ */

static void mas_send_key(struct input_dev *input, unsigned int code, int down)
{
	input_event(input, EV_KEY, code, down);
	input_event(input, EV_SYN, 0, 0);
}

static void mas_send_key_click(struct input_dev *input, unsigned int code)
{
	input_event(input, EV_KEY, code, 1);
	input_event(input, EV_SYN, 0, 0);
	input_event(input, EV_KEY, code, 0);
	input_event(input, EV_SYN, 0, 0);
}

static long mas_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	int val = MAFP_IOCTL_VERSION;

	switch (cmd) {
	case MAFP_IOCTL_WAKEUP:
		pm_wakeup_ws_event(gProcessWakeLock, jiffies_to_msecs(1250), 0);
		return 0;

	case MAFP_IOCTL_WAIT_EVENT:
		irq_flag = 0;
		freezer_do_not_count();
		wait_event_killable(gWaitq, irq_flag == 1);
		freezer_count();
		return 0;

	case MAFP_IOCTL_SIGNAL_EVENT:
		irq_flag = 1;
		__wake_up(&gWaitq, TASK_NORMAL, 1, NULL);
		return 0;

	case MAFP_IOCTL_NOP4:
	case MAFP_IOCTL_NOP5:
	case MAFP_IOCTL_NOP36:
	case MAFP_IOCTL_NOP37:
		/* vendor quirk: these commands exist but do nothing */
		return 0;

	case MAFP_IOCTL_ENABLE_IRQ:
		if (irq_enabled == 1)
			enable_irq(irq);
		return 0;

	case MAFP_IOCTL_DISABLE_IRQ:
		if (irq_enabled == 1)
			disable_irq_nosync(irq);
		return 0;

	case MAFP_IOCTL_KEY_F7_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_F7);
		return 0;

	case MAFP_IOCTL_KEY_F7_UP:
		mas_send_key(ma_input_t, MAFP_KEY_F7, 0);
		return 0;

	case MAFP_IOCTL_KEY_F7_DOWN:
		mas_send_key(ma_input_t, MAFP_KEY_F7, 1);
		return 0;

	case MAFP_IOCTL_KEY_F6_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_F6);
		return 0;

	case MAFP_IOCTL_KEY_F5_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_F5);
		return 0;

	case MAFP_IOCTL_KEY_FN1_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_FN1);
		return 0;

	case MAFP_IOCTL_KEY_FN3_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_FN3);
		return 0;

	case MAFP_IOCTL_KEY_FN2_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_FN2);
		return 0;

	case MAFP_IOCTL_KEY_FN4_CLICK:
		mas_send_key_click(ma_input_t, MAFP_KEY_FN4);
		return 0;

	case MAFP_IOCTL_REQUEST_IRQ:
		if (irq_enabled)
			return 0;

		irq_enabled = 1;
		ret = request_threaded_irq(irq, NULL, mas_interrupt,
					   MAFP_IRQ_FLAGS,
					   "sprd,finger_print-eint", NULL);
		if (ret < 0) {
			mas_log_err("mas_ioctl", "request irq", mafp_ret);
			return ret;
		}

		irq_set_irq_wake(irq, 1);
		/* vendor quirk: stores the (successful) return value of
		 * request_threaded_irq(), i.e. 0 */
		is_enable_irq = ret;
		return 0;

	case MAFP_IOCTL_FREE_IRQ:
		if (is_enable_irq)
			free_irq(irq, NULL);
		return 0;

	case MAFP_IOCTL_WAIT_U2:
		u2_flag = 0;
		return wait_event_interruptible(U2_Waitq, u2_flag == 1);

	case MAFP_IOCTL_SIGNAL_U2:
		u2_flag = 1;
		__wake_up(&U2_Waitq, TASK_NORMAL, 1, NULL);
		return 0;

	case MAFP_IOCTL_SET_REG:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_from_user(&ma_drv_reg, (void __user *)arg,
					  sizeof(ma_drv_reg));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_SET_SPEED:
		mutex_lock(&ioctl_lock);
		copy_from_user(&ma_speed, (void __user *)arg, sizeof(ma_speed));
		mutex_unlock(&ioctl_lock);
		printk("ma_spi_change start");
		return 0;

	case MAFP_IOCTL_SET_REG_AND_WAKE:
		mutex_lock(&ioctl_lock);
		copy_from_user(&ma_drv_reg, (void __user *)arg,
			       sizeof(ma_drv_reg));
		mutex_unlock(&ioctl_lock);
		msleep(20);
		u1_flag = 1;
		__wake_up(&U1_Waitq, TASK_NORMAL, 1, NULL);
		return 0;

	case MAFP_IOCTL_SET_FACTORY_FLAG:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_from_user(&g_factory_flag, (void __user *)arg,
					  sizeof(g_factory_flag));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_GET_REG:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_to_user((void __user *)arg, &ma_drv_reg,
					sizeof(ma_drv_reg));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_GET_VERSION:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_to_user((void __user *)arg, &val, sizeof(val));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_WAIT_AND_GET_REG:
		u1_flag = 0;
		ret = wait_event_interruptible(U1_Waitq, u1_flag == 1);
		mutex_lock(&ioctl_lock);
		copy_to_user((void __user *)arg, &ma_drv_reg,
			     sizeof(ma_drv_reg));
		mutex_unlock(&ioctl_lock);
		return ret;

	case MAFP_IOCTL_GET_SCREEN_STATE:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_to_user((void __user *)arg, &is_screen_on,
					sizeof(is_screen_on));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_GET_INT_PIN_STATE:
		int_pin_state = mas_get_interrupt_gpio();
		if (int_pin_state > 1)
			return 0;
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_to_user((void __user *)arg, &int_pin_state,
					sizeof(int_pin_state));
		mutex_unlock(&ioctl_lock);
		return 0;

	case MAFP_IOCTL_GET_FACTORY_FLAG:
		mutex_lock(&ioctl_lock);
		mafp_ret = copy_to_user((void __user *)arg, &g_factory_flag,
					sizeof(g_factory_flag));
		mutex_unlock(&ioctl_lock);
		return 0;

	default:
		mas_log_err("mas_ioctl", "mas_ioctl no such cmd", mafp_ret);
		return -EINVAL;
	}
}

static long mas_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return file->f_op->unlocked_ioctl(file, cmd,
					  (unsigned long)compat_ptr(arg));
}

static const struct file_operations sfops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= mas_ioctl,
	.compat_ioctl	= mas_compat_ioctl,
	.mmap		= mas_mmap,
};

/* ------------------------------------------------------------------ */
/* char device                                                        */
/* ------------------------------------------------------------------ */

/*
 * Allocates the character device "madev0" and the sysfs class "madev".
 * The vendor keeps everything in the tiny "struct mas_device" allocated by
 * the caller.
 */
static int init_file_node(struct mas_device *dev)
{
	int ret;

	ret = alloc_chrdev_region(&dev->devno, 0, 1, MAFP_DEV_NAME);
	if (ret < 0) {
		mas_log_err("init_file_node", "alloc_chrdev_region error!", ret);
		goto err;
	}

	dev->cdev = cdev_alloc();
	if (!dev->cdev) {
		mas_log_err("init_file_node", "cdev_alloc error!", ret);
		goto err;
	}

	dev->cdev->owner = THIS_MODULE;
	dev->cdev->ops = &sfops;

	ret = cdev_add(dev->cdev, dev->devno, 1);
	if (ret) {
		mas_log_err("init_file_node", "cdev_add", ret);
		goto err;
	}

	dev->class = class_create(THIS_MODULE, MAFP_DEV_NAME);
	if (IS_ERR(dev->class)) {
		mas_log_err("init_file_node", "class_create", 0);
		goto err;
	}

	dev->device = device_create(dev->class, NULL, dev->devno, NULL,
				    MAFP_DEV_NODE_NAME);
	if (IS_ERR(dev->device)) {
		/* vendor quirk: prints the low 32bit of the error pointer */
		mas_log_err("init_file_node", "device_create",
			    (int)(unsigned long)dev->device);
		goto err;
	}

	return 0;

err:
	mafp_ret = -1;
	unregister_chrdev_region(dev->devno, 1);
	/* vendor quirk: cdev_del() is called even when cdev_alloc() failed */
	cdev_del(dev->cdev);
	return -1;
}

/* ------------------------------------------------------------------ */
/* platform driver                                                    */
/* ------------------------------------------------------------------ */

static int mas_plat_probe(struct platform_device *pdev)
{
	struct device_node *of_node;
	int ret;

	of_node = of_find_compatible_node(NULL, NULL, MAFP_COMPATIBLE);
	node = of_node;

	mafp_ret = 0;
	mas_log_debug("mas_plat_probe", "start");

	sdev = kmalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev) {
		mas_log_err("init_vars", "malloc sdev space failed!",
			    mafp_ret);
		/* vendor quirk: falls through instead of returning */
	}

	ma_drv_reg = 0;
	gProcessWakeLock =
		wakeup_source_register(NULL, "microarray_process_wakelock");
	if (!gProcessWakeLock) {
		mafp_ret = -ENOMEM;
		goto err;
	}

	INIT_WORK(&gWork, mas_work);

	/*
	 * flags == 0xe000a:
	 *   WQ_UNBOUND | WQ_MEM_RECLAIM | __WQ_ORDERED |
	 *   __WQ_ORDERED_EXPLICIT | __WQ_LEGACY, max_active = 1
	 * which is create_singlethread_workqueue() with max_active = 1.
	 */
	gWorkq = alloc_workqueue("%s",
				 WQ_UNBOUND | WQ_MEM_RECLAIM |
				 __WQ_ORDERED | __WQ_ORDERED_EXPLICIT |
				 __WQ_LEGACY, 1, "mas_workqueue");
	if (!gWorkq) {
		mas_log_err("init_vars", "create_single_workqueue error!",
			    mafp_ret);
		mafp_ret = -ENOMEM;
		goto err;
	}

	mas_log_debug("mas_plat_probe", "poing 1");
	mafp_ret = 0;

	ret = mas_set_enable_gpio(pdev);
	mafp_ret = ret;
	if (ret)
		goto err;

	irq = mas_get_irq(pdev);
	printk(KERN_INFO "MAFP_%s: mas_get_irq = %d\n", "init_interrupt", irq);
	if (!irq) {
		mafp_ret = 1;
		mas_log_err("init_interrupt", "mas_get_irq", 1);
		goto err;
	}

	mas_log_debug("mas_plat_probe", "poing 2");
	mafp_ret = 0;

	ret = init_file_node(sdev);
	if (ret)
		goto err;

	mas_log_debug("mas_plat_probe", "poing 3");
	mafp_ret = 0;
	mas_log_debug("mas_plat_probe", "poing 4");
	mafp_ret = 0;

	mas_set_input();

	mas_log_debug("mas_plat_probe", "end");

	/*
	 * vendor quirk: the fb notifier is filled in but never registered
	 * with fb_register_client(), is_screen_on is just forced to 1
	 */
	notifier.notifier_call = mas_fb_notifier_callback;
	is_screen_on = 1;

	mafp_ret = 0;
	return 0;

err:
	if (gWorkq) {
		destroy_workqueue(gWorkq);
		gWorkq = NULL;
	}
	if (gProcessWakeLock) {
		wakeup_source_unregister(gProcessWakeLock);
		gProcessWakeLock = NULL;
	}
	return mafp_ret;
}

static int mas_plat_remove(struct platform_device *pdev)
{
	unregister_chrdev_region(sdev->devno, 1);
	cdev_del(sdev->cdev);

	if (gWorkq) {
		destroy_workqueue(gWorkq);
		gWorkq = NULL;
	}

	if (gProcessWakeLock) {
		wakeup_source_unregister(gProcessWakeLock);
		gProcessWakeLock = NULL;
	}

	/* vendor quirk: device_destroy()/class_destroy() are never called */
	return 0;
}

/* dead code in the binary (kept for completeness) */
static int __maybe_unused mas_remove_platform(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sof_match[] = {
	{ .compatible = MAFP_COMPATIBLE },
	{ },
};

static struct platform_driver spdrv = {
	.probe		= mas_plat_probe,
	.remove		= mas_plat_remove,
	.driver		= {
		.name		= MAFP_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= sof_match,
	},
};

static int mas_get_platform(void)
{
	int ret;

	mas_log_debug("mas_get_platform", "start");

	ret = platform_driver_register(&spdrv);
	if (ret)
		printk("MAFP mas_get_platform  spi_register_driver ret = %d\n",
		       ret);

	mas_log_debug("mas_get_platform", "end");
	return ret;
}

static int __init mas_init(void)
{
	int ret;

	irq_enabled = 0;

	ret = mas_get_platform();
	if (ret)
		mas_log_err("mas_init", "mas_get_platform", ret);

	return ret;
}

static void __exit mas_exit(void)
{
	/*
	 * vendor quirk: the binary's .exit.text is a bare "ret", i.e. the
	 * module never unregisters its platform driver.
	 */
}

module_init(mas_init);
module_exit(mas_exit);

MODULE_AUTHOR("Microarray");
MODULE_DESCRIPTION("Driver for microarray fingerprint sensor");
MODULE_LICENSE("GPL");
MODULE_ALIAS("of:N*T*Cmicroarray,afs121");
MODULE_ALIAS("of:N*T*Cmicroarray,afs121C*");
