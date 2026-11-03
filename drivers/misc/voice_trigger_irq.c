// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc/Spreadtrum voice-trigger EIC interrupt driver.
 *
 * Source-level reconstruction of the vendor module "voice_trigger_irq.ko"
 * (author VTS, licence GPL v2, depends on agdsp_access) taken from the
 * vendor_boot ramdisk of the Sunelan Q968 (UMS9157 / T157).
 *
 * The driver only brings up one threaded IRQ taken from the
 * "sprd,voice_trigger_irq" device tree node.  The EIC (external interrupt
 * controller) line is routed through the always-on AGCP block, so the
 * handler has to power the ADSP access path up, acknowledge the EIC bit in
 * the syscon and then read the status back for the log.
 *
 * Recovered from the binary:
 *   - private data is a 40-byte devm_kmalloc'ed object
 *     { int irq; int unused; struct device *dev; struct wakeup_source *ws;
 *       struct regmap *agcp_glb; u32 reg; u32 mask; }
 *     (reg/mask come from the two cells of the "audcp_glb_eic_int"
 *     property, agcp_glb from "sprd,syscon-agcp-glb");
 *   - the wakeup source is named "vts_wake", the IRQ "eic_vts";
 *   - flags are IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_SUSPEND
 *     (0x6004), the threaded handler is sprd_eic_vts_handler();
 *   - the RTOS notification is a 1000 ms pm_wakeup_ws_event();
 *   - every log line is prefixed "[voice-trigger-irq] " and the driver name
 *     string really is "voice-triggrt-irq" (vendor typo, kept verbatim).
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Provided by agdsp_access.ko. */
extern int agdsp_access_enable(void);
extern void agdsp_access_disable(void);

#define VTS_TAG		"[voice-trigger-irq] "

#define vts_err(fmt, ...)	printk(KERN_ERR VTS_TAG fmt, ##__VA_ARGS__)
#define vts_info(fmt, ...)	printk(KERN_INFO VTS_TAG fmt, ##__VA_ARGS__)
#define vts_warn(fmt, ...)	printk(KERN_WARNING VTS_TAG fmt, ##__VA_ARGS__)

struct vts_priv {
	int irq;
	int unused;
	struct device *dev;
	struct wakeup_source *ws;
	struct regmap *agcp_glb;
	u32 reg;
	u32 mask;
};

static irqreturn_t sprd_eic_vts_handler(int irq, void *data)
{
	struct vts_priv *vts = data;
	u32 reg_val = 0;

	vts_info("%s enter\n", __func__);

	if (!vts || !vts->agcp_glb) {
		vts_err("%s voice_trigger_irq or agcp_glb NULL\n", __func__);
		return IRQ_HANDLED;
	}

	if (agdsp_access_enable()) {
		dev_err(vts->dev, "%s agdsp_access_enable error\n", __func__);
		return IRQ_HANDLED;
	}

	if (regmap_update_bits(vts->agcp_glb, vts->reg, vts->mask, 0)) {
		vts_err("%s, regmap_update_bits AUDACCESS_APB_AGCP_CTRL error!\n",
			__func__);
	} else {
		if (vts->ws)
			pm_wakeup_ws_event(vts->ws, 1000, 0);

		if (regmap_read(vts->agcp_glb, vts->reg, &reg_val))
			vts_err("%s, regmap_update_bits audcp_glb_eic_int_reg error!\n",
				__func__);

		vts_info("%s:reg_val is %d\n", __func__, reg_val);
	}

	agdsp_access_disable();

	return IRQ_HANDLED;
}

static int voice_trigger_irq_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct vts_priv *vts;
	struct regmap *agcp_glb;
	u32 args[2];
	int irq, ret;

	vts = devm_kmalloc(&pdev->dev, sizeof(*vts), GFP_KERNEL);
	if (!vts) {
		vts_err("%s:Failed to allocate\n", __func__);
		return -ENOMEM;
	}

	ret = 0;
	if (syscon_regmap_lookup_by_phandle_args(np, "audcp_glb_eic_int",
						 2, args) == NULL)
		vts_err("audcp_glb_eic_int not exist.\n");
	else
		vts_info("audcp_glb_eic_int:reg:%x,mask:%x\n", args[0], args[1]);

	vts->reg = args[0];
	vts->mask = args[1];

	agcp_glb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-agcp-glb");
	if (IS_ERR(agcp_glb)) {
		vts_err("%s,audcp aon apb syscon not exist  %ld\n", __func__,
			PTR_ERR(agcp_glb));
		agcp_glb = NULL;
	}

	vts->agcp_glb = agcp_glb;
	vts->dev = &pdev->dev;
	platform_set_drvdata(pdev, vts);

	irq = platform_get_irq(pdev, 0);
	vts->irq = irq;
	vts_info("vts:irq:%d\n", irq);

	vts->ws = wakeup_source_register(&pdev->dev, "vts_wake");

	if (irq < 0) {
		dev_warn(&pdev->dev, "no irq resource: %d\n", irq);
		return -1;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					sprd_eic_vts_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT |
					IRQF_NO_SUSPEND, "eic_vts", vts);
	if (ret < 0) {
		vts_err("failed to request IRQ_%d\n", irq);
		if (vts->ws)
			wakeup_source_unregister(vts->ws);
	}

	return ret;
}

static int voice_trigger_irq_remove(struct platform_device *pdev)
{
	struct vts_priv *vts = platform_get_drvdata(pdev);

	devm_kfree(&pdev->dev, vts);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id voice_trigger_irq_of_match[] = {
	{ .compatible = "sprd,voice_trigger_irq" },
	{ }
};
MODULE_DEVICE_TABLE(of, voice_trigger_irq_of_match);

static struct platform_driver voice_trigger_irq_driver = {
	.probe = voice_trigger_irq_probe,
	.remove = voice_trigger_irq_remove,
	.driver = {
		.name = "voice-triggrt-irq",
		.of_match_table = voice_trigger_irq_of_match,
	},
};

static int __init voice_trigger_irq_init(void)
{
	return platform_driver_register(&voice_trigger_irq_driver);
}

static void __exit voice_trigger_irq_exit(void)
{
	platform_driver_unregister(&voice_trigger_irq_driver);
}

module_init(voice_trigger_irq_init);
module_exit(voice_trigger_irq_exit);

MODULE_DESCRIPTION("UNISOC Voice Trigger interrupt driver");
MODULE_AUTHOR("Jinbo Yang <jinbo.yang@unisoc.com>");
MODULE_LICENSE("GPL v2");
