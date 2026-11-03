// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm touch reporting.
 *
 * The report the sensor sends is not self describing: the application firmware
 * hands out a "touch report config" blob through CMD_GET_TOUCH_REPORT_CONFIG
 * that tells us where every field sits inside the payload, in bits.  This file
 * keeps that description, walks it for every report and translates the result
 * into Linux input events.
 *
 * Copyright (C) 2017 - 2018 Droid Division
 */

#include "synaptics_tcm_core.h"

#define TOUCH_REPORT_CONFIG_OFFSET		0x00
#define TOUCH_REPORT_CONFIG_COMMAND		0x25
#define TOUCH_REPORT_CONFIG_ITEM_COUNT		0x83

#define TOUCH_OBJECT_DATA_SIZE			0x20

struct syna_tcm_touch_hcd {
	u8 live;				/* +0x000 */
	u8 input_reporting;			/* +0x001 */
	u8 in_suspend;				/* +0x002 */
	u8 pad0[0x05];				/* +0x003 */
	u8 *prev_status;			/* +0x008 */
	u32 max_x;				/* +0x010 */
	u32 max_y;				/* +0x014 */
	u32 max_objects;			/* +0x018 */
	u8 pad1[0x04];				/* +0x01c */
	struct mutex report_mutex;		/* +0x020 */
	struct input_dev *input_dev;		/* +0x040 */
	void *object_data;			/* +0x048 */
	u32 timestamp;				/* +0x050 */
	u32 buttons;				/* +0x054 */
	u32 gesture_double_tap;			/* +0x058 */
	u32 frame_rate;				/* +0x05c */
	u32 power_im;				/* +0x060 */
	u32 cid_im;				/* +0x064 */
	u32 rail_im;				/* +0x068 */
	u32 cid_variance_im;			/* +0x06c */
	u32 nsm_frequency;			/* +0x070 */
	u32 nsm_state;				/* +0x074 */
	u32 num_active;				/* +0x078 */
	u32 num_cpu_cycles;			/* +0x07c */
	u32 face_detect;			/* +0x080 */
	u32 force;				/* +0x084 */
	u32 fingerprint_area_meet;		/* +0x088 */
	u8 pad2[0x04];				/* +0x08c */
	u32 prev_max_x;				/* +0x090 */
	u32 prev_max_y;				/* +0x094 */
	u32 prev_max_objects;			/* +0x098 */
	u8 pad3[0x04];				/* +0x09c */
	u8 out_init;				/* +0x0a0 */
	u8 pad4[0x07];				/* +0x0a1 */
	u8 *out_buf;				/* +0x0a8 */
	u32 out_buf_size;			/* +0x0b0 */
	u8 pad5[0x04];				/* +0x0b4 */
	struct mutex out_mutex;			/* +0x0b8 */
	u8 resp_init;				/* +0x0d8 */
	u8 pad6[0x07];				/* +0x0d9 */
	u8 *resp_buf;				/* +0xe0 */
	u32 resp_buf_size;			/* +0xe8 */
	u8 pad7[0x04];				/* +0x0ec */
	struct mutex resp_mutex;		/* +0x0f0 */
	struct syna_tcm_hcd *tcm_hcd;		/* +0x110 */
};

static struct syna_tcm_touch_hcd *touch_hcd;

/*
 * Offset of the object that was being parsed when the report config ran out of
 * items, kept in .bss so that the next report can pick it up again.
 */
static u32 touch_parse_report_end_of_foreach;

static void touch_report(void);
static int touch_set_input_dev(void);

static void touch_release_buffers(struct syna_tcm_touch_hcd *hcd)
{
	if (!hcd->resp_init) {
		kfree(hcd->resp_buf);
		hcd->resp_buf_size = 0;
	}

	if (!hcd->out_init) {
		kfree(hcd->out_buf);
		hcd->out_buf_size = 0;
	}
}

/*
 * Lift every finger the input device still has down.  Called before the
 * parameters change and whenever the sensor stops reporting, so that the
 * stack never ends up with a slot that was never released.
 */
