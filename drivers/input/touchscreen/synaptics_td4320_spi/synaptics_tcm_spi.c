// SPDX-License-Identifier: GPL-2.0
/*
 * SPI transport for the TD4320 touch controller.
 *
 * Reconstructed from synaptics_td4320_spi_ts.ko; the vendor keeps the shared
 * transfer buffers in three module globals that are resized on demand.
 */

#include "synaptics_tcm_core.h"

#define XFER_BUF_SIZE		0x100

static struct spi_device *syna_tcm_spi_device;
static struct platform_device *syna_tcm_spi_pdev;

static unsigned char *buf;
static unsigned int buf_size;
static struct spi_transfer *xfer;
static unsigned int xfer_count;

static int syna_tcm_spi_probe(struct spi_device *spi);
static static int syna_tcm_spi_remove(struct spi_device *spi);

static const struct of_device_id syna_tcm_of_match_table[] = {
	{ .compatible = "synaptics,tcm-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, syna_tcm_of_match_table);

static struct spi_driver syna_tcm_spi_driver = {
	.driver = {
		.name = "synaptics_tcm_spi",
		.owner = THIS_MODULE,
		.of_match_table = syna_tcm_of_match_table,
	},
	.probe = syna_tcm_spi_probe,
	.remove = syna_tcm_spi_remove,
};

/*
 * The generic attribute of every sysfs node the driver exports; reading one
 * of them is always a bug in the caller.
 */
int syna_tcm_show_error(void)
{
	LOGE("%s: Attribute not readable\n", __func__);

	return -1;
}

int syna_tcm_bus_init(void)
{
	return __spi_register_driver(THIS_MODULE, &syna_tcm_spi_driver);
}

static int syna_tcm_spi_remove(struct spi_device *spi)
{
	syna_tcm_spi_device->dev.platform_data = NULL;

	platform_device_unregister(syna_tcm_spi_device);

	return 0;
}

void syna_tcm_bus_exit(void)
{
	kfree(buf);
	kfree(xfer);

	driver_unregister(&syna_tcm_spi_driver.driver);
}

/*
 * Make sure the shared transfer array and payload buffer are large enough.
 * The buffers are only ever grown, and a failed reallocation leaves both the
 * pointer and the size consistent (0), so the next call tries again.
 */
int syna_tcm_spi_alloc_mem(struct syna_tcm_hcd *tcm_hcd, unsigned int count,
			   unsigned int size)
{
	if (count > xfer_count) {
		kfree(xfer);

		xfer = kmalloc(count * sizeof(struct spi_transfer),
			       GFP_KERNEL);
		if (!xfer) {
			LOGE("%s error: (line %d) Failed to allocate memory for xfer\n",
			     __func__, 0x11f);
			xfer_count = 0;
			return -ENOMEM;
		}

		xfer_count = count;
	} else {
		memset(xfer, 0, count * sizeof(struct spi_transfer));
	}

	if (size > buf_size) {
		if (buf_size)
			kfree(buf);

		buf = kmalloc(size, GFP_KERNEL);
		if (!buf) {
			LOGE("%s error: (line %d) Failed to allocate memory for buf\n",
			     __func__, 0x12e);
			buf_size = 0;
			return -ENOMEM;
		}

		buf_size = size;
	}

	return 0;
}

/*
 * Clock @size bytes in.  With a per byte delay configured every byte gets its
 * own transfer and its own dummy byte on MOSI, otherwise one transfer carries
 * the whole payload.  The block delay, when set, goes on the last transfer.
 */
int syna_tcm_spi_read(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		      unsigned int size)
{
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct spi_device *spi = (struct spi_device *)tcm_hcd->pdev->dev.parent;
	struct spi_message msg;
	unsigned int i;
	int retval;

	mutex_lock(&tcm_hcd->io_ctrl_mutex);

	spi_message_init(&msg);

	if (bdata->byte_delay_us)
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, size, 1);
	else
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, size);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory\n",
		     __func__, 0x1da);
		goto exit;
	}

	if (bdata->byte_delay_us) {
		buf[0] = 0xff;

		for (i = 0; i < size; i++) {
			xfer[i].tx_buf = buf;
			xfer[i].rx_buf = data + i;
			xfer[i].len = 1;
			xfer[i].delay_usecs = bdata->byte_delay_us;

			spi_message_add_tail(&xfer[i], &msg);
		}

		if (bdata->block_delay_us)
			xfer[size - 1].delay_usecs = bdata->block_delay_us;
	} else {
		memset(buf, 0xff, size);

		xfer[0].tx_buf = buf;
		xfer[0].rx_buf = data;
		xfer[0].len = size;
		if (bdata->block_delay_us)
			xfer[0].delay_usecs = bdata->block_delay_us;

		spi_message_add_tail(&xfer[0], &msg);
	}

	retval = spi_sync(spi, &msg);
	if (retval) {
		LOGE("%s error: (line %d) Failed to complete SPI transfer, error = %d\n",
		     __func__, 0x1f9, retval);
		goto exit;
	}

