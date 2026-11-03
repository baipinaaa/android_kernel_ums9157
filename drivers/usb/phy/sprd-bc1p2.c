// SPDX-License-Identifier: GPL-2.0
/*
 * sprd bc1p2 driver
 *
 * 依据 `sprd-bc1p2.ko`（3,264 B .text / 9 个真函数，设备 /proc/modules 里就是它）
 * 逐函数重建：所有寄存器地址、位掩码、循环次数、日志行号都取自反汇编
 * （recon/out/bc1p2_asm.txt），没有参考任何下载的上游源码。
 *
 * 与 9620 树里旧名文件 `sprd-ump96xx-bc1p2.c` 的关系：设备上是**新驱动**，
 * 名字从 ump96xx_bc1p2 改成 sprd_bc1p2、多了 kthread 重检测机制、
 * 寄存器表改成 per-compatible 的 `struct sprd_bc1p2_data`。旧文件已改名
 * `.orig-302` 留作对照，不参与编译。
 *
 * 结构体偏移全部来自 .ko：
 *   struct sprd_bc1p2      size 0x40
 *     +0x00 struct mutex bc1p2_lock   （锁名 "&bc1p2->bc1p2_lock"）
 *     +0x20 struct regmap *regmap
 *     +0x28 const struct sprd_bc1p2_data *pdata
 *     +0x30 enum usb_charger_type charger_type
 *     +0x34 u32 detect_state          （1 = 检出，2 = 未检出）
 *     +0x38 bool redetect_support     （pdata->reg3 非 0 才支持）
 *   struct sprd_bc1p2_info size 0x78  （由调用者 usb_add_bc1p2_init 传进来）
 *     +0x00 struct usb_phy *phy
 *     +0x08 struct kthread_worker bc1p2_kworker（锁名 "(&bc1p2_info->bc1p2_kworker)->lock"）
 *     +0x40 struct kthread_work type_work
 *     +0x68 struct task_struct *task
 *     +0x70 spinlock_t lock
 *     +0x74 bool pending
 *   .bss: +0x00 static struct sprd_bc1p2 *bc1p2；+0x08 static u8 det_delay_ms
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>
#include <linux/spinlock.h>
#include <linux/usb/phy.h>
#include <uapi/linux/usb/charger.h>

/* 状态寄存器位（.rodata 立即数反推） */
#define BC1P2_DET_DONE			BIT(11)		/* 0x800 */
#define BC1P2_DET_INT			BIT(2)		/* 0x004 */
#define BC1P2_DET_VALID			(BC1P2_DET_DONE | BC1P2_DET_INT)
#define BC1P2_DET_TYPE_MASK		GENMASK(7, 5)	/* 0x0e0 */
#define BC1P2_DET_TYPE_CDP		0x20
#define BC1P2_DET_TYPE_DCP		0x40
#define BC1P2_DET_TYPE_SDP		0x80

/* 轮询参数 */
#define BC1P2_DET_POLL_MS		20
#define BC1P2_DET_LOOPS			60
#define BC1P2_DET_LOOPS_REDETECT	107		/* 0x6b */

/* redetect 控制 */
#define BC1P2_REDET_EN			BIT(0)
#define BC1P2_REDET_DELAY_MASK		GENMASK(7, 4)	/* 0x0f0 */
#define BC1P2_REDET_DELAY_MAX		0x0f

/* usb_phy->flags 用到的位 */
#define BC1P2_FL_DETECTING		BIT(0)
#define BC1P2_FL_NO_REDETECT		BIT(30)		/* 与 DETECTING 一起置位 */

struct sprd_bc1p2_data {
	u32 reg;	/* +0x00 状态寄存器 */
	u32 reg2;	/* +0x04 probe 初始化 / redetect 延时寄存器 */
	u32 reg3;	/* +0x08 redetect 使能寄存器（0 = 该 PMIC 不支持） */
	u32 mask;	/* +0x0c */
	u32 shift;	/* +0x10 */
};

struct sprd_bc1p2 {
	struct mutex bc1p2_lock;
	struct regmap *regmap;
	const struct sprd_bc1p2_data *pdata;
	enum usb_charger_type charger_type;
	u32 detect_state;
	bool redetect_support;
};

struct sprd_bc1p2_info {
	struct usb_phy *phy;
	struct kthread_worker bc1p2_kworker;
	struct kthread_work type_work;
	struct task_struct *task;
	spinlock_t lock;
	bool pending;
};

static struct sprd_bc1p2 *bc1p2;
static u8 det_delay_ms;