static void touch_free_objects(void)
{
	int i;

	if (!touch_hcd->input_dev)
		return;

	mutex_lock(&touch_hcd->report_mutex);

	for (i = 0; i < touch_hcd->max_objects; i++) {
		input_event(touch_hcd->input_dev, EV_ABS, ABS_MT_SLOT, i);
		input_mt_report_slot_state(touch_hcd->input_dev,
				MT_TOOL_FINGER, 0);
	}

	input_event(touch_hcd->input_dev, EV_KEY, BTN_TOUCH, 0);
	input_event(touch_hcd->input_dev, EV_KEY, BTN_TOOL_FINGER, 0);
	input_event(touch_hcd->input_dev, EV_SYN, SYN_REPORT, 0);

	mutex_unlock(&touch_hcd->report_mutex);
}

static int touch_set_input_params(void)
{
	if (touch_hcd->max_objects) {
		kfree(touch_hcd->prev_status);

		touch_hcd->prev_status = kmalloc(touch_hcd->max_objects,
				GFP_KERNEL);
		if (!touch_hcd->prev_status) {
			dev_err(&touch_hcd->tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to allocate memory for touch_hcd->prev_status\n",
					"touch_set_input_params", 0x327);
			return -ENOMEM;
		}
	}

	return 0;
}

static int touch_set_input_dev(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	struct input_dev *input_dev;
	int retval;

	input_dev = input_allocate_device();

	touch_hcd->input_dev = input_dev;

	if (!input_dev) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate input device\n",
				"touch_set_input_dev", 0x36f);
		return -ENODEV;
	}

	input_dev->name = "synaptics_tcm_touch";
	input_dev->phys = "synaptics_tcm/touch_input";
	input_dev->id.version = 1;
	input_dev->id.product = 0x0201;
	input_dev->dev.parent = &tcm_hcd->pdev->dev;
	input_dev->dev.driver_data = tcm_hcd;

	input_dev->evbit[0] |= BIT_MASK(EV_SYN);
	input_dev->evbit[0] |= BIT_MASK(EV_KEY);
	input_dev->evbit[0] |= BIT_MASK(EV_ABS);

	input_dev->keybit[BIT_WORD(BTN_TOUCH)] |= BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_TOOL_FINGER)] |=
		BIT_MASK(BTN_TOOL_FINGER);

	input_dev->propbit[0] |= BIT_MASK(INPUT_PROP_DIRECT);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
			touch_hcd->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			touch_hcd->max_y, 0, 0);

	input_mt_init_slots(input_dev, touch_hcd->max_objects, INPUT_MT_DIRECT);

	touch_hcd->prev_max_x = touch_hcd->max_x;
	touch_hcd->prev_max_y = touch_hcd->max_y;
	touch_hcd->prev_max_objects = touch_hcd->max_objects;

	retval = touch_set_input_params();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to set input parameters\n",
				"touch_set_input_dev", 0x38b);
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return -ENOMEM;
	}

	retval = input_register_device(input_dev);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to register input device\n",
				"touch_set_input_dev", 0x394);
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}

	return 0;
}

static int touch_get_input_params(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	int retval;

	retval = tcm_write_message(tcm_hcd, TOUCH_REPORT_CONFIG_COMMAND,
			NULL, 0, &tcm_hcd->config.buf,
			&tcm_hcd->config.buf_size,
			&tcm_hcd->config.data_length, NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				"touch_get_input_params", 0x356,
				"CMD_GET_TOUCH_REPORT_CONFIG");
		return retval;
	}

	return 0;
}

/*
 * The report config is the only source of truth for the input device limits:
 * the sensor may come back with a different panel geometry after a firmware
 * update, and the config is fetched before every rebuild.
 */