exit:
	mutex_unlock(&tcm_hcd->io_ctrl_mutex);

	return retval;
}

/* the write side mirrors the read side, but without a dummy byte */
int syna_tcm_spi_write(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		       unsigned int size)
{
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct spi_device *spi = (struct spi_device *)tcm_hcd->pdev->dev.parent;
	struct spi_message msg;
	unsigned int i;
	int retval;

	mutex_lock(&tcm_hcd->io_ctrl_mutex);

	spi_message_init(&msg);

	if (bdata->byte_delay_us)
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, size, 0);
	else
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, 0);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory\n",
		     __func__, 0x215);
		goto exit;
	}

	if (bdata->byte_delay_us) {
		for (i = 0; i < size; i++) {
			xfer[i].tx_buf = data + i;
			xfer[i].len = 1;
			xfer[i].delay_usecs = bdata->byte_delay_us;

			spi_message_add_tail(&xfer[i], &msg);
		}

		if (bdata->block_delay_us)
			xfer[size - 1].delay_usecs = bdata->block_delay_us;
	} else {
		xfer[0].tx_buf = data;
		xfer[0].len = size;
		if (bdata->block_delay_us)
			xfer[0].delay_usecs = bdata->block_delay_us;

		spi_message_add_tail(&xfer[0], &msg);
	}

	retval = spi_sync(spi, &msg);
	if (retval) {
		LOGE("%s error: (line %d) Failed to complete SPI transfer, error = %d\n",
		     __func__, 0x230, retval);
		goto exit;
	}

exit:
	mutex_unlock(&tcm_hcd->io_ctrl_mutex);

	return retval;
}

/*
 * RMI over SPI: the address is sent as two bytes, the high one carrying the
 * read flag, followed by the payload as a burst (or byte by byte when a per
 * byte delay is configured).  The bus is switched to mode 3 for the transfer.
 */
int syna_tcm_spi_rmi_read(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			  unsigned char *data, unsigned int size)
{
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct spi_device *spi = (struct spi_device *)tcm_hcd->pdev->dev.parent;
	struct spi_message msg;
	unsigned int mode;
	unsigned int i;
	int retval;

	mutex_lock(&tcm_hcd->io_ctrl_mutex);

	spi_message_init(&msg);

	if (bdata->ubl_byte_delay_us)
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, size + 2, 3);
	else
		retval = syna_tcm_spi_alloc_mem(tcm_hcd, 2, size + 2);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory\n",
		     __func__, 0x14f);
		goto exit;
	}

	buf[0] = (addr >> 8) | 0x80;
	buf[1] = addr & 0xff;

	if (bdata->ubl_byte_delay_us) {
		buf[2] = 0xff;

		xfer[0].tx_buf = buf;
		xfer[0].len = 1;
		xfer[0].speed_hz = bdata->ubl_max_freq;
		if (bdata->block_delay_us)
			xfer[size + 1].delay_usecs = bdata->block_delay_us;
		spi_message_add_tail(&xfer[0], &msg);

		xfer[1].tx_buf = buf + 1;
		xfer[1].len = 1;
		xfer[1].delay_usecs = bdata->ubl_byte_delay_us;
		xfer[1].speed_hz = bdata->ubl_max_freq;
		if (!size && bdata->block_delay_us)
			xfer[size + 1].delay_usecs = bdata->block_delay_us;
		spi_message_add_tail(&xfer[1], &msg);

		for (i = 0; i < size; i++) {
			xfer[i + 2].tx_buf = buf + 2;
			xfer[i + 2].rx_buf = data + i;
			xfer[i + 2].len = 1;
			xfer[i + 2].delay_usecs = bdata->ubl_byte_delay_us;
			if (i == size - 1 && bdata->block_delay_us)
				xfer[i + 2].delay_usecs =
					bdata->block_delay_us;
			xfer[i + 2].speed_hz = bdata->ubl_max_freq;

			spi_message_add_tail(&xfer[i + 2], &msg);
		}
	} else {
		xfer[0].tx_buf = buf;
		xfer[0].len = 2;
		xfer[0].speed_hz = bdata->ubl_max_freq;
		spi_message_add_tail(&xfer[0], &msg);

		memset(buf + 2, 0xff, size);

		xfer[1].tx_buf = buf + 2;
		xfer[1].rx_buf = data;
		xfer[1].len = size;
		if (bdata->block_delay_us)
			xfer[1].delay_usecs = bdata->block_delay_us;
		xfer[1].speed_hz = bdata->ubl_max_freq;
		spi_message_add_tail(&xfer[1], &msg);
	}

	mode = spi->mode;
	spi->mode = SPI_MODE_3;

	retval = spi_sync(spi, &msg);

	spi->mode = mode;

	if (retval) {
		LOGE("%s error: (line %d) Failed to complete SPI transfer, error = %d\n",
		     __func__, 0x17e, retval);
		goto exit;
	}

