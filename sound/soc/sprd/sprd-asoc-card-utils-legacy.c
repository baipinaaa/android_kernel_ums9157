// SPDX-License-Identifier: GPL-2.0
/*
 * sprd-asoc-card-utils-legacy.c  --  Unisoc ASoC machine driver board hooks
 *
 * Reconstructed from snd-soc-sprd-card.ko (vendor kernel 5.4.210, T157).
 * Original file: .../audio_driver/sprd/machine/sprd_card/sprd-asoc-card-utils-legacy.c
 *
 * Function map (binary .text offsets):
 *   0x226c  1176 sprd_asoc_card_parse_ext_hook
 *   0x2704  1412 hook_general_spk
 *   0x28c0   620 hook_general_ctl
 *   0x2b30    56 select_mode_show
 *   0x2b68    92 select_mode_store
 *   0x2c08   128 sprd_asoc_card_ext_hook_free_gpio
 *   0x2de0    92 sprd_asoc_shutdown
 *   0x2e20   868 board_ext_pin_control   (__func__ "board_ext_hook")
 *   0x2f80   284 board_main_mic_event
 *   0x309c   284 board_sub_mic_event
 *   0x31b8   284 board_sub_mic2_event
 *   0x32d4   284 board_head_mic_event
 *   0x33f0   284 board_dig0_mic_event
 *   0x350c   284 board_dig1_mic_event
 *   0x3628   284 board_dig_fm_event
 *   0x3744   140 board_speaker_event
 *   0x37d0   140 board_speaker1_event
 *   0x385c   140 board_earpiece_event
 *   0x38e8   284 board_line_in_event
 *   0x3a04   132 board_headphone_event
 *   0x3a88   172 board_ext_enable
 *   0x3bd4    76 board_func_get
 *   0x3c20   152 board_func_set
 *   0x3cf8    80 board_mute_get
 *   0x3d48   152 board_mute_set
 *   0x3e20   196 smartamp_boost_get
 *   0x3e70   196 smartamp_boost_set
 *   0x3f34   232 sprd_asoc_board_comm_probe
 *   0x401c   336 sprd_asoc_board_comm_late_probe
 *   0x416c    16 sprd_asoc_ext_hook_register
 *   0x417c    32 sprd_asoc_ext_hook_unregister
 *   0x41c0   412 sprd_asoc_card_parse_smartamp_boost
 */

#define pr_fmt(fmt) "[ASoC:BOARD] " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/info.h>
#include <sound/jack.h>

#include "sprd-asoc-card-utils.h"

struct sprd_hook_spk_priv hook_spk_priv;
struct sprd_board_priv board;
u32 select_mode;

/* names of the 12 externally controllable audio paths */
const char *func_name[SPRD_MAX_EXT_FUNC] = {
	"Ext Spk",
	"Ext Spk1",
	"Ext Ear",
	"HeadPhone Jack",
	"Line Jack",
	"Mic Jack",
	"Aux Mic Jack",
	"Aux Mic2 Jack",
	"HP Mic Jack",
	"DMic Jack",
	"DMic1 Jack",
	"Dig FM Jack",
};

static struct kobject *ext_debug_kobj;

static int hook_general_spk(int type, int on);
static int hook_general_ctl(int id, int on);

/* mapping between the DT "mode" field and the hook implementation */
struct sprd_ext_hook_desc {
	const char *name;
	sprd_hook_fn fn;
	u32 mode;
};

static const struct sprd_ext_hook_desc ext_hook_arr[] = {
	{ "general_speaker", hook_general_spk, 1 },
	{ "rcv_through_ext_spk", hook_general_ctl, 1 },
};