static enum usb_charger_type sprd_bc1p2_detect(void);
void usb_phy_notify_charger(struct usb_phy *x);
void sprd_bc1p2_notify_charger(struct usb_phy *x);

void usb_remove_bc1p2(struct sprd_bc1p2_info *bc1p2_info)
{
	if (!bc1p2_info->task)
		return;

	kthread_flush_worker(&bc1p2_info->bc1p2_kworker);
	kthread_stop(bc1p2_info->task);
	bc1p2_info->task = NULL;
}
EXPORT_SYMBOL_GPL(usb_remove_bc1p2);

/*
 * chg_state 决定通知什么电流给上层的 charger 框架：
 *   PRESENT → 问 phy 要 min/max，再把 max 作为 value 通知
 *   ABSENT  → 先按类型把 chg_cur 的默认档位写好，再以 0 通知
 * 两种情况的末尾都要发 KOBJ_CHANGE，让用户空间重新读 /sys/class/power_supply。
 */
void usb_phy_notify_charger(struct usb_phy *x)
{
	int min_cur = 0;
	int max_cur = 0;

	switch (x->chg_state) {
	case USB_CHARGER_PRESENT:
		usb_phy_get_charger_current(x, &min_cur, &max_cur);
		atomic_notifier_call_chain(&x->notifier, max_cur, x);
		break;
	case USB_CHARGER_ABSENT:
		x->chg_cur.sdp_min = 2;
		x->chg_cur.sdp_max = 500;
		x->chg_cur.dcp_min = 500;
		x->chg_cur.dcp_max = 5000;
		x->chg_cur.cdp_min = 5000;
		x->chg_cur.cdp_max = 5000;
		x->chg_cur.aca_min = 5000;
		x->chg_cur.aca_max = 5000;
		atomic_notifier_call_chain(&x->notifier, 0, x);
		break;
	default:
		dev_warn(x->dev, "Unknown USB charger state: %d\n", x->chg_state);
		return;
	}

	kobject_uevent(&x->dev->kobj, KOBJ_CHANGE);
}
EXPORT_SYMBOL_GPL(usb_phy_notify_charger);

static void sprd_get_bc1p2_type_work(struct kthread_work *work)
{
	struct sprd_bc1p2_info *bc1p2_info;

	bc1p2_info = container_of(work, struct sprd_bc1p2_info, type_work);

	dev_dbg(bc1p2_info->phy->dev, "%s\n", __func__);

	/*
	 * 每次醒来都把 pending 清掉并通知一次；通知过程中可能又被
	 * sprd_usb_changed() 置位，所以用循环而不是 if。
	 */
	for (;;) {
		spin_lock(&bc1p2_info->lock);
		if (!bc1p2_info->pending) {
			spin_unlock(&bc1p2_info->lock);
			break;
		}
		bc1p2_info->pending = false;
		spin_unlock(&bc1p2_info->lock);
		sprd_bc1p2_notify_charger(bc1p2_info->phy);
	}
}

/*
 * 由 USB PHY 驱动在 probe 时调用（`phy-sprd-usb31-qogirn6pro.ko` 就是这么用的）：
 * usb_phy 的指针在这一侧保存，重检测线程也在这里起。
 */
int usb_add_bc1p2_init(struct sprd_bc1p2_info *bc1p2_info, struct usb_phy *x)
{
	struct sched_param param = { .sched_priority = 1 };
	struct task_struct *task;

	if (!x->dev) {
		dev_err(NULL, "no device provided for PHY\n");
		return -ENODEV;
	}

	bc1p2_info->phy = x;
	bc1p2_info->pending = false;

	kthread_init_worker(&bc1p2_info->bc1p2_kworker);
	kthread_init_work(&bc1p2_info->type_work, sprd_get_bc1p2_type_work);

	task = kthread_create_on_node(kthread_worker_fn, &bc1p2_info->bc1p2_kworker,
				      NUMA_NO_NODE, "sprd_bc1p2_worker");
	if (IS_ERR(task)) {
		/*
		 * 原样保留：这里先清 task 再取 PTR_ERR(task)，所以失败时
		 * 反而返回 0。反汇编就是这个行为（ldr w0,[x19,#0x68] 读到 0）。
		 */
		bc1p2_info->task = NULL;
		dev_err(x->dev, "failed to run bc1p2_thread\n");
		return PTR_ERR(bc1p2_info->task);
	}

	wake_up_process(task);
	bc1p2_info->task = task;
	sched_setscheduler(bc1p2_info->task, SCHED_FIFO, &param);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_add_bc1p2_init);