static int touch_set_input_reporting(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	struct syna_tcm_board_data *bdata;
	int retval;

	if ((u8)(tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		dev_notice(&tcm_hcd->pdev->dev,
				"%s info: Identifying mode = 0x%02x\n",
				"touch_set_input_reporting",
				tcm_hcd->identify_info[1]);
		return 0;
	}

	touch_hcd->input_reporting = 0;

	touch_free_objects();

	mutex_lock(&touch_hcd->report_mutex);

	bdata = tcm_hcd->hw_if->bdata;

	touch_hcd->max_x = get_unaligned_le16(&tcm_hcd->app_info[0x20]);
	touch_hcd->max_y = get_unaligned_le16(&tcm_hcd->app_info[0x22]);
	touch_hcd->max_objects = get_unaligned_le16(&tcm_hcd->app_info[0x24]);

	if (bdata->swap_axes) {
		u32 temp = touch_hcd->max_x;

		touch_hcd->max_x = touch_hcd->max_y;
		touch_hcd->max_y = temp;
	}

	mutex_lock(&tcm_hcd->config.buf_mutex);

	retval = touch_get_input_params();

	mutex_unlock(&tcm_hcd->config.buf_mutex);

	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get input parameters\n",
				"touch_set_input_reporting", 0x43b);
		goto out;
	}

	if (!touch_hcd->max_x && !touch_hcd->max_y) {
		dev_notice(&tcm_hcd->pdev->dev,
				"%s info: Input parameters unchanged\n",
				"touch_set_input_reporting");
		retval = 0;
		goto out;
	}

	if (touch_hcd->prev_max_objects != touch_hcd->max_objects) {
		kfree(touch_hcd->object_data);

		touch_hcd->object_data = kmalloc(touch_hcd->max_objects << 5,
				GFP_KERNEL);
		if (!touch_hcd->object_data) {
			dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to allocate memory for touch_hcd->touch_data.object_data\n",
					"touch_check_input_params", 0x407);
			dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to check input parameters\n",
					"touch_set_input_reporting", 0x442);
			retval = -ENOMEM;
			goto out;
		}
	}

	if (touch_hcd->prev_max_x == touch_hcd->max_x &&
			touch_hcd->prev_max_y == touch_hcd->max_y) {
		dev_notice(&tcm_hcd->pdev->dev,
				"%s info: Input parameters unchanged\n",
				"touch_set_input_reporting");
		retval = 0;
		goto out;
	}

	if (touch_hcd->input_dev) {
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
	}

	retval = touch_set_input_dev();
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to set up input device\n",
				"touch_set_input_reporting", 0x452);

out:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->input_reporting = (retval < 0) ? 0 : 1;

	return retval;
}

static int touch_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	touch_hcd = kmem_cache_alloc(touch_hcd_cache, GFP_KERNEL);
	if (!touch_hcd) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for touch_hcd\n",
				"touch_init", 0x466);
		return -ENOMEM;
	}

	touch_hcd->tcm_hcd = tcm_hcd;

	mutex_init(&touch_hcd->report_mutex, "&touch_hcd->report_mutex");
	mutex_init(&touch_hcd->out_mutex, "&touch_hcd->out.buf_mutex");
	mutex_init(&touch_hcd->resp_mutex, "&touch_hcd->resp.buf_mutex");

	touch_hcd->out_init = 0;
	touch_hcd->resp_init = 0;

	retval = touch_set_input_reporting();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to set up input reporting\n",
				"touch_init", 0x474);
		kfree(touch_hcd->object_data);
		kfree(touch_hcd->prev_status);
		touch_release_buffers(touch_hcd);
		kfree(touch_hcd);
		touch_hcd = NULL;
		return retval;
	}

	tcm_hcd->touch_report_fn = (void *)touch_report;

	return 0;
}

static int touch_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (!touch_hcd)
		return 0;

	tcm_hcd->touch_report_fn = NULL;

	if (touch_hcd->input_dev)
		input_unregister_device(touch_hcd->input_dev);

	kfree(touch_hcd->object_data);
	kfree(touch_hcd->prev_status);
	touch_release_buffers(touch_hcd);
	kfree(touch_hcd);
	touch_hcd = NULL;

	return 0;
}

static int touch_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return touch_init(tcm_hcd);

	touch_free_objects();

	if ((u8)(tcm_hcd->identify_info[1] - 1) >= 2) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Application mode is not running (firmware mode = %d)\n",
				"touch_reinit", 0x4af,
				tcm_hcd->identify_info[1]);
		return 0;
	}

	retval = tcm_hcd->identify_fn(tcm_hcd, false);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do identification\n",
				"touch_reinit", 0x4b6);
		return retval;
	}

	retval = touch_set_input_reporting();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to set up input reporting\n",
				"touch_reinit", 0x4bd);
		return retval;
	}

	return retval;
}

int touch_early_suspend(struct syna_tcm_hcd *tcm_hcd)
{
	if (touch_hcd) {
		touch_hcd->in_suspend = !tcm_hcd->irq_no_disable;
		touch_free_objects();
	}

	return 0;
}

int touch_suspend(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	touch_hcd->in_suspend = 1;

	touch_free_objects();

	if (!tcm_hcd->irq_no_disable)
		return 0;

	if (!touch_hcd->live) {
		irq_set_irq_wake(tcm_hcd->irq, 1);
		touch_hcd->live = 1;
	}

	touch_hcd->in_suspend = 0;

	retval = tcm_hcd->set_dynamic_config_fn(tcm_hcd,
			DC_READ_RAW_DATA, 1);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to enable wakeup gesture mode\n",
				"touch_suspend", 0x4ea);
		return retval;
	}

	return 0;
}