/* ---------------------------------------------------------------------- */
/* hook implementations                                                   */
/* ---------------------------------------------------------------------- */
static int hook_general_spk(int type, int on)
{
	struct gpio_desc *desc;
	u32 gpio, share;

	if (type >= SPRD_MAX_EXT_PA)
		return -EINVAL;

	gpio = hook_spk_priv.gpio[type];
	if ((int)gpio < 0) {
		pr_err("%s gpio is invalid!\n", "hook_general_spk");
		return -EINVAL;
	}
	desc = gpio_to_desc(gpio);

	share = hook_spk_priv.share_gpio[type];
	if (share > 10)
		share = 0;

	pr_info("%s id: %d, gpio: %d, mode: %d, on: %d\n", "hook_general_spk",
		type, gpio, share, on);

	if (!on) {
		gpiod_set_raw_value(desc, 0);
		hook_spk_priv.state[type] = 0;
		msleep(1);
		return 1;
	}

	if (hook_spk_priv.state[type]) {
		pr_info("%s mode: %d, select_mode: %d\n", "hook_general_spk",
			hook_spk_priv.state[type], select_mode);
		share = hook_spk_priv.state[type];
	}
	hook_spk_priv.state[type] = share;

	spin_lock_irqsave(&hook_spk_priv.lock, 0);
	while (share >= 2) {
		gpiod_set_raw_value(desc, 1);
		__const_udelay(0x218e);
		gpiod_set_raw_value(desc, 0);
		__const_udelay(0x218e);
		share--;
	}
	gpiod_set_raw_value(desc, 1);
	spin_unlock_irqrestore(&hook_spk_priv.lock, 0);

	msleep(22);
	return 1;
}

static int hook_general_ctl(int id, int on)
{
	if (id >= SPRD_MAX_EXT_PA)
		return -EINVAL;

	if (id == 1 && hook_spk_priv.rcv_shared_multi_spk)
		return 1;

	if (id == 2) {
		if (hook_spk_priv.state[0] >= 1) {
			select_mode = on > 0 ? (u32)-1 : 0;
			pr_info("%s hook state {%d, %d, %d}\n", "hook_general_ctl",
				hook_spk_priv.state[0], hook_spk_priv.state[1],
				hook_spk_priv.state[2]);
			return 1;
		}
		hook_general_spk(2, on);
		if (hook_spk_priv.rcv_shared_multi_spk) {
			select_mode = on > 0 ? hook_spk_priv.share_gpio[2] : 0;
			hook_general_spk(1, on);
			select_mode = 0;
		}
		pr_info("%s hook state {%d, %d, %d}\n", "hook_general_ctl",
			hook_spk_priv.state[0], hook_spk_priv.state[1],
			hook_spk_priv.state[2]);
		return 1;
	}

	if (id == 0) {
		if (on && select_mode >= 1) {
			hook_general_spk(2, 0);
			if (hook_spk_priv.rcv_shared_multi_spk)
				hook_general_spk(1, 0);
			select_mode = (u32)-1;
		}
		hook_general_spk(0, on);
		if (hook_spk_priv.rcv_shared_multi_spk) {
			select_mode = 0;
			hook_general_spk(1, on);
		}
		if (!on && select_mode == (u32)-1) {
			hook_general_spk(2, 1);
			if (hook_spk_priv.rcv_shared_multi_spk)
				hook_general_spk(1, 1);
		}
		pr_info("%s hook state {%d, %d, %d}\n", "hook_general_ctl",
			hook_spk_priv.state[0], hook_spk_priv.state[1],
			hook_spk_priv.state[2]);
		return 1;
	}

	return hook_general_spk(id, on);
}

/* ---------------------------------------------------------------------- */
/* select_mode sysfs attribute (exposed under /sys/kernel/extpa)           */
/* ---------------------------------------------------------------------- */
static ssize_t select_mode_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", select_mode);
}

static ssize_t select_mode_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long long v;
	int ret;

	ret = kstrtoull(buf, 10, &v);
	if (!ret) {
		select_mode = (u32)v;
		pr_info("speaker ext pa select_mode = %d\n", select_mode);
	} else {
		pr_err("%s kstrtoul failed!(%d)\n", "select_mode_store", ret);
	}
	return count;
}

static struct kobj_attribute select_mode_attr =
	__ATTR(select_mode, 0444, select_mode_show, select_mode_store);

/* ---------------------------------------------------------------------- */
/* ext hook gpio management                                               */
/* ---------------------------------------------------------------------- */
void sprd_asoc_card_ext_hook_free_gpio(struct device *dev)
{
	int i;

	for (i = 0; i < SPRD_MAX_EXT_PA; i++) {
		if (!hook_spk_priv.used[i])
			continue;
		if ((int)hook_spk_priv.gpio[i] >= 0)
			gpio_free(hook_spk_priv.gpio[i]);
		hook_spk_priv.used[i] = 0;
	}
	pr_info("%s, gpio_freed\n", "sprd_asoc_card_ext_hook_free_gpio");
}
EXPORT_SYMBOL_GPL(sprd_asoc_card_ext_hook_free_gpio);