exit:
	mutex_unlock(&tcm_hcd->io_ctrl_mutex);

	return retval;
}

/*
 * The write side sends both address bytes and the payload as one burst, so the
 * scratch buffer has to hold the address in front of a copy of the data.
 */
int syna_tcm_spi_rmi_write(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			   unsigned char *data, unsigned int size)
{
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct spi_device *spi = (struct spi_device *)tcm_hcd->pdev->dev.parent;
	struct spi_message msg;
	unsigned int mode;
	int retval;

	mutex_lock(&tcm_hcd->io_ctrl_mutex);

	spi_message_init(&msg);

	retval = syna_tcm_spi_alloc_mem(tcm_hcd, 1, size + 2);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory\n",
		     __func__, 0x19c);
		goto exit;
	}

	buf[0] = (addr >> 8) & 0x7f;
	buf[1] = addr & 0xff;

	if (!data || !(buf + 2) || buf_size - 2 < size) {
		LOGE("%s error: (line %d) Failed to copy write data\n",
		     __func__, 0x1a9);
		retval = -EINVAL;
		goto exit;
	}

	memcpy(buf + 2, data, size);

	xfer[0].tx_buf = buf;
	xfer[0].len = size + 2;
	if (bdata->block_delay_us)
		xfer[0].delay_usecs = bdata->block_delay_us;
	spi_message_add_tail(&xfer[0], &msg);

	mode = spi->mode;
	spi->mode = SPI_MODE_3;

	retval = spi_sync(spi, &msg);

	spi->mode = mode;

	if (retval) {
		LOGE("%s error: (line %d) Failed to complete SPI transfer, error = %d\n",
		     __func__, 0x1bc, retval);
		goto exit;
	}

exit:
	mutex_unlock(&tcm_hcd->io_ctrl_mutex);

	return retval;
}

/* the shared transport description handed to the core driver */
static struct syna_tcm_bus_io bus_io;
static struct syna_tcm_hw_interface hw_if;

/*
 * Every property is mandatory here, the DT of this platform always carries
 * the full set; the plain flags and the gpios are read without diagnostics.
 */
int syna_tcm_spi_parse_dt(struct device *dev,
			  struct syna_tcm_board_data *bdata,
			  struct device_node *np)
{
	u32 value;
	int retval;

	bdata->x_flip = of_property_read_bool(np, "synaptics,x-flip");
	bdata->y_flip = of_property_read_bool(np, "synaptics,y-flip");
	bdata->swap_axes = of_property_read_bool(np, "synaptics,swap-axes");

	bdata->irq_gpio = of_get_named_gpio_flags(np, "synaptics,irq-gpio", 0,
						  &bdata->irq_gpio_flags);
	bdata->power_gpio = of_get_named_gpio(np, "synaptics,power-gpio", 0);
	bdata->reset_gpio = of_get_named_gpio(np, "synaptics,reset-gpio", 0);
	bdata->tpio_reset_gpio = of_get_named_gpio(np,
						   "synaptics,tpio-reset-gpio",
						   0);

	retval = of_property_read_u32(np, "synaptics,irq-on-state", &value);
	if (retval >= 0)
		bdata->irq_on_state = value;