int touch_resume(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	touch_hcd->in_suspend = 0;

	if (!tcm_hcd->irq_no_disable)
		return 0;

	if (touch_hcd->live) {
		irq_set_irq_wake(tcm_hcd->irq, 0);
		touch_hcd->live = 0;
	}

	retval = tcm_hcd->set_dynamic_config_fn(tcm_hcd,
			DC_READ_RAW_DATA, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to disable wakeup gesture mode\n",
				"touch_resume", 0x506);
		return retval;
	}

	return 0;
}
/*
 * One touch object, as laid out in touch_hcd->object_data.  The report config
 * tells us which bit range belongs to which field; the order of the stores
 * below follows the order of the items in that config.
 */
struct syna_tcm_touch_data {
	u8 status;				/* +0x00 */
	u8 pad0[0x03];				/* +0x01 */
	u32 x;					/* +0x04 */
	u32 y;					/* +0x08 */
	u32 x_width;				/* +0x0c */
	u32 y_width;				/* +0x10 */
	u32 z;					/* +0x14 */
	u32 tx;					/* +0x18 */
	u32 rx;					/* +0x1c */
};

/*
 * Pull one field out of the report buffer.  Fields are packed back to back and
 * may straddle byte boundaries in either direction, so the first chunk is
 * masked to what is left of the current byte and every following chunk is
 * shifted up by the number of bits already collected.
 */
static int touch_get_report_data(unsigned int bit_offset,
		unsigned int bit_count)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	unsigned int byte, bit, taken, count, value;

	if ((bit_count - 1) > 31) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Invalid number of bits\n",
				"touch_get_report_data", 0xd1);
		return -EINVAL;
	}

	if (bit_offset + bit_count >
			tcm_hcd->report_buffer.data_length * 8)
		return -EINVAL;

	byte = bit_offset >> 3;
	bit = bit_offset & 0x07;

	taken = MIN(8 - bit, bit_count);
	value = (tcm_hcd->report_buffer.buf[byte] >> bit) &
			(0xff >> (8 - taken));

	while (taken < bit_count) {
		count = MIN(bit_count - taken, 8);
		value |= (tcm_hcd->report_buffer.buf[++byte] &
				(0xff >> (8 - count))) << (bit_count - taken);
		taken += count;
	}

	return value;
}

/*
 * Walk the report config the firmware handed us and fill object_data and the
 * frame level fields.  Every item that carries data is two bytes: the item
 * number and the width of the field in bits.
 */