int sprd_asoc_card_parse_ext_hook(struct device *dev,
				  struct sprd_asoc_card_ext_hook *hook)
{
	struct device_node *np = dev->of_node;
	const struct {
		const char *name;
		sprd_hook_fn fn;
		u32 mode;
	} *tbl, *entry;
	struct of_phandle_args args;
	u32 info[4], i, cnt, type, mode;
	int ret, gpio, share;

	if (!hook) {
		pr_err("%s, Board special data is NULL!\n",
		       "sprd_asoc_card_parse_ext_hook");
		return -EINVAL;
	}

	if (of_find_property(np, "extral-iic-pa", NULL)) {
		pr_info("%s hook aw87xx iic pa!\n",
			"sprd_asoc_card_parse_hook");
		hook->fn[0] = hook_general_ctl;
		return 0;
	}

	cnt = of_property_count_elems_of_size(np, "sprd,spk-ext-pa-info", 4);
	if (cnt <= 0) {
		ret = cnt;
		if (ret == 0)
			goto rcv_shared;
		pr_info("Count '%s' failed!(%d)\n", "sprd,spk-ext-pa-info", ret);
		goto rcv_shared;
	}

	if (cnt & 3) {
		pr_err("Spk pa info is not a multiple of %d.\n", 4);
		return -EINVAL;
	}

	if (cnt >= 0x34) {
		pr_err("Speaker count %d is greater than %d!\n", cnt / 4,
		       SPRD_MAX_EXT_PA);
		cnt = 0x30;
	}

	for (i = 0; i < cnt; i += 4) {
		ret = of_property_read_u32_array(np, "sprd,spk-ext-pa-info",
						 info, 4);
		if (ret) {
			pr_err("Read property '%s' failed!\n",
			       "sprd,spk-ext-pa-info");
			return ret;
		}
		type = info[0];
		mode = info[1];
		if (type >= SPRD_MAX_EXT_PA) {
			pr_err("Ext ctrl type %d is invalid!\n", type);
			continue;
		}

		if (mode < ARRAY_SIZE(ext_hook_arr))
			hook->fn[type] = ext_hook_arr[mode].fn;
		else {
			pr_err("Hook selection %d is invalid!\n", mode);
			hook->fn[type] = NULL;
		}

		pr_info("ext_ctrl_type %d hook_sel %d priv_data %d gpio %d\n",
			type, mode, info[2], info[3]);

		share = info[3];
		if (share) {
			if (share > i / 4) {
				pr_err("share_gpio %d is bigger than spk_cnt!\n",
				       share);
				continue;
			}
			hook_spk_priv.gpio[type] =
				hook_spk_priv.gpio[share - 1];
		} else {
			gpio = of_get_named_gpio_flags(np, "sprd,spk-ext-pa-gpio",
						       i / 4, NULL);
			if (gpio < 0) {
				pr_err("Get gpio for '%s' failed(%d)!\n",
				       "sprd,spk-ext-pa-gpio", gpio);
				pr_err("Get gpio failed:%d!\n", gpio);
				continue;
			}
			hook_spk_priv.gpio[type] = gpio;
			ret = gpio_request_one(gpio, 2, NULL);
			if (ret) {
				pr_err("Gpio request[%d] failed:%d!\n", gpio, ret);
				continue;
			}
		}
		hook_spk_priv.share_gpio[type] = share;
		hook_spk_priv.used[type] = 1;
		pr_info("Gpio request[%d] ret:%d! hook_p = %p, ext_ctrl_p = %p \n",
			hook_spk_priv.gpio[type], 0, hook, hook->fn[type]);
	}

	pr_info("%s, spk cnt = %d, ext_ctrl_type = %d\n",
		"sprd_asoc_card_parse_ext_hook", cnt / 4, i / 4);

rcv_shared:
	if (!of_property_read_u32(np, "sprd,rcv_shared_multi_spk",
				  &hook_spk_priv.rcv_shared_multi_spk))
		pr_info("[%s][20210525] rcv shared with multi spk: %d\n",
			"sprd_asoc_card_parse_ext_hook",
			hook_spk_priv.rcv_shared_multi_spk);