/*
 * 充电器状态变化（由 USB 驱动调）：只记状态 + 唤醒重检测线程，
 * 真正的检测在 sprd_get_bc1p2_type_work() 里做，避免在中断上下文读 I2C。
 */
void sprd_usb_changed(struct sprd_bc1p2_info *bc1p2_info, int state)
{
	struct usb_phy *x = bc1p2_info->phy;

	dev_dbg(x->dev, "%s\n", __func__);

	spin_lock(&bc1p2_info->lock);
	bc1p2_info->pending = true;
	x->chg_state = state;
	if (state == USB_CHARGER_ABSENT)
		x->flags &= ~BC1P2_FL_DETECTING;
	spin_unlock(&bc1p2_info->lock);

	if (bc1p2_info->task)
		kthread_queue_work(&bc1p2_info->bc1p2_kworker, &bc1p2_info->type_work);
}
EXPORT_SYMBOL_GPL(sprd_usb_changed);

void sprd_bc1p2_notify_charger(struct usb_phy *x)
{
	enum usb_charger_type type;

	if (!bc1p2 || !x->charger_detect) {
		/* 两个 NULL 检查共用同一条消息（.rodata+0x1c0） */
		pr_err("%s:line%d: charger_detect NULL pointer!!!\n", __func__, 389);
		return;
	}

	if (x->chg_state != USB_CHARGER_PRESENT) {
		if (x->chg_state == USB_CHARGER_ABSENT) {
			dev_info(x->dev, "usb_charger_absent\n");
			x->chg_type = UNKNOWN_TYPE;
			usb_phy_notify_charger(x);
		} else {
			dev_warn(x->dev, "Unknown USB charger state: %d\n", x->chg_state);
		}
		return;
	}

	type = x->charger_detect(x);
	x->chg_type = type;

	/*
	 * 检测期间线被拔了：芯片报出来的类型不能信，按“不在”上报。
	 * 0x%x 的实参是常量 0（反汇编里 w2 = wzr）。
	 */
	if (x->chg_state == USB_CHARGER_ABSENT) {
		dev_info(x->dev, "detected bc1p2 type:0x%x, absent\n", 0);
		x->chg_type = UNKNOWN_TYPE;
		usb_phy_notify_charger(x);
		return;
	}

	/*
	 * 支持重检测的 PMIC 上，未检出（type == 0）且已经进入过检测流程时
	 * 不重复上报，等 kthread 那边的结果。
	 */
	if (!type && bc1p2->redetect_support && (x->flags & BC1P2_FL_DETECTING))
		return;

	dev_info(x->dev, "detected bc1p2 type:0x%x\n", type);
	usb_phy_notify_charger(x);
}
EXPORT_SYMBOL_GPL(sprd_bc1p2_notify_charger);

static int sprd_bc1p2_redetect_control(bool enable)
{
	int ret;

	ret = regmap_update_bits(bc1p2->regmap, bc1p2->pdata->reg3,
				 BC1P2_REDET_EN,
				 enable ? BC1P2_REDET_EN : 0);
	if (ret)
		pr_info("fail to %s, enable = %d\n", __func__, enable);

	return ret;
}

/*
 * 单次检测：轮询状态寄存器直到 CHG_DET_DONE，再按 TYPE 位解码。
 * 循环次数 60 / 107 取决于是不是刚刚触发过重检测（det_delay_ms）。
 * `i < 60` 那个判断是原代码就有的：刚触发重检测的头 48 次只等 DONE 位，
 * 不检查 INT 位。
 */
static enum usb_charger_type sprd_bc1p2_detect(void)
{
	enum usb_charger_type type = UNKNOWN_TYPE;
	u32 val = 0;
	int loops;
	int i;
	int ret;

	loops = det_delay_ms ? BC1P2_DET_LOOPS_REDETECT : BC1P2_DET_LOOPS;
	det_delay_ms = 0;

	for (i = loops; i > 0; i--) {
		ret = regmap_read(bc1p2->regmap, bc1p2->pdata->reg, &val);
		if (ret)
			break;

		if (i < BC1P2_DET_LOOPS && !(val & BC1P2_DET_INT))
			break;

		if (val & BC1P2_DET_DONE) {
			switch (val & BC1P2_DET_TYPE_MASK) {
			case BC1P2_DET_TYPE_SDP:
				type = SDP_TYPE;
				break;
			case BC1P2_DET_TYPE_DCP:
				type = DCP_TYPE;
				break;
			case BC1P2_DET_TYPE_CDP:
				type = CDP_TYPE;
				break;
			default:
				type = UNKNOWN_TYPE;
				break;
			}
			break;
		}

		msleep(BC1P2_DET_POLL_MS);
	}