static int touch_parse_report(struct syna_tcm_hcd *tcm_hcd)
{
	u8 *config = tcm_hcd->config.buf;
	unsigned int config_length = tcm_hcd->config.data_length;
	unsigned int config_offset = 0;
	unsigned int bit_offset = 0;
	unsigned int object_index = 0;
	unsigned int begin_foreach_offset = 0;
	unsigned int item, count, value;
	struct syna_tcm_touch_data *object;
	int retval;

	if (!config_length)
		return 0;

	while (config_offset < config_length) {
		item = config[config_offset++];

		switch (item) {
		case 0x00:	/* end of the config */
			goto exit;

		case 0x01:	/* filler, carries nothing */
			break;

		case 0x02:	/* start of the per-object block */
			begin_foreach_offset = config_offset;
			break;

		case 0x03:	/* end of the per-object block */
			touch_parse_report_end_of_foreach = config_offset;
			object_index++;
			if (object_index < touch_hcd->max_objects)
				config_offset = begin_foreach_offset;
			break;

		case 0x04:	/* align the next field on a byte boundary */
			bit_offset = (bit_offset + 7) & ~7U;
			break;

		case 0x05:	/* timestamp */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get timestamp\n",
						"touch_parse_report", 0x153);
				goto error;
			}
			touch_hcd->timestamp = value;
			bit_offset += count;
			break;

		case 0x06:	/* object index */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object index\n",
						"touch_parse_report", 0x15e);
				goto error;
			}
			object_index = value;
			bit_offset += count;
			break;

		case 0x07:	/* object classification */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object classification\n",
						"touch_parse_report", 0x168);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->status = (u8)value;
			bit_offset += count;
			break;

		case 0x08:	/* object x position */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object x position\n",
						"touch_parse_report", 0x173);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->x = value;
			bit_offset += count;
			break;

		case 0x09:	/* object y position */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object y position\n",
						"touch_parse_report", 0x17e);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->y = value;
			bit_offset += count;
			break;

		case 0x0a:	/* object z */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object z\n",
						"touch_parse_report", 0x18a);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->z = value;
			bit_offset += count;
			break;

		case 0x0b:	/* object x width */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object x width\n",
						"touch_parse_report", 0x194);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->x_width = value;
			bit_offset += count;
			break;

		case 0x0c:	/* object y width */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object y width\n",
						"touch_parse_report", 0x19e);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->y_width = value;
			bit_offset += count;
			break;

		case 0x0d:	/* object tx position */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object tx position\n",
						"touch_parse_report", 0x1a8);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->tx = value;
			bit_offset += count;
			break;

		case 0x0e:	/* object rx position */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object rx position\n",
						"touch_parse_report", 0x1b2);
				goto error;
			}
			object = (struct syna_tcm_touch_data *)
					touch_hcd->object_data + object_index;
			object->rx = value;
			bit_offset += count;
			break;

		case 0x0f:	/* 0D buttons state */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get 0D buttons state\n",
						"touch_parse_report", 0x1bc);
				goto error;
			}
			touch_hcd->buttons = value;
			bit_offset += count;
			break;

		case 0x10:	/* gesture double tap */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get gesture double tap\n",
						"touch_parse_report", 0x1c6);
				goto error;
			}
			touch_hcd->gesture_double_tap = value;
			bit_offset += count;
			break;

		case 0x11:	/* frame rate */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get frame rate\n",
						"touch_parse_report", 0x1d0);
				goto error;
			}
			touch_hcd->frame_rate = value;
			bit_offset += count;
			break;

		case 0x12:	/* power IM */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get power IM\n",
						"touch_parse_report", 0x1da);
				goto error;
			}
			touch_hcd->power_im = value;
			bit_offset += count;
			break;

		case 0x13:	/* CID IM */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get CID IM\n",
						"touch_parse_report", 0x1e4);
				goto error;
			}
			touch_hcd->cid_im = value;
			bit_offset += count;
			break;

		case 0x14:	/* rail IM */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get rail IM\n",
						"touch_parse_report", 0x1ee);
				goto error;
			}
			touch_hcd->rail_im = value;
			bit_offset += count;
			break;

		case 0x15:	/* CID variance IM */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get CID variance IM\n",
						"touch_parse_report", 0x1f8);
				goto error;
			}
			touch_hcd->cid_variance_im = value;
			bit_offset += count;
			break;

		case 0x16:	/* NSM frequency */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get NSM frequency\n",
						"touch_parse_report", 0x202);
				goto error;
			}
			touch_hcd->nsm_frequency = value;
			bit_offset += count;
			break;

		case 0x17:	/* NSM state */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get NSM state\n",
						"touch_parse_report", 0x20c);
				goto error;
			}
			touch_hcd->nsm_state = value;
			bit_offset += count;
			break;

		case 0x18:	/* number of active objects */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get number of active objects\n",
						"touch_parse_report", 0x216);
				goto error;
			}
			touch_hcd->num_active = value;
			bit_offset += count;
			break;

		case 0x19:	/* CPU cycles used since the last frame */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get num CPU cycles used since last frame\n",
						"touch_parse_report", 0x220);
				goto error;
			}
			touch_hcd->num_cpu_cycles = value;
			bit_offset += count;
			break;

		case 0x1a:	/* face detect */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to detect face\n",
						"touch_parse_report", 0x22a);
				goto error;
			}
			touch_hcd->face_detect = value;
			bit_offset += count;
			break;

		case 0x1b:	/* reserved, carries a width but nothing we use */
			count = config[config_offset++];
			bit_offset += count;
			break;

		case 0x1c:	/* object force */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object force\n",
						"touch_parse_report", 0x1c0);
				goto error;
			}
			touch_hcd->force = value;
			bit_offset += count;
			break;

		case 0x1d:	/* fingerprint area */
			count = config[config_offset++];
			value = touch_get_report_data(bit_offset, count);
			if (value < 0) {
				dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) Failed to get object force\n",
						"touch_parse_report", 0x1cb);
				goto error;
			}
			touch_hcd->fingerprint_area_meet = value;
			dev_notice(&tcm_hcd->pdev->dev,
					"%s info: fingerprint_area_meet = %x\n",
					"touch_parse_report", value);
			bit_offset += count;
			break;

		default:	/* unknown item, skip the field it describes */
			count = config[config_offset++];
			bit_offset += count;
			break;
		}
	}

	if (!touch_hcd->num_active && !touch_parse_report_end_of_foreach) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Invalid report, num_active and end_foreach are 0\n",
				"touch_parse_report", 0x24a);
	}