	if (!hook_spk_priv.rcv_shared_multi_spk)
		return 0;

	/* register the select_mode attribute once, under /sys/kernel/extpa */
	if (!ext_debug_kobj) {
		ext_debug_kobj = kobject_create_and_add("extpa", kernel_kobj);
		if (ext_debug_kobj)
			sysfs_create_file_ns(ext_debug_kobj,
					     &select_mode_attr.attr, NULL);
	}

	(void)args;
	(void)entry;
	(void)tbl;
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_card_parse_ext_hook);

/* ---------------------------------------------------------------------- */
/* board pin control                                                      */
/* ---------------------------------------------------------------------- */
static void board_ext_pin_control(struct snd_soc_dapm_context *dapm,
				  int from, int to)
{
	int i;

	if (!dapm) {
		pr_err("%s mdata is NULL\n", "board_ext_hook");
		return;
	}

	if (from >= SPRD_MAX_EXT_FUNC) {
		pr_err("%s start func id(%d) is invalid.\n", "board_ext_hook",
		       from);
		return;
	}
	if (to > SPRD_MAX_EXT_FUNC) {
		pr_err("%s end func id(%d) is invalid.\n", "board_ext_hook", to);
		return;
	}

	pr_debug("function id(%d), on=%d\n", from, to);

	for (i = from; i < to; i++) {
		if (board.func_state[i] == 1) {
			if (i <= 3) {
				if (board.ext[i].state)
					snd_soc_dapm_disable_pin(dapm,
								 func_name[i]);
				else
					snd_soc_dapm_enable_pin(dapm,
								func_name[i]);
			} else {
				snd_soc_dapm_enable_pin(dapm, func_name[i]);
			}
		} else {
			snd_soc_dapm_disable_pin(dapm, func_name[i]);
		}
	}

	snd_soc_dapm_sync(dapm);
}

/* ---------------------------------------------------------------------- */
/* ext hook dispatcher                                                    */
/* ---------------------------------------------------------------------- */
static int board_ext_enable(int on, int func_id)
{
	sprd_hook_fn fn;
	int val, ret = on;

	val = on ? (board.ext[func_id].state ? on : 0) : 0;

	if (sp_audio_debug_flag & 1)
		pr_debug("%s func_id %d enable %d\n", "board_ext_enable",
			 func_id, val);

	if (!g_card_ext_hook)
		return ret;

	fn = g_card_ext_hook->fn[func_id];
	if (!fn)
		return ret;

	ret = fn(func_id, val);
	if (ret < 0)
		pr_err("ERR:Call external earpiece control failed %d!\n", ret);

	return ret;
}

/* ---------------------------------------------------------------------- */
/* board widget event callbacks                                           */
/* ---------------------------------------------------------------------- */
#define SPRD_EXT_LINE_IN	4
#define SPRD_EXT_MAIN_MIC	5
#define SPRD_EXT_SUB_MIC	6
#define SPRD_EXT_SUB_MIC2	7
#define SPRD_EXT_HEAD_MIC	8
#define SPRD_EXT_DIG0_MIC	9
#define SPRD_EXT_DIG1_MIC	10
#define SPRD_EXT_DIG_FM		11

static int board_hook_event(int event, int id, const char *fmt)
{
	int on = (event & 3) != 0;

	if (sp_audio_debug_flag & 1) {
		pr_debug(fmt, on ? "on" : "off");
		pr_debug("%s Switch %s\n", func_name[id], on ? "on" : "off");
	}

	if (g_card_ext_hook && g_card_ext_hook->fn[id])
		g_card_ext_hook->fn[id](id, on);

	return 0;
}

static int board_main_mic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_MAIN_MIC,
				"[ASoC:BOARD] Main MIC Switch %s\n");
}

static int board_sub_mic_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_SUB_MIC,
				"[ASoC:BOARD] Sub MIC Switch %s\n");
}

static int board_sub_mic2_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_SUB_MIC2,
				"[ASoC:BOARD] Sub MIC2 Switch %s\n");
}

static int board_head_mic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_HEAD_MIC,
				"[ASoC:BOARD] Head MIC Switch %s\n");
}

static int board_dig0_mic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_DIG0_MIC,
				"[ASoC:BOARD] Digtial0 MIC Switch %s\n");
}