	retval = of_property_read_u32(np, "synaptics,power-on-state", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read synaptics,power-on-state property\n",
		     __func__, 0x67);
		return retval;
	}
	bdata->power_on_state = value;

	retval = of_property_read_u32(np, "synaptics,power-delay-ms", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read synaptics,power-delay-ms property\n",
		     __func__, 0x76);
		return retval;
	}
	bdata->power_delay_ms = value;

	retval = of_property_read_u32(np, "synaptics,reset-on-state", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read synaptics,reset-on-state property\n",
		     __func__, 0x8d);
		return retval;
	}
	bdata->reset_on_state = value;

	retval = of_property_read_u32(np, "synaptics,reset-active-ms", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read synaptics,reset-active-ms property\n",
		     __func__, 0x9c);
		return retval;
	}
	bdata->reset_active_ms = value;

	retval = of_property_read_u32(np, "synaptics,reset-delay-ms", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,reset-delay-ms property\n",
		     __func__, 0xab);
		return retval;
	}
	bdata->reset_delay_ms = value;

	retval = of_property_read_u32(np, "synaptics,byte-delay-us", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,byte-delay-us property\n",
		     __func__, 0xcb);
		return retval;
	}
	bdata->byte_delay_us = value;

	retval = of_property_read_u32(np, "synaptics,block-delay-us", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,block-delay-us property\n",
		     __func__, 0xda);
		return retval;
	}
	bdata->block_delay_us = value;

	retval = of_property_read_u32(np, "synaptics,spi-mode", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,spi-mode property\n",
		     __func__, 0xe9);
		return retval;
	}
	bdata->spi_mode = value;

	retval = of_property_read_u32(np, "synaptics,ubl-max-freq", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,ubl-max-freq property\n",
		     __func__, 0xf8);
		return retval;
	}
	bdata->ubl_max_freq = value;

	retval = of_property_read_u32(np, "synaptics,ubl-byte-delay-us", &value);
	if (retval < 0) {
		LOGE("%s error: (line %d) Unable to read synaptics,ubl-byte-delay-us property\n",
		     __func__, 0x107);
		return retval;
	}
	bdata->ubl_byte_delay_us = value;

	of_property_read_string(np, "synaptics,pwr-reg-name",
				&bdata->pwr_reg_name);
	of_property_read_string(np, "synaptics,bus-reg-name",
				&bdata->bus_reg_name);

	return 0;
}

/*
 * Bring up the SPI side and hand the hardware interface over to the core
 * platform device; everything the core needs lives in the bus description.
 */
static int syna_tcm_spi_probe(struct spi_device *spi)
{
	struct syna_tcm_board_data *bdata;
	int retval;

	LOGE(">>>>>>>>>>>%s:probe start<<<<<<<<<<<", __func__);

	if (spi->controller->flags & SPI_CONTROLLER_HALF_DUPLEX) {
		LOGE("%s error: (line %d) Full duplex not supported by host\n",
		     __func__, 0x240);
		return -EIO;
	}

	syna_tcm_spi_device = platform_device_alloc("synaptics_tcm", 0);
	if (!syna_tcm_spi_device) {
		LOGE("%s error: (line %d) Failed to allocate platform device\n",
		     __func__, 0x247);
		return -ENOMEM;
	}

	bdata = devm_kzalloc(&spi->dev, sizeof(*bdata), GFP_KERNEL);
	if (!bdata) {
		LOGE("%s error: (line %d) Failed to allocate memory for board data\n",
		     __func__, 0x24f);
		platform_device_put(syna_tcm_spi_device);
		return -ENOMEM;
	}

	hw_if.bdata = bdata;

	retval = syna_tcm_spi_parse_dt(&spi->dev, bdata, spi->dev.of_node);
	if (retval < 0) {
		platform_device_put(syna_tcm_spi_device);
		return retval;
	}

	if (bdata->spi_mode <= 3)
		spi->mode = bdata->spi_mode;

	spi->bits_per_word = 8;

	retval = spi_setup(spi);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to set up SPI protocol driver\n",
		     __func__, 0x273);
		platform_device_put(syna_tcm_spi_device);
		return retval;
	}

	bus_io.type = 0x1c;
	bus_io.rmi_read = syna_tcm_spi_rmi_read;
	bus_io.rmi_write = syna_tcm_spi_rmi_write;
	bus_io.read = syna_tcm_spi_read;
	bus_io.write = syna_tcm_spi_write;

	hw_if.io = &bus_io;

	syna_tcm_spi_device->dev.platform_data = &hw_if;

	retval = platform_device_add(syna_tcm_spi_device);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to add platform device\n",
		     __func__, 0x27d);
		platform_device_put(syna_tcm_spi_device);
		return retval;
	}

	LOGE(">>>>>>>>>>>%s:probe end<<<<<<<<<<<", __func__);

	return 0;
}