exit:
	return 0;

error:
	retval = -EINVAL;
	return retval;
}

/*
 * Called by the core for every touch report it reads out of the sensor.
 */
static void touch_report(void)
{
	struct syna_tcm_hcd *tcm_hcd;
	struct syna_tcm_board_data *bdata;
	struct syna_tcm_touch_data *object;
	unsigned int i, status, finger_count = 0;
	int x, y, tmp;

	if (!touch_hcd->input_reporting) {
		dev_err(&touch_hcd->tcm_hcd->pdev->dev,
				"%s error: (line %d) touch didn't init\n",
				"touch_report", 0x293);
		return;
	}

	if (!touch_hcd->input_dev) {
		dev_err(&touch_hcd->tcm_hcd->pdev->dev,
				"%s error: (line %d) touch input dev didn't init\n",
				"touch_report", 0x298);
		return;
	}

	if (touch_hcd->in_suspend) {
		dev_err(&touch_hcd->tcm_hcd->pdev->dev,
				"%s error: (line %d) touch didn't resume\n",
				"touch_report", 0x29e);
		return;
	}

	tcm_hcd = touch_hcd->tcm_hcd;
	bdata = tcm_hcd->hw_if->bdata;

	mutex_lock(&touch_hcd->report_mutex);

	memset(touch_hcd->object_data, 0,
			touch_hcd->max_objects * TOUCH_OBJECT_DATA_SIZE);

	if (touch_parse_report(tcm_hcd) < 0) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to parse touch report\n",
				"touch_report", 0x2a7);
		goto unlock;
	}

	/* The panel is powered down, its idea of the slots is stale. */
	if (tcm_hcd->polling_disabled)
		goto unlock;

	if (!touch_hcd->max_objects)
		goto no_finger;

	object = touch_hcd->object_data;

	for (i = 0; i < touch_hcd->max_objects; i++) {
		status = object->status;

		if (!(touch_hcd->prev_status[i] | status))
			goto next;

		if ((u8)(status - 1) >= 2) {
			if (!status) {
				input_event(touch_hcd->input_dev, EV_ABS,
						ABS_MT_SLOT, i);
				input_mt_report_slot_state(touch_hcd->input_dev,
						MT_TOOL_FINGER, 0);
			}
			goto next;
		}

		x = object->x;
		y = object->y;

		if (bdata->swap_axes) {
			tmp = x;
			x = y;
			y = tmp;
		}

		if (bdata->x_flip)
			x = touch_hcd->prev_max_x - x;

		if (bdata->y_flip)
			y = touch_hcd->prev_max_y - y;

		input_event(touch_hcd->input_dev, EV_ABS, ABS_MT_SLOT, i);
		input_mt_report_slot_state(touch_hcd->input_dev,
				MT_TOOL_FINGER, 1);
		input_event(touch_hcd->input_dev, EV_KEY, BTN_TOUCH, 1);
		input_event(touch_hcd->input_dev, EV_KEY, BTN_TOOL_FINGER, 1);
		input_event(touch_hcd->input_dev, EV_ABS,
				ABS_MT_POSITION_X, x);
		input_event(touch_hcd->input_dev, EV_ABS,
				ABS_MT_POSITION_Y, y);

		dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Finger %d: x = %d, y = %d\n",
				"touch_report", i, x, y);

		finger_count++;

next:
		touch_hcd->prev_status[i] = object->status;
		object = (struct syna_tcm_touch_data *)
				((u8 *)object + TOUCH_OBJECT_DATA_SIZE);
	}

	if (finger_count)
		goto sync;

no_finger:
	input_event(touch_hcd->input_dev, EV_KEY, BTN_TOUCH, 0);
	input_event(touch_hcd->input_dev, EV_KEY, BTN_TOOL_FINGER, 0);

sync:
	input_event(touch_hcd->input_dev, EV_SYN, SYN_REPORT, 0);

unlock:
	mutex_unlock(&touch_hcd->report_mutex);
}