static int board_dig1_mic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_DIG1_MIC,
				"[ASoC:BOARD] Digtial1 MIC Switch %s\n");
}

static int board_dig_fm_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_DIG_FM,
				"[ASoC:BOARD] Digtial FM Switch %s\n");
}

static int board_line_in_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event)
{
	return board_hook_event(event, SPRD_EXT_LINE_IN,
				"[ASoC:BOARD] LINE IN Switch %s\n");
}

static int board_speaker_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event)
{
	int on = (event & 3) != 0;

	if (sp_audio_debug_flag & 1)
		pr_debug("[ASoC:BOARD] Speaker Switch %s\n", on ? "on" : "off");
	board.ext[0].on = on;
	board_ext_enable(on, 0);
	return 0;
}

static int board_speaker1_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	int on = (event & 3) != 0;

	if (sp_audio_debug_flag & 1)
		pr_debug("[ASoC:BOARD] Speaker2 Switch %s\n", on ? "on" : "off");
	board.ext[1].on = on;
	board_ext_enable(on, 1);
	return 0;
}

static int board_earpiece_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	int on = (event & 3) != 0;

	if (sp_audio_debug_flag & 1)
		pr_debug("[ASoC:BOARD] Earpiece Switch %s\n", on ? "on" : "off");
	board.ext[2].on = on;
	board_ext_enable(on, 2);
	return 0;
}

static int board_headphone_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	int on = (event & 3) != 0;

	if (sp_audio_debug_flag & 1)
		pr_debug("[ASoC:BOARD] Headphone Switch %s\n", on ? "on" : "off");
	board_ext_enable(on, 3);
	return 0;
}

/* ---------------------------------------------------------------------- */
/* board kcontrol callbacks                                               */
/* ---------------------------------------------------------------------- */
static int board_func_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kcontrol->private_value;
	unsigned int idx = ~mc->reg & 0xffff;

	if (idx >= SPRD_MAX_EXT_FUNC)
		return -EINVAL;

	ucontrol->value.integer.value[0] = board.func_state[idx];
	return 0;
}

static int board_func_set(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kcontrol->private_value;
	struct snd_soc_card *card = kcontrol->private_data;
	unsigned int idx = ~mc->reg & 0xffff;
	unsigned long val = ucontrol->value.integer.value[0];

	if (idx >= SPRD_MAX_EXT_FUNC)
		return -EINVAL;

	if (board.func_state[idx] == val)
		return 0;

	board.func_state[idx] = val;
	board_ext_pin_control(&card->dapm, idx, BIT(idx) ? 0 : 0);
	return 1;
}

static int board_mute_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kcontrol->private_value;
	unsigned int idx = ~mc->reg & 0xffff;

	if (idx >= 4)
		return -EINVAL;

	ucontrol->value.integer.value[0] = board.ext[idx].state;
	return 0;
}

static int board_mute_set(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kcontrol->private_value;
	struct snd_soc_card *card = kcontrol->private_data;
	unsigned int idx = ~mc->reg & 0xffff;
	unsigned long val = ucontrol->value.integer.value[0];

	if (idx >= 4)
		return -EINVAL;

	if (board.ext[idx].state == val)
		return 0;

	board.ext[idx].state = val;
	board_ext_pin_control(&card->dapm, idx, idx);
	return 1;
}

/* ---------------------------------------------------------------------- */
/* smartamp boost                                                         */
/* ---------------------------------------------------------------------- */
int smartamp_boost_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = kcontrol->private_data;
	struct sprd_asoc_card_priv *priv = sprd_card_priv(card);

	if (!priv) {
		pr_err("%s, Board special data is NULL!\n",
		       "smartamp_boost_get");
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = priv->boost.mode;
	return 0;
}

int smartamp_boost_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = kcontrol->private_data;
	struct sprd_asoc_card_priv *priv = sprd_card_priv(card);
	struct soc_enum *e = (void *)kcontrol->private_value;
	unsigned long mode = ucontrol->value.integer.value[0];
	const char *name = "Off";

	if (!priv) {
		pr_err("%s, Board special data is NULL!\n",
		       "smartamp_boost_set");
		return -EINVAL;
	}

	if (mode > e->max - 1) {
		pr_err("Invalid item(%d) for hp sel\n", (int)mode);
		return -EINVAL;
	}

	priv->boost.mode = mode;
	if (e->texts && e->texts[mode])
		name = e->texts[mode];

	/* the boost gpio is optional, keep the vendor behaviour */
	if (gpio_is_valid(priv->boost.mode_gpio))
		gpiod_set_raw_value(gpio_to_desc(priv->boost.mode_gpio),
				    mode != 0);

	pr_debug("smartamp boost mode: '%s'\n", name);
	return 0;
}