	bc1p2->detect_state = (val & BC1P2_DET_INT) ? 1 : 2;

	if (bc1p2->redetect_support)
		sprd_bc1p2_redetect_control(false);

	return type;
}

/*
 * 对外的检测入口（USB 框架通过 x->charger_detect 调）。
 * 分两条路：
 *   flags 里没有 DETECTING → 走完整流程（检测 + 必要时触发芯片重检测）
 *   flags 里已有 DETECTING → 说明重检测正在跑，直接读寄存器
 */
enum usb_charger_type sprd_bc1p2_charger_detect(struct usb_phy *x)
{
	enum usb_charger_type type;
	u32 val = BC1P2_REDET_DELAY_MAX;
	bool unstable = false;
	int ret;

	if (!bc1p2) {
		pr_err("%s:line%d: bc1p2 NULL pointer!!!\n", __func__, 337);
		return UNKNOWN_TYPE;
	}

	mutex_lock(&bc1p2->bc1p2_lock);

	if (!(x->flags & BC1P2_FL_DETECTING)) {
		type = sprd_bc1p2_detect();

		if (bc1p2->detect_state != 1) {
			dev_info(x->dev, "%s:line%d: type = UNKNOWN_TYPE\n", __func__, 353);
			type = UNKNOWN_TYPE;
			goto store;
		}

		if (!bc1p2->redetect_support) {
			/*
			 * 这套 PMIC 没有重检测寄存器：直接把“检测中”钉住，
			 * 顺便置 BIT(30)，避免上层反复触发。
			 */
			x->flags |= BC1P2_FL_DETECTING | BC1P2_FL_NO_REDETECT;
			dev_info(x->dev, "%s:line%d: type = %d\n", __func__, 360, type);
			goto store;
		}

		if (type != UNKNOWN_TYPE)
			goto log_type;

		/* 支持重检测但第一次没检出：报 UNKNOWN 后让芯片重新跑一遍 */
		dev_info(x->dev, "first_detect:type:0x%x\n", 0);
		x->chg_type = UNKNOWN_TYPE;
		usb_phy_notify_charger(x);

		ret = regmap_update_bits(bc1p2->regmap, bc1p2->pdata->reg2,
					 BC1P2_REDET_DELAY_MASK,
					 BC1P2_REDET_DELAY_MASK);
		if (ret) {
			type = SDP_TYPE;
			goto redetect_done;
		}

		ret = sprd_bc1p2_redetect_control(true);
		if (ret) {
			type = SDP_TYPE;
			goto redetect_done;
		}

		msleep(BC1P2_DET_POLL_MS);

		ret = regmap_read(bc1p2->regmap, bc1p2->pdata->reg, &val);
		if (ret) {
			type = DCP_TYPE;
			goto redetect_done;
		}

		if (!(val & BC1P2_DET_INT)) {
			type = CDP_TYPE;
			unstable = true;
			goto redetect_done;
		}

		if (val & BC1P2_DET_DONE) {
			type = ACA_TYPE;
			goto redetect_done;
		}

		det_delay_ms = 1;
		type = sprd_bc1p2_detect();
		goto log_type;

redetect_done:
		sprd_bc1p2_redetect_control(false);
		if (unstable)
			dev_warn(x->dev, "USB connection is unstable during redetect bc1p2\n");
		else
			dev_err(x->dev, "trigger redetect bc1p2 failed, error= %d\n", type);
		type = UNKNOWN_TYPE;
	} else {
		/* 重检测进行中：直接看寄存器里的原始类型位 */
		type = bc1p2->charger_type;
		if (!type) {
			val = 0;
			ret = regmap_read(bc1p2->regmap, bc1p2->pdata->reg, &val);
			type = UNKNOWN_TYPE;
			if (!ret && (val & BC1P2_DET_VALID) == BC1P2_DET_VALID) {
				switch (val & BC1P2_DET_TYPE_MASK) {
				case BC1P2_DET_TYPE_CDP:
					type = CDP_TYPE;
					break;
				case BC1P2_DET_TYPE_DCP:
					type = DCP_TYPE;
					break;
				case BC1P2_DET_TYPE_SDP:
					type = SDP_TYPE;
					break;
				default:
					type = UNKNOWN_TYPE;
					break;
				}
			}
			bc1p2->charger_type = type;
		}
		dev_info(x->dev, "%s:line%d: type = %d\n", __func__, 346, type);
		goto store;
	}

log_type:
	if (bc1p2->detect_state == 1) {
		x->flags |= BC1P2_FL_DETECTING;
		dev_info(x->dev, "%s:line%d: type = %d\n", __func__, 378, type);
	} else {
		dev_info(x->dev, "%s:line%d: type = UNKNOWN_TYPE\n", __func__, 372);
		type = UNKNOWN_TYPE;
	}

store:
	bc1p2->charger_type = type;
	mutex_unlock(&bc1p2->bc1p2_lock);