int sprd_asoc_card_parse_smartamp_boost(struct device *dev,
					struct sprd_smartamp_boost *boost)
{
	struct device_node *np = dev->of_node;
	int idx, gpio;

	if (!boost) {
		pr_err("Board special data is NULL!\n");
		return -EINVAL;
	}

	if (!of_find_property(np, "sprd,smartamp-boost-names", NULL))
		return 0;

	idx = of_property_match_string(np, "sprd,smartamp-boost-names",
				       "boost_enable");
	if (idx < 0) {
		pr_err("No match found for '%s' gpio!\n", "boost_enable");
		return idx;
	}
	gpio = of_get_named_gpio_flags(np, "sprd,smartamp-boost-gpios", idx,
				       NULL);
	if (gpio_is_valid(gpio)) {
		boost->enable_gpio = gpio;
		if (devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW,
					  "boost_enable"))
			pr_err("Gpio '%s' request failed(%d)!\n",
			       "boost_enable", gpio);
	}

	idx = of_property_match_string(np, "sprd,smartamp-boost-names",
				       "boost_mode");
	if (idx < 0) {
		pr_err("No match found for '%s' gpio!\n", "boost_mode");
		return idx;
	}
	gpio = of_get_named_gpio_flags(np, "sprd,smartamp-boost-gpios", idx,
				       NULL);
	if (gpio_is_valid(gpio)) {
		boost->mode_gpio = gpio;
		if (devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW,
					  "boost_mode"))
			pr_err("Gpio '%s' request failed(%d)!\n",
			       "boost_mode", gpio);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_card_parse_smartamp_boost);

/* ---------------------------------------------------------------------- */
/* board common probe / late probe                                        */
/* ---------------------------------------------------------------------- */
int sprd_asoc_board_comm_probe(struct snd_soc_card *card)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (sp_audio_debug_flag & 1)
			pr_debug("function id(%d), on=%d\n", i, 0);
		if (g_card_ext_hook && g_card_ext_hook->fn[i])
			g_card_ext_hook->fn[i](i, 0);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_board_comm_probe);

int sprd_asoc_board_comm_late_probe(struct snd_soc_card *card)
{
	struct snd_info_entry *entry;
	int i;

	entry = snd_info_create_card_entry(card->snd_card, "asoc-sprd-debug",
					   card->snd_card->proc_root);
	if (entry) {
		entry->private_data = &sp_audio_debug_flag;
		entry->mode |= 0x80;
		entry->c.read = snd_pcm_sprd_debug_read;
		entry->c.write = snd_pcm_sprd_debug_write;
		if (snd_info_register(entry) < 0)
			snd_info_free_entry(entry);
	}

	board_ext_pin_control(&card->dapm, 0, SPRD_MAX_EXT_FUNC);

	for (i = 0; i < SPRD_MAX_EXT_FUNC; i++)
		snd_soc_dapm_ignore_suspend(&card->dapm, func_name[i]);

	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_board_comm_late_probe);

void sprd_asoc_shutdown(struct snd_soc_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	memset(&board, 0, offsetof(struct sprd_board_priv, ext));
	board_ext_pin_control(&card->dapm, 0, SPRD_MAX_EXT_FUNC);
}
EXPORT_SYMBOL_GPL(sprd_asoc_shutdown);

/* ---------------------------------------------------------------------- */
/* ext hook registration                                                  */
/* ---------------------------------------------------------------------- */
int sprd_asoc_ext_hook_register(void *hook)
{
	g_card_ext_hook = hook;
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_ext_hook_register);

int sprd_asoc_ext_hook_unregister(void *hook)
{
	if (g_card_ext_hook != hook) {
		pr_err("ERR:Maybe Unregister other's Hook?\n");
		return -EBUSY;
	}
	g_card_ext_hook = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_asoc_ext_hook_unregister);