	return bc1p2->charger_type;
}
EXPORT_SYMBOL_GPL(sprd_bc1p2_charger_detect);

static const struct sprd_bc1p2_data sc2720_bc1p2_data = {
	.reg = 0xe14, .reg2 = 0xe18, .reg3 = 0x000, .mask = 0xe00, .shift = 9,
};

static const struct sprd_bc1p2_data sc2721_bc1p2_data = {
	.reg = 0xec8, .reg2 = 0xecc, .reg3 = 0x000, .mask = 0xe00, .shift = 9,
};

static const struct sprd_bc1p2_data sc2730_bc1p2_data = {
	.reg = 0x1b9c, .reg2 = 0x1ba0, .reg3 = 0x000, .mask = 0xe00, .shift = 9,
};

static const struct sprd_bc1p2_data sc2731_bc1p2_data = {
	.reg = 0xedc, .reg2 = 0xed8, .reg3 = 0x000, .mask = 0xe00, .shift = 9,
};

static const struct sprd_bc1p2_data ump9620_bc1p2_data = {
	.reg = 0x239c, .reg2 = 0x23a0, .reg3 = 0x243c, .mask = 0xf00, .shift = 8,
};

static const struct sprd_bc1p2_data ump518_bc1p2_data = {
	.reg = 0x1b9c, .reg2 = 0x1ba0, .reg3 = 0x1d24, .mask = 0xf00, .shift = 8,
};

/*
 * 顺序照 .rodata 里的表项顺序（每项 0xc8 字节：name[32]+type[32]+compatible[128]+data）。
 * 只有 ump9620 / ump518 有 reg3，也就是只有它们支持芯片自重检测。
 */
static const struct of_device_id sprd_bc1p2_of_match[] = {
	{ .compatible = "sprd,sc2720-bc1p2", .data = &sc2720_bc1p2_data },
	{ .compatible = "sprd,sc2721-bc1p2", .data = &sc2721_bc1p2_data },
	{ .compatible = "sprd,sc2730-bc1p2", .data = &sc2730_bc1p2_data },
	{ .compatible = "sprd,sc2731-bc1p2", .data = &sc2731_bc1p2_data },
	{ .compatible = "sprd,ump9620-bc1p2", .data = &ump9620_bc1p2_data },
	{ .compatible = "sprd,ump518-bc1p2", .data = &ump518_bc1p2_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_bc1p2_of_match);

static int sprd_bc1p2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* gfp 立即数 0xdc0 = GFP_KERNEL | __GFP_ZERO，且 .ko 只 import devm_kmalloc */
	bc1p2 = devm_kmalloc(dev, sizeof(*bc1p2), GFP_KERNEL | __GFP_ZERO);
	if (!bc1p2)
		return -ENOMEM;

	bc1p2->pdata = of_device_get_match_data(dev);
	if (!bc1p2->pdata) {
		dev_err(dev, "no matching driver data found\n");
		return -ENODEV;
	}

	/* regmap 挂在 parent（PMIC）上，本驱动自己不去创建 */
	bc1p2->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!bc1p2->regmap) {
		dev_err(dev, "failed to get regmap\n");
		return -ENODEV;
	}

	ret = regmap_update_bits(bc1p2->regmap, bc1p2->pdata->reg2,
				 bc1p2->pdata->mask,
				 2 << bc1p2->pdata->shift);
	if (ret)
		return ret;

	bc1p2->redetect_support = !!bc1p2->pdata->reg3;
	if (!bc1p2->redetect_support)
		dev_warn(dev, "no support chg_bc1p2 redetect\n");

	mutex_init(&bc1p2->bc1p2_lock);

	return 0;
}

static struct platform_driver sprd_bc1p2_driver = {
	.probe = sprd_bc1p2_probe,
	.driver = {
		.name = "sprd-bc1p2",
		.of_match_table = sprd_bc1p2_of_match,
	},
};

module_platform_driver(sprd_bc1p2_driver);

MODULE_AUTHOR("Yongzhi Chen <yongzhi.chen@unisoc.com>");
MODULE_DESCRIPTION("sprd bc1p2 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sprd_bc1p2");
