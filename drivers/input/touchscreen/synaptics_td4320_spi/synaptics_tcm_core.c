// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm driver core.
 *
 * The core owns the command/response protocol spoken over the transport
 * selected by the bus layer.  A message is a header (command, length low,
 * length high) followed by the payload, optionally split into chunks that
 * the controller can digest, and the answer arrives asynchronously in the
 * interrupt thread.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "synaptics_tcm_core.h"

/* signalled by the interrupt thread once a command response is complete */
static struct completion response_complete;

/*
 * Copy that refuses to overrun the destination; the vendors builds keep the
 * diagnostic here instead of at every call site.
 */
static int secure_memcpy(unsigned char *dest, unsigned int dest_size,
			 const unsigned char *src, unsigned int src_size)
{
	if (dest_size < src_size) {
		pr_err("%s: src_size = %d, dest_size = %d, count = %d\n",
		       __func__, src_size, dest_size, src_size);
		return -EINVAL;
	}

	memcpy(dest, src, src_size);

	return 0;
}

/*
 * Make sure a scratch buffer can hold size bytes, reusing whatever was
 * allocated before.  The caller holds the buffer mutex.
 */
static int syna_tcm_alloc_mem(struct syna_tcm_hcd *tcm_hcd,
			      struct syna_tcm_buf *buf, unsigned int size)
{
	if (buf->buf_size >= size) {
		memset(buf->buf, 0x00, buf->buf_size);
		buf->data_length = 0;
		return 0;
	}

	kfree(buf->buf);

	buf->buf = kmalloc(size, GFP_KERNEL);
	if (!buf->buf) {
		LOGE("%s: Failed to allocate memory\n", __func__);
		LOGE("%s: Allocation size = %d\n", __func__, size);
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;

	memset(buf->buf, 0x00, size);

	buf->data_length = 0;

	return 0;
}

/*
 * Single fragment raw write: one command byte in front of the data, sent as
 * one transfer.  Used for the streaming commands that do not answer.
 */
static int syna_tcm_raw_write(struct syna_tcm_hcd *tcm_hcd,
			      unsigned char command, unsigned char *data,
			      unsigned int size)
{
	int retval;

	retval = syna_tcm_alloc_mem(tcm_hcd, &tcm_hcd->out, size + 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->out.buf\n",
		     __func__, 0x58a);
		return retval;
	}

	tcm_hcd->out.buf[0] = command;

	if (size) {
		if (!data || !(tcm_hcd->out.buf + 1)) {
			LOGE("%s error: (line %d) Failed to copy data\n",
			     __func__, 0x59c);
			return -EINVAL;
		}

		retval = secure_memcpy(tcm_hcd->out.buf + 1,
				       tcm_hcd->out.buf_size - 1, data, size);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy data\n",
			     __func__, 0x59c);
			return retval;
		}
	}

	retval = tcm_hcd->hw_if->io->write(tcm_hcd, tcm_hcd->out.buf,
					   size + 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write to device\n",
		     __func__, 0x5a7);
		return retval;
	}

	return 0;
}

/**
 * syna_tcm_write_message() - issue a command and optionally collect the reply.
 * @tcm_hcd: the driver context.
 * @command: command code to send.
 * @payload: bytes following the header, may be NULL when @length is zero.
 * @length: payload length.
 * @resp_buf: when non NULL, the caller wants the response and the pointer is
 *            updated to the internal response buffer on success.
 * @resp_length: size of the buffer handed back through @resp_buf.
 * @response_code: first byte of the response, the command specific code.
 * @status: optional in/out status byte, set to 0xff when no status is known.
 * @delay_ms: when non zero the polling work is rearmed before waiting.
 *
 * A command with a @resp_buf is answered through the asynchronous response
 * machinery: the interrupt thread fills tcm_hcd->resp and completes
 * response_complete.  Without @resp_buf the message is streamed out in
 * fragments and nothing is waited for.
 *
 * Return: zero on success, a negative error code otherwise.
 */
int syna_tcm_write_message(struct syna_tcm_hcd *tcm_hcd, unsigned char command,
			   unsigned char *payload, unsigned int length,
			   unsigned char **resp_buf, unsigned int *resp_length,
			   unsigned char *response_code, unsigned char *status,
			   unsigned int delay_ms)
{
	unsigned int chunk_size, count, chunk, offset, remain;
	unsigned int i;
	int retval = 0;

	if (status)
		*status = 0xff;

	/*
	 * Commands are only issued from process context; the only exception is
	 * the suspended state, where the wakeup path is allowed to talk to the
	 * controller directly.
	 */
	if (!tcm_hcd->in_suspend &&
	    tcm_hcd->context_pid == (u32)task_pid_nr(current)) {
		LOGE("%s error: (line %d) Invalid execution context\n",
		     __func__, 0x688);
		return -EINVAL;
	}

	mutex_lock(&tcm_hcd->command_mutex);
	mutex_lock(&tcm_hcd->rw_ctrl_mutex);

	if (resp_buf) {
		if (delay_ms && tcm_hcd->in_suspend) {
			cancel_delayed_work_sync(&tcm_hcd->polling_work);
			flush_workqueue(tcm_hcd->helper_wq);
		}

		tcm_hcd->response_ready = 0;
		reinit_completion(&response_complete);
		tcm_hcd->command = command;

		mutex_lock(&tcm_hcd->resp.buf_mutex);
		tcm_hcd->resp.buf = *resp_buf;
		tcm_hcd->resp.buf_size = *resp_length;
		tcm_hcd->resp.data_length = 0;
		mutex_unlock(&tcm_hcd->resp.buf_mutex);

		chunk_size = tcm_hcd->read_message_count ?
			     tcm_hcd->read_message_count - 1 : length + 2;
		if (command == 0x45)
			chunk_size = 0xf0;

		count = (length + 1 + chunk_size) / chunk_size;
		if (!count)
			count = 1;

		dev_dbg(&tcm_hcd->pdev->dev, "%s info: Command = 0x%02x\n",
			__func__, command);

		mutex_lock(&tcm_hcd->out.buf_mutex);

		remain = length + 2;
		offset = (unsigned int)-2;

		for (i = 0; i < count; i++) {
			chunk = min(remain, chunk_size);

			retval = syna_tcm_alloc_mem(tcm_hcd, &tcm_hcd->out,
						    chunk + 1);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->out.buf\n",
				     __func__, 0x6d6);
				retval = -ENOMEM;
				goto unlock;
			}

			if (i) {
				tcm_hcd->out.buf[0] = 1;
				retval = secure_memcpy(tcm_hcd->out.buf + 1,
						       tcm_hcd->out.buf_size - 1,
						       payload + offset, chunk);
				if (retval < 0) {
					LOGE("%s error: (line %d) Failed to copy payload\n",
					     __func__, 0x6f9);
					goto unlock;
				}
			} else {
				tcm_hcd->out.buf[0] = command;
				tcm_hcd->out.buf[1] = length & 0xff;
				tcm_hcd->out.buf[2] = (length >> 8) & 0xff;

				if (chunk >= 3) {
					if (!payload ||
					    !(tcm_hcd->out.buf + 3)) {
						LOGE("%s error: (line %d) Failed to copy payload\n",
						     __func__, 0x6e9);
						retval = -EINVAL;
						goto unlock;
					}

					retval = secure_memcpy(
						tcm_hcd->out.buf + 3,
						tcm_hcd->out.buf_size - 3,
						payload, chunk - 2);
					if (retval < 0) {
						LOGE("%s error: (line %d) Failed to copy payload\n",
						     __func__, 0x6e9);
						goto unlock;
					}
				}
			}

			retval = tcm_hcd->hw_if->io->write(tcm_hcd,
							   tcm_hcd->out.buf,
							   chunk + 1);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to write to device\n",
				     __func__, 0x705);
				goto unlock;
			}

			remain -= chunk;
			offset += chunk_size;

			if (count > 1)
				usleep_range(500, 1000);
		}

		mutex_unlock(&tcm_hcd->out.buf_mutex);

		if (delay_ms && tcm_hcd->in_suspend)
			queue_delayed_work_on(WORK_CPU_UNBOUND,
					      tcm_hcd->helper_wq,
					      &tcm_hcd->polling_work,
					      msecs_to_jiffies(delay_ms));

		if (!wait_for_completion_timeout(&response_complete, 750)) {
			LOGE("%s error: (line %d) Timed out waiting for response (command 0x%02x)\n",
			     __func__, 0x723, tcm_hcd->command);
			retval = -ETIME;
			goto done;
		}

		if (tcm_hcd->response_ready) {
			LOGE("%s error: (line %d) Failed to get valid response (command 0x%02x)\n",
			     __func__, 0x72c, tcm_hcd->command);
			retval = -EIO;
			goto done;
		}

		mutex_lock(&tcm_hcd->resp.buf_mutex);

		if (tcm_hcd->status_code != 1) {
			if (tcm_hcd->resp.data_length) {
				LOGE("%s error: (line %d) Error code = 0x%02x (command 0x%02x)\n",
				     __func__, 0x737, tcm_hcd->resp.buf[0],
				     tcm_hcd->command);
			}
			retval = -EIO;
		} else {
			retval = 0;
		}

		*resp_buf = tcm_hcd->resp.buf;
		*resp_length = tcm_hcd->resp.buf_size;
		*response_code = tcm_hcd->resp.data_length;

		if (status)
			*status = tcm_hcd->status_code;

		mutex_unlock(&tcm_hcd->resp.buf_mutex);
	} else {
		chunk_size = tcm_hcd->read_message_count ?
			     tcm_hcd->read_message_count - 1 : length;

		count = (length - 1 + chunk_size) / chunk_size;
		if (!count)
			count = 1;

		mutex_lock(&tcm_hcd->out.buf_mutex);

		offset = 0;

		for (i = 0; i < count; i++) {
			chunk = min(length, chunk_size);

			retval = syna_tcm_raw_write(tcm_hcd, i ? 1 : command,
						    payload + offset, chunk);
			if (retval < 0)
				goto unlock;

			length -= chunk;
			offset += chunk_size;
		}

		mutex_unlock(&tcm_hcd->out.buf_mutex);
	}

	retval = 0;
	goto done;

unlock:
	if (resp_buf)
		mutex_unlock(&tcm_hcd->out.buf_mutex);

done:
	tcm_hcd->command = 0;
	tcm_hcd->response_ready = 0;

	mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
	mutex_unlock(&tcm_hcd->command_mutex);

	return retval;
}

/*
 * Every functional module registers itself in this pool; reports that are not
 * handled by the core are broadcast to the registered handlers.
 */
struct syna_tcm_module {
	u8 init_pending;				/* +0x00 */
	u8 remove_pending;				/* +0x01 */
	u8 pad0[0x06];
	struct list_head node;				/* +0x08 */
	struct syna_tcm_module_desc *desc;		/* +0x18 */
};

static struct {
	u8 init;					/* +0x00 */
	u8 notify;					/* +0x01 */
	u8 pad0[0x06];
	struct mutex mutex;				/* +0x08 */
	u8 pad1[0x18];
	struct list_head list;				/* +0x28 */
	struct work_struct work;			/* +0x38 */
	struct workqueue_struct *workqueue;		/* +0x68 */
	struct syna_tcm_hcd *hcd;			/* +0x70 */
} mod_pool;

/*
 * Grow or shrink a buffer so that it holds exactly size bytes.  Unlike
 * syna_tcm_alloc_mem() this never keeps a larger allocation around, which is
 * what the response and report paths want.
 */
static int syna_tcm_realloc_mem(struct syna_tcm_hcd *tcm_hcd,
				struct syna_tcm_buf *buf, unsigned int size)
{
	if (buf->buf) {
		kfree(buf->buf);
		buf->buf = NULL;
		buf->buf_size = 0;
	}

	if (!size)
		return 0;

	buf->buf = kmalloc(size, GFP_KERNEL);
	if (!buf->buf) {
		LOGE("%s: Failed to allocate memory\n", __func__);
		LOGE("%s: Failed to copy data\n", __func__);
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;

	memset(buf->buf, 0x00, size);

	return 0;
}

/**
 * syna_tcm_raw_read() - read length bytes into a caller supplied buffer.
 * @tcm_hcd: the driver context.
 * @buf: destination, the header is stripped from the first fragment.
 * @length: number of bytes the caller expects.
 *
 * Multi fragment replies carry a three byte header on every fragment; the
 * first one is copied verbatim, the following ones contribute only their
 * payload.  A single padding byte is returned as MESSAGE_PADDING.
 *
 * Return: zero on success, a negative error code otherwise.
 */
static int syna_tcm_raw_read(struct syna_tcm_hcd *tcm_hcd, unsigned char *buf,
			     unsigned int length)
{
	unsigned int chunk_size, count, chunk, copied, remain;
	unsigned int offset = 0, i = 0, k;
	int retval = 0;

	if (length <= 1) {
		LOGE("%s error: (line %d) Invalid length information\n",
		     __func__, 0x4f9);
		return -EINVAL;
	}

	chunk_size = tcm_hcd->max_response_size ?
		     tcm_hcd->max_response_size - 2 : length - 2;

	count = (length - 3 + chunk_size) / chunk_size;

	mutex_lock(&tcm_hcd->temp.buf_mutex);

	remain = length;

	for (k = 0; k < count; k++) {
		chunk = (remain > chunk_size) ? chunk_size : remain;

		if (chunk == 1) {
			buf[i++] = MESSAGE_PADDING;
			remain--;
			continue;
		}

		retval = syna_tcm_alloc_mem(tcm_hcd, &tcm_hcd->temp,
					    chunk + 2);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->temp.buf\n",
			     __func__, 0x521);
			goto exit;
		}

		retval = tcm_hcd->hw_if->io->read(tcm_hcd, tcm_hcd->temp.buf,
						  chunk + 2);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read from device\n",
			     __func__, 0x52b);
			goto exit;
		}

		if (k) {
			if (tcm_hcd->temp.buf[1] != 3) {
				LOGE("%s error: (line %d) Incorrect header code (0x%02x)\n",
				     __func__, 0x53c, tcm_hcd->temp.buf[1]);
				retval = -EIO;
				goto exit;
			}

			mutex_unlock(&tcm_hcd->temp.buf_mutex);

			retval = secure_memcpy(buf + i, length - i,
					       tcm_hcd->temp.buf + 2, chunk);
			mutex_lock(&tcm_hcd->temp.buf_mutex);

			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to copy data\n",
				     __func__, 0x549);
				goto exit;
			}

			copied = chunk;
		} else {
			mutex_unlock(&tcm_hcd->temp.buf_mutex);

			retval = secure_memcpy(buf, length, tcm_hcd->temp.buf,
					       chunk + 2);
			mutex_lock(&tcm_hcd->temp.buf_mutex);

			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to copy data\n",
				     __func__, 0x549);
				goto exit;
			}

			copied = chunk + 2;
		}

		i += copied;
		remain -= chunk;
		offset += chunk;
	}

exit:
	mutex_unlock(&tcm_hcd->temp.buf_mutex);

	return retval;
}

/*
 * The first fragment of a message only carried as much as fits into in.buf,
 * the rest has to be fetched with continued reads.
 */
static int syna_tcm_continued_read(struct syna_tcm_hcd *tcm_hcd)
{
	unsigned char *in_buf;
	unsigned int remaining, offset, chunk, chunk_size;
	int retval = 0;

	in_buf = tcm_hcd->in.buf;

	/* the header of the very first fragment lives in front of the payload */
	offset = tcm_hcd->in.buf_size;

	remaining = tcm_hcd->payload_length + 5 - offset;

	chunk_size = tcm_hcd->command_count;

	if (chunk_size < 2)
		chunk_size = 2;

	chunk_size -= 2;

	mutex_lock(&tcm_hcd->temp.buf_mutex);

	while (remaining) {
		chunk = (remaining > chunk_size) ? chunk_size : remaining;

		if (chunk == 1) {
			in_buf[offset++] = MESSAGE_PADDING;
			remaining--;
			continue;
		}

		retval = syna_tcm_alloc_mem(tcm_hcd, &tcm_hcd->temp,
					    chunk + 2);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->temp.buf\n",
			     __func__, 0x4a3);
			goto exit;
		}

		retval = tcm_hcd->hw_if->io->read(tcm_hcd, tcm_hcd->temp.buf,
						  chunk + 2);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read from device\n",
			     __func__, 0x4ae);
			goto exit;
		}

		if (tcm_hcd->temp.buf[0] != MESSAGE_MARKER) {
			LOGE("%s error: (line %d) Incorrect header marker (0x%02x)\n",
			     __func__, 0x4ba, tcm_hcd->temp.buf[0]);
			retval = -EIO;
			goto exit;
		}

		if (tcm_hcd->temp.buf[1] != 3) {
			LOGE("%s error: (line %d) Incorrect header code (0x%02x)\n",
			     __func__, 0x4c3, tcm_hcd->temp.buf[1]);
			retval = -EIO;
			goto exit;
		}

		mutex_unlock(&tcm_hcd->temp.buf_mutex);

		retval = secure_memcpy(in_buf + offset,
				       tcm_hcd->in.buf_size - offset,
				       tcm_hcd->temp.buf + 2, chunk);
		mutex_lock(&tcm_hcd->temp.buf_mutex);

		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy payload\n",
			     __func__, 0x4d0);
			goto exit;
		}

		offset += chunk;
		remaining -= chunk;
	}

exit:
	mutex_unlock(&tcm_hcd->temp.buf_mutex);

	return retval;
}

/*
 * A response arrived for the command that is currently waiting in
 * syna_tcm_write_message().
 */
static int syna_tcm_dispatch_response(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;

	if (tcm_hcd->response_ready != 1)
		return 0;

	tcm_hcd->status_code = tcm_hcd->report_code;

	if (tcm_hcd->payload_length) {
		mutex_lock(&tcm_hcd->resp.buf_mutex);

		retval = syna_tcm_realloc_mem(tcm_hcd, &tcm_hcd->resp,
					      tcm_hcd->payload_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->resp.buf\n",
			     __func__, 0x3c7);
			mutex_unlock(&tcm_hcd->resp.buf_mutex);
			goto exit;
		}

		mutex_lock(&tcm_hcd->in.buf_mutex);

		retval = secure_memcpy(tcm_hcd->resp.buf,
				       tcm_hcd->resp.buf_size,
				       tcm_hcd->in.buf + 4,
				       tcm_hcd->payload_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy payload\n",
			     __func__, 0x3d6);
			mutex_unlock(&tcm_hcd->in.buf_mutex);
			mutex_unlock(&tcm_hcd->resp.buf_mutex);
			goto exit;
		}

		tcm_hcd->resp.data_length = tcm_hcd->payload_length;

		mutex_unlock(&tcm_hcd->in.buf_mutex);
		mutex_unlock(&tcm_hcd->resp.buf_mutex);
	} else {
		retval = 0;
	}

exit:
	tcm_hcd->response_ready = retval;

	complete(&response_complete);

	return 0;
}

/*
 * Broadcast a report to the modules that registered a handler; a report whose
 * code nobody knows about simply falls through.
 */
static int syna_tcm_dispatch_report(struct syna_tcm_hcd *tcm_hcd)
{
	struct syna_tcm_module *mod;
	int retval = 0;

	if (tcm_hcd->report_code == 0x84) {
		unsigned char fw_log[0xff];

		mutex_lock(&tcm_hcd->in.buf_mutex);

		secure_memcpy(fw_log, sizeof(fw_log), tcm_hcd->in.buf + 4,
			      min(tcm_hcd->payload_length,
				  (unsigned int)sizeof(fw_log) - 1));

		mutex_unlock(&tcm_hcd->in.buf_mutex);

		fw_log[sizeof(fw_log) - 1] = 0;

		LOGE("%s error: (line %d) TouchFWLog: %s\n",
		     __func__, 0x380, fw_log);

		return 0;
	}

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry(mod, &mod_pool.list, node) {
		if (!mod->init_pending && !mod->remove_pending)
			continue;

		if (mod->desc && mod->desc->handler)
			retval = mod->desc->handler(tcm_hcd);
	}

	tcm_hcd->report_code_last = tcm_hcd->report_code;

	mutex_unlock(&mod_pool.mutex);

	return retval;
}

/*
 * Route one complete message: identify reports reconfigure the transport,
 * everything with a code below 0x10 is a command response, the rest belongs
 * to the functional modules.
 */
static int syna_tcm_dispatch_message(struct syna_tcm_hcd *tcm_hcd)
{
	unsigned int size;
	int retval;

	if (tcm_hcd->report_code == 0x10) {
		mutex_lock(&tcm_hcd->in.buf_mutex);

		size = min(tcm_hcd->payload_length,
			   (unsigned int)sizeof(tcm_hcd->identify_info));

		memcpy(tcm_hcd->identify_info, tcm_hcd->in.buf + 4, size);

		mutex_unlock(&tcm_hcd->in.buf_mutex);

		mutex_lock(&tcm_hcd->resp.buf_mutex);

		tcm_hcd->command_count = tcm_hcd->read_message_count;

		mutex_unlock(&tcm_hcd->resp.buf_mutex);

		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Received identify report (firmware mode = 0x%02x)\n",
			   __func__, tcm_hcd->identify_info[0x01]);

		mutex_lock(&tcm_hcd->resp.buf_mutex);

		if (tcm_hcd->response_ready == 1)
			complete(&response_complete);

		mutex_unlock(&tcm_hcd->resp.buf_mutex);

		return 0;
	}

	if (tcm_hcd->report_code == 0x0b) {
		dev_notice(&tcm_hcd->pdev->dev, "%s info: Device has been reset\n",
			   __func__);
		tcm_hcd->response_ready = -1;
		return 0;
	}

	if (tcm_hcd->dispatch_state) {
		dev_notice(&tcm_hcd->pdev->dev, "%s info: Helper thread is busy\n",
			   __func__);
		return 0;
	}

	mutex_lock(&tcm_hcd->in.buf_mutex);
	mutex_lock(&tcm_hcd->report_buffer.buf_mutex);

	tcm_hcd->report_buffer.data_length = tcm_hcd->payload_length;
	tcm_hcd->report_buffer.buf = tcm_hcd->in.buf + 4;
	tcm_hcd->report_buffer.buf_size = tcm_hcd->in.buf_size - 4;
	tcm_hcd->dispatch_report_code = tcm_hcd->report_code;

	if (tcm_hcd->touch_report_fn && tcm_hcd->report_code == REPORT_TOUCH) {
		((void (*)(void))tcm_hcd->touch_report_fn)();
		retval = 0;
	} else {
		retval = syna_tcm_dispatch_report(tcm_hcd);
	}

	mutex_unlock(&tcm_hcd->report_buffer.buf_mutex);
	mutex_unlock(&tcm_hcd->in.buf_mutex);

	return retval;
}

/**
 * syna_tcm_read_message() - read one complete message from the device.
 * @tcm_hcd: the driver context.
 * @buf: optional caller buffer, when NULL the internal in.buf is used and the
 *       message is dispatched to the functional modules.
 * @length: size of @buf, ignored when @buf is NULL.
 *
 * Return: zero on success, a negative error code otherwise.
 */
int syna_tcm_read_message(struct syna_tcm_hcd *tcm_hcd, unsigned char *buf,
			  unsigned int length)
{
	unsigned int max_count, count, chunk_size, chunk, need, pad_offset;
	bool retry = true;
	int retval = 0;

	mutex_lock(&tcm_hcd->rw_ctrl_mutex);

	if (buf) {
		retval = syna_tcm_raw_read(tcm_hcd, buf, length);
		goto exit;
	}

	if (length <= 1) {
		LOGE("%s error: (line %d) Invalid length information\n",
		     __func__, 0x4f9);
		retval = -EINVAL;
		goto exit;
	}

read:
	mutex_lock(&tcm_hcd->in.buf_mutex);

	retval = tcm_hcd->hw_if->io->read(tcm_hcd, tcm_hcd->in.buf,
					  tcm_hcd->command_count);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read from device\n",
		     __func__, 0x5d8);
		mutex_unlock(&tcm_hcd->in.buf_mutex);
		goto retry_or_exit;
	}

	if (tcm_hcd->in.buf[0] != MESSAGE_MARKER) {
		LOGE("%s error: (line %d) Incorrect header marker (0x%02x)\n",
		     __func__, 0x5e8, tcm_hcd->in.buf[0]);
		mutex_unlock(&tcm_hcd->in.buf_mutex);
		retval = -ENXIO;
		goto retry_or_exit;
	}

	tcm_hcd->report_code = tcm_hcd->in.buf[1];
	tcm_hcd->payload_length = tcm_hcd->in.buf[2] |
				  (tcm_hcd->in.buf[3] << 8);

	dev_dbg(&tcm_hcd->pdev->dev,
		"%s info: Status report code = 0x%02x\n",
		__func__, tcm_hcd->report_code);

	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Payload length = %d\n",
		__func__, tcm_hcd->payload_length);

	if (((tcm_hcd->report_code - 0x10) & 0xff) >= 0xef) {
		if (tcm_hcd->report_code > 3) {
			LOGE("%s error: (line %d) Incorrect Status code (0x%02x)\n",
			     __func__, 0x610, tcm_hcd->report_code);

			if (tcm_hcd->report_code != 0xff) {
				/* fall through, some reports use the low codes */
			} else {
				mutex_unlock(&tcm_hcd->in.buf_mutex);
				goto retry_or_exit;
			}
		}
	}

	need = tcm_hcd->payload_length + 5;
	pad_offset = tcm_hcd->payload_length + 4;

	if (need > tcm_hcd->in.buf_size) {
		if (pad_offset != tcm_hcd->in.buf_size) {
			mutex_unlock(&tcm_hcd->in.buf_mutex);

			retval = syna_tcm_realloc_mem(tcm_hcd, &tcm_hcd->in,
						      tcm_hcd->command_count + 6);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to reallocate memory for tcm_hcd->in.buf\n",
				     __func__, 0x47d);
				retval = -ENOMEM;
				goto retry_or_exit;
			}

			retval = syna_tcm_continued_read(tcm_hcd);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to do continued read\n",
				     __func__, 0x633);
				goto retry_or_exit;
			}

			tcm_hcd->in.buf[0] = MESSAGE_MARKER;
			tcm_hcd->in.buf[1] = tcm_hcd->report_code;
			tcm_hcd->in.buf[2] = tcm_hcd->payload_length & 0xff;
			tcm_hcd->in.buf[3] = (tcm_hcd->payload_length >> 8) & 0xff;

			mutex_lock(&tcm_hcd->in.buf_mutex);
		} else {
			tcm_hcd->in.buf[pad_offset] = MESSAGE_PADDING;
		}
	} else {
		if (tcm_hcd->in.buf[pad_offset] != MESSAGE_PADDING) {
			LOGE("%s error: (line %d) Incorrect message padding byte (0x%02x)\n",
			     __func__, 0x642, tcm_hcd->in.buf[pad_offset]);
			mutex_unlock(&tcm_hcd->in.buf_mutex);
			retval = -EIO;
			goto retry_or_exit;
		}
	}

	mutex_unlock(&tcm_hcd->in.buf_mutex);

	max_count = need > 9 ? need : 9;

	count = (tcm_hcd->max_response_size - 1 < max_count) ?
		tcm_hcd->max_response_size : max_count;

	tcm_hcd->command_count = count;

	if (!tcm_hcd->helper_thread)
		goto exit;

	if (tcm_hcd->report_code == 0x10) {
		retval = syna_tcm_dispatch_message(tcm_hcd);
		goto dispatch_done;
	}

	if (tcm_hcd->report_code < 0x10) {
		retval = syna_tcm_dispatch_response(tcm_hcd);
		goto dispatch_done;
	}

	mutex_lock(&tcm_hcd->in.buf_mutex);
	mutex_lock(&tcm_hcd->report_buffer.buf_mutex);

	tcm_hcd->report_buffer.data_length = tcm_hcd->payload_length;
	tcm_hcd->report_buffer.buf = tcm_hcd->in.buf + 4;
	tcm_hcd->report_buffer.buf_size = tcm_hcd->in.buf_size - 4;
	tcm_hcd->dispatch_report_code = tcm_hcd->report_code;

	retval = syna_tcm_dispatch_message(tcm_hcd);

	mutex_unlock(&tcm_hcd->report_buffer.buf_mutex);
	mutex_unlock(&tcm_hcd->in.buf_mutex);

dispatch_done:
	if (tcm_hcd->hdl_pending) {
		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Switched to TCM mode and going to download the configs\n",
			   __func__);
	}

	retval = 0;
	goto exit;

retry_or_exit:
	if (retry) {
		usleep_range(5000, 10000);
		retry = false;
		goto read;
	}

	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Out-of-sync continued read\n",
		__func__);

	tcm_hcd->payload_length = 0;

exit:
	if (tcm_hcd->response_ready == 1) {
		tcm_hcd->response_ready = -1;
		complete(&response_complete);
	}

	mutex_unlock(&tcm_hcd->rw_ctrl_mutex);

	return retval;
}

extern struct syna_tcm_module_desc zeroflash_module;
extern struct syna_tcm_module_desc reflash_module;
extern struct syna_tcm_module_desc device_module;
extern struct syna_tcm_module_desc testing_module;
extern struct syna_tcm_module_desc recovery_module;
extern struct syna_tcm_module_desc diag_module;

/**
 * syna_tcm_set_gpio() - claim a pin and give it a direction.
 * @tcm_hcd: handle to the driver context.
 * @gpio: pin number to claim.
 * @state: non-zero to drive the pin as a raw output, zero for an input.
 * @dir: raw value programmed when @state is non-zero.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_set_gpio(struct syna_tcm_hcd *tcm_hcd, int gpio, int state,
			     int dir)
{
	int retval;
	char label[16];

	retval = snprintf(label, sizeof(label), "tcm_gpio_%d\n", gpio);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set GPIO label\n",
			__func__, 0x86b);

		return retval;
	}

	retval = gpio_request(gpio, label);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to request GPIO %d\n",
			__func__, 0x872, gpio);

		return retval;
	}

	if (state)
		retval = gpiod_direction_output_raw(gpio_to_desc(gpio), dir);
	else
		retval = gpiod_direction_input(gpio_to_desc(gpio));

	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set GPIO %d direction\n",
			__func__, 0x87d, gpio);

	return retval;
}

/**
 * syna_tcm_check_hdl() - let the modules have another go at the last report.
 * @tcm_hcd: handle to the driver context.
 * @report_code: report code to pretend we just received.
 *
 * Used when a transfer failed mid-report: instead of dropping the payload on
 * the floor the modules are called again with whatever is left in the report
 * buffer.  Only modules that asked to be kept out of the way are skipped.
 */
static void syna_tcm_check_hdl(struct syna_tcm_hcd *tcm_hcd, u8 report_code)
{
	struct syna_tcm_module *module;

	mutex_lock(&tcm_hcd->report_buffer.buf_mutex);

	tcm_hcd->report_buffer.buf_size = 0;
	tcm_hcd->report_buffer.buf = NULL;
	tcm_hcd->dispatch_report_code = report_code;

	mutex_unlock(&tcm_hcd->report_buffer.buf_mutex);

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry(module, &mod_pool.list, node) {
		if (module->init_pending)
			continue;

		if (module->remove_pending)
			continue;

		if (module->desc && module->desc->handler)
			module->desc->handler(tcm_hcd);
	}

	mutex_unlock(&mod_pool.mutex);
}

/**
 * syna_tcm_isr() - the threaded interrupt handler.
 * @irq: interrupt number, unused.
 * @data: handle to the driver context.
 *
 * The level was sampled before we got here, so an interrupt that does not see
 * the expected level is a spurious one and is dropped silently.
 *
 * Return: IRQ_HANDLED.
 */
static irqreturn_t syna_tcm_isr(int irq, void *data)
{
	struct syna_tcm_hcd *tcm_hcd = data;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	int retval;

	if (gpiod_get_raw_value(gpio_to_desc(bdata->irq_gpio)) !=
	    bdata->irq_on_state)
		goto exit;

	/* remember who is talking to the sensor, write_message validates it */
	tcm_hcd->context_pid = current->pid;

	retval = tcm_hcd->read_message_fn(tcm_hcd, NULL, 0);
	if (retval < 0) {
		if (tcm_hcd->shut_down)
			goto exit;

		if (tcm_hcd->mode == 2) {
			syna_tcm_check_hdl(tcm_hcd, 0xfe);
		} else {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to read message\n",
				__func__, 0x800);
		}
	}

exit:
	return IRQ_HANDLED;
}

/**
 * syna_tcm_enable_irq() - arm or disarm the interrupt line.
 * @tcm_hcd: handle to the driver context.
 * @en: true to enable, false to disable.
 * @irq_flag: when disabling, keep the handler registered and only park it.
 *
 * The very first enable registers the threaded handler; later enables just
 * unmask the line.  irq_freed remembers which of the two the next enable has to
 * do.  When the line cannot be armed at all the driver falls back to polling
 * the part from a delayed work item.
 *
 * Return: zero.
 */
static int syna_tcm_enable_irq(struct syna_tcm_hcd *tcm_hcd, bool en,
			       bool irq_flag)
{
	static bool irq_freed;
	int retval = 0;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	mutex_lock(&tcm_hcd->irq_en_mutex);

	if (en) {
		if (tcm_hcd->irq_registered) {
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Interrupt already enabled\n",
				__func__);
			goto exit;
		}

		if (bdata->irq_gpio < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Invalid IRQ GPIO\n",
				__func__, 0x819);
			goto queue_polling;
		}

		if (irq_freed)
			retval = request_threaded_irq(tcm_hcd->irq, NULL,
						      syna_tcm_isr,
						      IRQF_ONESHOT |
						      IRQF_TRIGGER_HIGH,
						      "synaptics_tcm", tcm_hcd);
		else
			enable_irq(tcm_hcd->irq);

		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to create interrupt thread\n",
				__func__, 0x824);
			goto queue_polling;
		}

		msleep(20);
	} else {
		if (!tcm_hcd->irq_registered) {
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Interrupt already disabled\n",
				__func__);
			goto exit;
		}

		if (bdata->irq_gpio >= 0) {
			if (irq_flag) {
				disable_irq_nosync(tcm_hcd->irq);
			} else {
				disable_irq(tcm_hcd->irq);
				free_irq(tcm_hcd->irq, tcm_hcd);
			}

			irq_freed = !irq_flag;
		}

		if (irq_flag) {
			cancel_delayed_work(&tcm_hcd->polling_work);
		} else {
			cancel_delayed_work_sync(&tcm_hcd->polling_work);
			flush_workqueue(tcm_hcd->polling_wq);
		}

		tcm_hcd->in_suspend = 0;
	}

	goto exit;

queue_polling:
	queue_delayed_work_on(WORK_CPU_UNBOUND, tcm_hcd->polling_wq,
			      &tcm_hcd->polling_work, 2);
	tcm_hcd->in_suspend = 1;
	msleep(20);

exit:
	tcm_hcd->irq_registered = en;
	mutex_unlock(&tcm_hcd->irq_en_mutex);

	return 0;
}

/**
 * syna_tcm_module_work() - service the module pool.
 * @work: the pool work item.
 *
 * A module that was just registered has its init callback called here rather
 * than in syna_tcm_add_module(), because that runs before the sensor is awake.
 * A module whose owner asked for its removal gets its remove callback called
 * and is dropped from the pool; until then both flags make it invisible to the
 * report dispatcher.
 */
static void syna_tcm_module_work(struct work_struct *work)
{
	struct syna_tcm_module *module, *next;
	struct syna_tcm_hcd *tcm_hcd = mod_pool.hcd;

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry_safe(module, next, &mod_pool.list, node) {
		if (module->init_pending) {
			if (module->desc->init)
				module->desc->init(tcm_hcd);

			module->init_pending = 0;
		}

		if (!module->remove_pending)
			continue;

		if (module->desc->remove)
			module->desc->remove(tcm_hcd);

		list_del(&module->node);
		kfree(module);
	}

	mutex_unlock(&mod_pool.mutex);
}

/*
 * The command callbacks live behind void pointers in the context so that the
 * structure layout stays checkable; these casts keep the call sites readable.
 */
#define tcm_write_message(hcd, ...)					   	((int (*)(struct syna_tcm_hcd *, unsigned char, unsigned char *,	   		  unsigned int, unsigned char **, unsigned int *,		   		  unsigned char *, unsigned char *, unsigned int))		   	 (hcd)->write_message_fn)((hcd), __VA_ARGS__)

#define tcm_enable_irq(hcd, en, flag)					   	((int (*)(struct syna_tcm_hcd *, bool, bool))			   	 (hcd)->enable_irq_fn)((hcd), (en), (flag))

#define tcm_read_message(hcd, buf, len)					   	((int (*)(struct syna_tcm_hcd *, unsigned char *, unsigned int))	   	 (hcd)->read_message_fn)((hcd), (buf), (len))

/**
 * syna_tcm_check_f35() - probe the F35 function until the part answers.
 * @tcm_hcd: handle to the driver context.
 *
 * A part that still runs its ROM boot loader only answers RMI reads, so the
 * driver keeps asking for function number 0xee and pulsing reset until F$35
 * shows up.  Ten attempts is what the vendor firmware needs on a cold boot.
 *
 * Return: zero when F$35 is there, -ENODEV otherwise.
 */
static int syna_tcm_check_f35(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	int try_times = 0;
	unsigned char function_number = 0;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	do {
		retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd, 0xee,
						      &function_number, 1);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to read F35 function number\n",
				__func__, 0xe25);
			break;
		}

		dev_dbg(&tcm_hcd->pdev->dev, "%s info: Found F$%02x\n",
			__func__, function_number);

		if (function_number == 0x35)
			return 0;

		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to find F$35, try_times = %d\n",
			__func__, 0xe31, try_times);

		if (try_times == 10)
			break;

		msleep(100);

		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio), 0);
		msleep(5);
		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio), 1);
		msleep(5);

		try_times++;
	} while (1);

	tcm_hcd->helper_thread = 0;

	return -ENODEV;
}

/**
 * syna_tcm_wait_hdl() - wait for a host download to finish.
 * @tcm_hcd: handle to the driver context.
 *
 * The helper thread signals completion once the part has left the host
 * download firmware, so all this has to do is bound the wait.  The flag is
 * cleared on timeout, otherwise a stuck download would keep every later
 * caller waiting for the full timeout.
 *
 * Return: zero on success, -EIO when the download never completed.
 */
static int syna_tcm_wait_hdl(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	msleep(100);

	if (!tcm_hcd->hdl_pending)
		return 0;

	retval = wait_event_interruptible_timeout(tcm_hcd->hdl_wq,
						  tcm_hcd->hdl_pending,
						  msecs_to_jiffies(5000));
	if (!retval) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Timed out waiting for completion of host download\n",
			__func__, 0x75f);
		tcm_hcd->hdl_pending = 0;
		return -EIO;
	}

	return 0;
}

/**
 * syna_tcm_reset() - ask the sensor to reset itself.
 * @tcm_hcd: handle to the driver context.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_reset(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	retval = tcm_write_message(tcm_hcd, CMD_RESET, NULL, 0, &resp_buf,
				   &resp_length, &response_code, NULL,
				   bdata->reset_delay_ms);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xbea, "CMD_RESET");

	return retval;
}

/**
 * syna_tcm_reset_and_reinit() - reset the part and rebuild the driver state.
 * @tcm_hcd: handle to the driver context.
 * @hw_reset: true to pulse the reset pin instead of sending a command.
 * @reinit: kept for the CFI signature, the vendor never looks at it.
 *
 * A part that answers in a firmware mode other than application or host
 * download is first asked to run the application firmware, and only then are
 * the functional modules given their reinit callback.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_reset_and_reinit(struct syna_tcm_hcd *tcm_hcd,
				     bool hw_reset, bool reinit)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned int size;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct syna_tcm_module *module;

	(void)reinit;

	mutex_lock(&tcm_hcd->reset_mutex);

	if (hw_reset) {
		if (bdata->reset_gpio < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Hardware reset unavailable\n",
				__func__, 0xc07);
			retval = -ENODEV;
			goto unlock_free;
		}

		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
				    bdata->reset_on_state);
		msleep(bdata->reset_active_ms);
		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
				    !bdata->reset_on_state);
	} else {
		retval = tcm_write_message(tcm_hcd, CMD_RESET, NULL, 0,
					   &resp_buf, &resp_length,
					   &response_code, NULL,
					   bdata->reset_delay_ms);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				"syna_tcm_reset", 0xbea, "CMD_RESET");
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 0xc12);
			goto unlock_free;
		}
	}

	/*
	 * A download in flight keeps the interrupt armed, so there is nothing to
	 * identify and the helper thread owns the rest of the bring up.
	 */
	if (tcm_hcd->irq_enabled) {
		mutex_unlock(&tcm_hcd->reset_mutex);
		kfree(resp_buf);
		resp_buf = NULL;

		retval = syna_tcm_wait_hdl(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to wait for completion of host download\n",
				__func__, 0xc20);
			return retval;
		}

		return 0;
	}

	msleep(bdata->reset_delay_ms);

	retval = ((int (*)(struct syna_tcm_hcd *, bool))
		  tcm_hcd->identify_fn)(tcm_hcd, false);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do identification\n",
			__func__, 0xc30);
		goto unlock_free;
	}

	if ((tcm_hcd->identify_info[1] - 1) > 1) {
		retval = tcm_write_message(tcm_hcd,
					   CMD_RUN_APPLICATION_FIRMWARE, NULL, 0,
					   &resp_buf, &resp_length,
					   &response_code, NULL, 100);
		if (retval < 0)
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Failed to write command %s\n",
				   __func__, "CMD_RUN_APPLICATION_FIRMWARE");

		retval = ((int (*)(struct syna_tcm_hcd *, bool))
			  tcm_hcd->identify_fn)(tcm_hcd, false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do identification\n",
				__func__, 0xc49);
			goto unlock_free;
		}
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Firmware mode = 0x%02x\n",
		   __func__, tcm_hcd->identify_info[1]);

	if ((tcm_hcd->identify_info[1] - 1) >= 2)
		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Boot status = 0x%02x\n",
			   __func__, tcm_hcd->boot_info[1]);
	else if (tcm_hcd->status)
		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Application status = 0x%02x\n",
			   __func__, tcm_hcd->status);

	if ((tcm_hcd->identify_info[1] - 1) <= 1) {
		retval = tcm_write_message(tcm_hcd, CMD_GET_FEATURES, NULL, 0,
					   &resp_buf, &resp_length,
					   &response_code, NULL, 0);
		if (retval < 0) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Failed to write command %s\n",
				   __func__, "CMD_GET_FEATURES");
		} else {
			size = min_t(unsigned int, response_code,
				     sizeof(tcm_hcd->feature_description));

			if (!secure_memcpy(tcm_hcd->feature_description,
					   sizeof(tcm_hcd->feature_description),
					   resp_buf, size))
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to copy feature description\n",
					__func__, 0xc74);
		}
	}

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry(module, &mod_pool.list, node) {
		if (module->init_pending || module->remove_pending)
			continue;

		if (module->desc->reinit)
			module->desc->reinit(tcm_hcd);
	}

	mutex_unlock(&mod_pool.mutex);

	retval = 0;

unlock_free:
	mutex_unlock(&tcm_hcd->reset_mutex);
	kfree(resp_buf);

	return retval;
}

/*
 * Bring the sensor back to a known baseline.  The module carries no symbol
 * for this, the vendor inlines the same five lines wherever it is needed.
 */
static int syna_tcm_rezero(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;

	retval = tcm_write_message(tcm_hcd, CMD_REZERO, NULL, 0, &resp_buf,
				   &resp_length, &response_code, NULL, 0);
	kfree(resp_buf);

	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xcab, "CMD_REZERO");

	return retval;
}

/**
 * syna_tcm_sleep() - park the sensor in deep sleep or wake it up again.
 * @tcm_hcd: handle to the driver context.
 * @en: true to enter deep sleep, false to leave it.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_sleep(struct syna_tcm_hcd *tcm_hcd, bool en)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned char command = en ? CMD_ENTER_DEEP_SLEEP :
			      CMD_EXIT_DEEP_SLEEP;

	retval = tcm_write_message(tcm_hcd, command, NULL, 0, &resp_buf,
				   &resp_length, &response_code, NULL, 0);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xbca,
			en ? "CMD_ENTER_DEEP_SLEEP" : "CMD_EXIT_DEEP_SLEEP");

	kfree(resp_buf);

	return retval;
}

static int syna_tcm_get_app_info(struct syna_tcm_hcd *tcm_hcd);
static int syna_tcm_get_romboot_info(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_get_boot_info() - read the boot loader info block.
 * @tcm_hcd: handle to the driver context.
 *
 * Only a part sitting in the boot loader answers this one, and it answers with
 * the plain identify command rather than a dedicated boot info request.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_get_boot_info(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned int size;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;

	retval = tcm_write_message(tcm_hcd, 0x10, NULL, 0, &resp_buf,
				   &resp_length, &response_code, NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0x971, "CMD_GET_BOOT_INFO");
		goto out;
	}

	size = MIN(response_code, sizeof(tcm_hcd->boot_info));

	if (!resp_buf || size > resp_length) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy boot info\n",
			__func__, 0x97c);
		retval = -EINVAL;
		goto out;
	}

	memcpy(tcm_hcd->boot_info, resp_buf, size);

	retval = 0;

out:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_identify() - ask the sensor who it is, then fetch its info block.
 * @tcm_hcd: handle to the driver context.
 * @id: true to send CMD_IDENTIFY first and unpack the reply.
 *
 * The vendor splits this in two halves that share one critical section: when
 * @id is set the plain identify command is sent and its payload copied into the
 * context (which is where the firmware mode comes from), and either way the
 * right info block is then pulled for the mode the part reports.  A part can
 * answer from three places, hence the three ranges below.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_identify(struct syna_tcm_hcd *tcm_hcd, bool id)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	u16 read_message_count;
	u32 build_id;
	u8 firmware_mode;

	mutex_lock(&tcm_hcd->identify_mutex);

	if (id) {
		retval = tcm_write_message(tcm_hcd, CMD_IDENTIFY, NULL, 0,
					   &resp_buf, &resp_length,
					   &response_code, NULL, 0);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				__func__, 0x9e9, "CMD_IDENTIFY");
			goto exit;
		}

		if (secure_memcpy(tcm_hcd->identify_info,
				  sizeof(tcm_hcd->identify_info), resp_buf,
				  MIN(response_code,
				      sizeof(tcm_hcd->identify_info)))) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to copy identification info\n",
				__func__, 0x9f4);
			retval = -EINVAL;
			goto exit;
		}

		build_id = get_unaligned_le32(&tcm_hcd->identify_info[0x12]);
		read_message_count =
			get_unaligned_le16(&tcm_hcd->identify_info[0x16]);

		tcm_hcd->pad12 = build_id;

		read_message_count = MIN(read_message_count, 0x100);
		tcm_hcd->read_message_count = read_message_count ?
					      read_message_count :
					      get_unaligned_le16(
						      &tcm_hcd->identify_info[0x16]);

		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Firmware build id = %d\n",
			   __func__, build_id);
	}

	firmware_mode = tcm_hcd->identify_info[0x01];

	if (firmware_mode - 1 < 2) {
		retval = syna_tcm_get_app_info(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get application info\n",
				__func__, 0xa0a);
			goto exit;
		}
	} else if (firmware_mode - 0x0b < 2) {
		dev_dbg(&tcm_hcd->pdev->dev,
			"%s info: In bootloader mode\n", __func__);

		retval = syna_tcm_get_boot_info(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get boot info\n",
				__func__, 0xa17);
			goto exit;
		}
	} else if (firmware_mode == 4) {
		dev_dbg(&tcm_hcd->pdev->dev,
			"%s info: In rombootloader mode\n", __func__);

		retval = syna_tcm_get_romboot_info(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get application info\n",
				__func__, 0xa23);
			goto exit;
		}
	}

	retval = 0;

exit:
	mutex_unlock(&tcm_hcd->identify_mutex);

	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_get_app_info() - fetch the application firmware's status block.
 * @tcm_hcd: handle to the driver context.
 *
 * A freshly started application image reports status 1 or 2 while it is still
 * bringing itself up, so the driver keeps asking for up to a second before it
 * accepts whatever the part reports.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_get_app_info(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned int size;
	int retry = 1000;

	do {
		retval = tcm_write_message(tcm_hcd, CMD_GET_APPLICATION_INFO,
					   NULL, 0, &resp_buf, &resp_length,
					   &response_code, NULL, 0);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				__func__, 0x939, "CMD_GET_APPLICATION_INFO");
			break;
		}

		size = MIN(response_code,
			   (unsigned int)sizeof(tcm_hcd->app_info));

		if (secure_memcpy(tcm_hcd->app_info,
				  sizeof(tcm_hcd->app_info), resp_buf, size)) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to copy application info\n",
				__func__, 0x944);
			retval = -EINVAL;
			break;
		}

		tcm_hcd->status = tcm_hcd->app_info[0x02] |
				  (tcm_hcd->app_info[0x03] << 8);

		retval = 0;

		if (retry > 0 && tcm_hcd->status - 1 <= 1) {
			msleep(100);
			retry -= 100;
			continue;
		}

		break;
	} while (1);

	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_get_romboot_info() - fetch the ROM boot loader's status block.
 * @tcm_hcd: handle to the driver context.
 *
 * Only four of the ten bytes are useful to the download path: the payload size
 * and the flash block/granularity limits.  The rest is logged for support.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_get_romboot_info(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned int size;
	struct romboot_info *info =
		(struct romboot_info *)tcm_hcd->romboot_info;

	retval = tcm_write_message(tcm_hcd, CMD_GET_ROMBOOT_INFO, NULL, 0,
				   &resp_buf, &resp_length, &response_code,
				   NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0x99e, "CMD_GET_ROMBOOT_INFO");
		goto exit;
	}

	size = MIN(response_code, (unsigned int)sizeof(*info));

	if (secure_memcpy(tcm_hcd->romboot_info, sizeof(*info), resp_buf,
			  size)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy boot info\n",
			__func__, 0x9a9);
		retval = -EINVAL;
		goto exit;
	}

	retval = 0;

	dev_dbg(&tcm_hcd->pdev->dev, "%s info: version = %d\n",
		__func__, info->version);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: status = 0x%02x\n",
		__func__, info->status);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: version = 0x%02x 0x%02x\n",
		__func__, info->version_0, info->version_1);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: write_block_size_words = %d\n",
		__func__, info->write_block_size_words);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: max_write_payload_size = %d\n",
		__func__, info->max_write_payload_size);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: last_reset_reason = 0x%02x\n",
		__func__, info->last_reset_reason);

exit:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_get_dynamic_config() - read one dynamic configuration item.
 * @tcm_hcd: handle to the driver context.
 * @id: item to read.
 * @value: filled with the 16 bit value the sensor reports.
 *
 * Return: zero on success, a negative errno otherwise.
 */
int syna_tcm_get_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
				enum dynamic_config_id id, u16 *value)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned char payload = (unsigned char)id;

	retval = tcm_write_message(tcm_hcd, CMD_GET_DYNAMIC_CONFIG, &payload,
				   1, &resp_buf, &resp_length,
				   &response_code, NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xb34, "CMD_GET_DYNAMIC_CONFIG");
		goto exit;
	}

	if (response_code != 2) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data length\n",
			__func__, 0xb3a);
		retval = -EINVAL;
		goto exit;
	}

	*value = get_unaligned_le16(resp_buf);

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_set_dynamic_config() - change one dynamic configuration item.
 * @tcm_hcd: handle to the driver context.
 * @id: item to change.
 * @value: new 16 bit value.
 *
 * Return: zero on success, a negative errno otherwise.
 */
int syna_tcm_set_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
				enum dynamic_config_id id, u16 value)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned char payload[3];

	payload[0] = (unsigned char)id;
	payload[1] = value & 0xff;
	payload[2] = (value >> 8) & 0xff;

	retval = tcm_write_message(tcm_hcd, CMD_SET_DYNAMIC_CONFIG, payload, 3,
				   &resp_buf, &resp_length, &response_code,
				   NULL, 0);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xb65, "CMD_SET_DYNAMIC_CONFIG");

	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_get_data_location() - find where an image lives in flash.
 * @tcm_hcd: handle to the driver context.
 * @area: flash area to look up.
 * @addr: filled with the byte offset of the image.
 * @length: filled with the size of the image.
 *
 * Return: zero on success, a negative errno otherwise.
 */
int syna_tcm_get_data_location(struct syna_tcm_hcd *tcm_hcd,
			       enum flash_area area, u32 *addr, u32 *length)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned char payload = (unsigned char)area;

	if (area >= FLASH_AREA_COUNT) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid flash area\n",
			__func__, 0xb86);
		return -EINVAL;
	}

	retval = tcm_write_message(tcm_hcd, CMD_GET_DATA_LOCATION, &payload, 1,
				   &resp_buf, &resp_length, &response_code,
				   NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xb99, "CMD_GET_DATA_LOCATION");
		goto exit;
	}

	if (response_code != 4) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data length\n",
			__func__, 0xb9f);
		retval = -EINVAL;
		goto exit;
	}

	*addr = get_unaligned_le16(resp_buf);
	*length = get_unaligned_le16(resp_buf + 2);

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_polling_work() - read the sensor when the interrupt is unusable.
 * @work: the context's polling work item.
 *
 * Only runs while the driver is parked (in_suspend) - that is the state the
 * interrupt path leaves behind when it could not arm the line.  A -ENXIO from a
 * host download transport means the part rebooted into its ROM boot loader, so
 * the boot probe is re-run and the state machine gets a 0xfe report.
 */
/*
 * The touch module owns the input device and the wakeup gesture flag, both
 * the system sleep callbacks just hand the context over.
 */
extern int touch_suspend(struct syna_tcm_hcd *tcm_hcd);
extern int touch_resume(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_suspend() - quiesce the sensor before the system sleeps.
 * @dev: device being suspended.
 *
 * Every functional module gets a chance to park itself first, then the
 * interrupt is disarmed.  polling_disabled doubles as the "already suspended"
 * marker so a second call is a no-op and resume knows what to undo.
 *
 * Return: zero.
 */
static int syna_tcm_suspend(struct device *dev)
{
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);
	struct syna_tcm_module *module;

	if (tcm_hcd->polling_disabled)
		return 0;

	dev_info(dev, "%s info: %s\n", __func__, __func__);

	touch_suspend(tcm_hcd);

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry(module, &mod_pool.list, node) {
		if (module->init_pending || module->remove_pending)
			continue;

		if (module->desc->suspend)
			module->desc->suspend(tcm_hcd);
	}

	mutex_unlock(&mod_pool.mutex);

	if (!tcm_hcd->irq_no_disable)
		tcm_enable_irq(tcm_hcd, false, true);

	tcm_hcd->polling_disabled = 1;

	return 0;
}

/**
 * syna_tcm_resume() - bring the sensor back after the system wakes up.
 * @dev: device being resumed.
 *
 * Only touch the interrupt when it was armed before the suspend: a part that
 * was left in host download mode is driven by the download path instead.  A
 * part that is not running its application or host download firmware needs a
 * full reset before anything can talk to it again.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_resume(struct device *dev)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);
	struct syna_tcm_module *module;

	if (!tcm_hcd->polling_disabled)
		return 0;

	dev_info(dev, "%s info: %s\n", __func__, __func__);

	if (tcm_hcd->irq_enabled) {
		if (!tcm_hcd->irq_no_disable) {
			tcm_enable_irq(tcm_hcd, true, false);

			retval = syna_tcm_wait_hdl(tcm_hcd);
			if (retval < 0) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to wait for completion of host download\n",
					__func__, 0xd41);
				goto out_clear;
			}

			goto resume_done;
		}
	} else if (!tcm_hcd->irq_no_disable) {
		tcm_enable_irq(tcm_hcd, true, false);
	}

	if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Identifying mode = 0x%02x\n",
			   __func__, tcm_hcd->identify_info[1]);

		retval = ((int (*)(struct syna_tcm_hcd *, bool, bool))
			  tcm_hcd->reset_and_reinit_fn)(tcm_hcd, false, true);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset and reinit\n",
				__func__, 0xd6c);
			goto out_clear;
		}

		if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Identifying mode = 0x%02x\n",
				   __func__, tcm_hcd->identify_info[1]);
			retval = 0;
			goto out_clear;
		}
	} else {
		retval = ((int (*)(struct syna_tcm_hcd *, bool))
			  tcm_hcd->sleep_fn)(tcm_hcd, false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to exit deep sleep\n",
				__func__, 0xd5b);
			goto out_clear;
		}

		retval = syna_tcm_rezero(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to rezero\n",
				__func__, 0xd62);
			goto out_clear;
		}
	}

resume_done:
	touch_resume(tcm_hcd);

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry(module, &mod_pool.list, node) {
		if (module->init_pending || module->remove_pending)
			continue;

		if (module->desc->resume)
			module->desc->resume(tcm_hcd);
	}

	mutex_unlock(&mod_pool.mutex);

	retval = 0;

out_clear:
	tcm_hcd->polling_disabled = 0;

	return retval;
}

static int syna_tcm_resume_adapter(struct device *dev)
{
	return syna_tcm_resume(dev);
}

static void syna_tcm_polling_work(struct work_struct *work)
{
	struct syna_tcm_hcd *tcm_hcd =
		container_of(work, struct syna_tcm_hcd, polling_work.work);
	int retval;

	if (!tcm_hcd->in_suspend)
		return;

	retval = tcm_read_message(tcm_hcd, NULL, 0);

	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read message\n",
			__func__, 0x7dd);

		if (retval == -ENXIO && tcm_hcd->hw_if->io->type == 0x1c)
			syna_tcm_check_hdl(tcm_hcd, 0xfe);

		if (tcm_hcd->polling_disabled)
			return;
	}

	queue_delayed_work_on(WORK_CPU_UNBOUND, tcm_hcd->polling_wq,
			      &tcm_hcd->polling_work, 2);
}

static void touch_reinit(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_helper_work() - finish what the identify report started.
 * @work: the helper work item embedded in the driver context.
 *
 * The interrupt handler cannot switch firmware modes or rebuild the input
 * device on its own, so it parks the work that has to happen in the dispatch
 * state and lets this work item pick it up outside interrupt context.
 */
static void syna_tcm_helper_work(struct work_struct *work)
{
	struct syna_tcm_hcd *tcm_hcd =
		container_of(work, struct syna_tcm_hcd, helper_work);
	struct syna_tcm_module *module;
	int retval;

	if (tcm_hcd->shut_down)
		goto exit;

	switch (tcm_hcd->dispatch_state) {
	case 1:
		mutex_lock(&tcm_hcd->reset_mutex);

		retval = syna_tcm_run_application_firmware(tcm_hcd);
		if (retval < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to switch to application mode\n",
				__func__, 0xcd0);

		mutex_unlock(&tcm_hcd->reset_mutex);
		break;
	case 2:
		retval = touch_reinit(tcm_hcd);
		if (retval < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to re-initialze touch reporting\n",
				__func__, 0xd03);
		break;
	case 3:
		syna_tcm_check_hdl(tcm_hcd, 0xfd);
		break;
	case 4:
		mutex_lock(&tcm_hcd->reset_mutex);

		retval = ((int (*)(struct syna_tcm_hcd *, bool))
			  tcm_hcd->identify_fn)(tcm_hcd, true);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Application firmware is not running\n",
				__func__, 0xce1);
			goto unlock_reset;
		}

		retval = touch_reinit(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to initialze touch reporting\n",
				__func__, 0xceb);
			goto unlock_reset;
		}

		mutex_lock(&mod_pool.mutex);

		list_for_each_entry(module, &mod_pool.list, node) {
			if (!module->desc->reinit)
				continue;

			module->desc->reinit(tcm_hcd);
		}

		mutex_unlock(&mod_pool.mutex);

		/* the download paths wait on this queue while the modes switch */
		wake_up_interruptible(&tcm_hcd->hdl_wq);
unlock_reset:
		mutex_unlock(&tcm_hcd->reset_mutex);
		break;
	default:
		break;
	}

exit:
	tcm_hcd->dispatch_state = 0;
}

/**
 * syna_tcm_run_application_firmware() - switch the part to its application image.
 * @tcm_hcd: handle to the driver context.
 *
 * The command only takes effect once the part is out of the boot loader, and it
 * can land before the application has finished coming up, so a second attempt
 * is made before the mode is inspected.  A non-zero application status is
 * reported but not treated as fatal.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_run_application_firmware(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;

	retval = tcm_write_message(tcm_hcd, CMD_RUN_APPLICATION_FIRMWARE,
				   NULL, 0, &resp_buf, &resp_length,
				   &response_code, NULL, 100);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xa84, "CMD_RUN_APPLICATION_FIRMWARE");
		goto exit;
	}

	retval = ((int (*)(struct syna_tcm_hcd *, bool))
		  tcm_hcd->identify_fn)(tcm_hcd, false);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do identification\n",
			__func__, 0xa8b);
		goto exit;
	}

	if (tcm_hcd->identify_info[0x01] - 1 >= 2) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware (boot status = 0x%02x)\n",
			__func__, 0xa92, tcm_hcd->boot_info[0x01]);

		retval = tcm_write_message(tcm_hcd,
					   CMD_RUN_APPLICATION_FIRMWARE,
					   NULL, 0, &resp_buf, &resp_length,
					   &response_code, NULL, 100);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				__func__, 0xa84,
				"CMD_RUN_APPLICATION_FIRMWARE");
			goto exit;
		}

		retval = ((int (*)(struct syna_tcm_hcd *, bool))
			  tcm_hcd->identify_fn)(tcm_hcd, false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do identification\n",
				__func__, 0xa8b);
			goto exit;
		}

		if (tcm_hcd->identify_info[0x01] - 1 >= 2) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to run application firmware (boot status = 0x%02x)\n",
				__func__, 0xa92,
				tcm_hcd->boot_info[0x01]);
			retval = -EINVAL;
			goto exit;
		}
	}

	if (tcm_hcd->status)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Application status = 0x%02x\n",
			__func__, 0xa9c, tcm_hcd->status);

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

static int syna_tcm_run_application_firmware(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_run_bootloader_firmware() - park the part in its boot loader.
 * @tcm_hcd: handle to the driver context.
 *
 * A part that still sits in the ROM boot loader takes the 0x42 command, one
 * that already runs application firmware has to be asked politely with 0x1f and
 * then re-identified to confirm it really moved.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_run_bootloader_firmware(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;
	unsigned char command;
	u8 firmware_mode;

	firmware_mode = tcm_hcd->identify_info[0x01];

	command = (firmware_mode == 4) ? CMD_ROMBOOT_RUN_BOOTLOADER_FIRMWARE :
					 CMD_RUN_BOOTLOADER_FIRMWARE;

	retval = tcm_write_message(tcm_hcd, command, NULL, 0, &resp_buf,
				   &resp_length, &response_code, NULL, 100);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, firmware_mode == 4 ? 0xac2 : 0xac6,
			firmware_mode == 4 ?
				"CMD_ROMBOOT_RUN_BOOTLOADER_FIRMWARE" :
				"CMD_RUN_BOOTLOADER_FIRMWARE");
		goto exit;
	}

	if (firmware_mode != 4) {
		retval = ((int (*)(struct syna_tcm_hcd *, bool))
			  tcm_hcd->identify_fn)(tcm_hcd, false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do identification\n",
				__func__, 0xacf);
			goto exit;
		}

		firmware_mode = tcm_hcd->identify_info[0x01];

		/* 1 and 2 both still mean application firmware is running */
		if (firmware_mode - 1 <= 1) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to enter bootloader mode\n",
				__func__, 0xad5);
			retval = -EINVAL;
			goto exit;
		}
	}

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_run_production_test_firmware() - switch to the factory test image.
 * @tcm_hcd: handle to the driver context.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_run_production_test_firmware(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned char *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char response_code = 0;

	retval = tcm_write_message(tcm_hcd, CMD_ENTER_PRODUCTION_TEST_MODE, NULL,
				   0, &resp_buf, &resp_length, &response_code,
				   NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0xa4f, "CMD_ENTER_PRODUCTION_TEST_MODE");
		goto exit;
	}

	if (tcm_hcd->identify_info[0x01] != 0x0e) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run production test firmware\n",
			__func__, 0xa55);
		retval = -EINVAL;
		goto exit;
	}

	if (tcm_hcd->status)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Application status = 0x%02x\n",
			__func__, 0xa5f, tcm_hcd->status);

	retval = 0;

exit:
	kfree(resp_buf);

	return retval;
}

/**
 * syna_tcm_switch_mode() - move the part between its three firmwares.
 * @tcm_hcd: handle to the driver context.
 * @mode: which image to run next.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_switch_mode(struct syna_tcm_hcd *tcm_hcd,
				enum tcm_mode mode)
{
	int retval;

	mutex_lock(&tcm_hcd->reset_mutex);

	switch (mode) {
	case MODE_LOAD_BOOTLOADER:
		retval = syna_tcm_run_bootloader_firmware(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to switch to bootloader mode\n",
				__func__, 0xaf3);
			goto exit;
		}
		break;
	case MODE_LOAD_APPLICATION:
		retval = syna_tcm_run_application_firmware(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to switch to application mode\n",
				__func__, 0xafb);
			goto exit;
		}
		break;
	case MODE_LOAD_PRODUCTION_TEST:
		retval = syna_tcm_run_production_test_firmware(tcm_hcd);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to switch to production test mode\n",
				__func__, 0xb03);
			goto exit;
		}
		break;
	default:
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid firmware mode\n",
			__func__, 0xb09);
		retval = -EINVAL;
		goto exit;
	}

	retval = 0;

exit:
	mutex_unlock(&tcm_hcd->reset_mutex);

	return retval;
}

/*
 * Two of the nodes are only meaningful in one direction, the stubs keep them
 * in the group so that the attribute list matches the one user space expects.
 */
static ssize_t syna_tcm_store_error(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	pr_err("%s: Attribute not writable\n", __func__);

	return -1;
}

static ssize_t syna_tcm_show_error(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	pr_err("%s: Attribute not readable\n", __func__);

	return -1;
}

static ssize_t syna_tcm_sysfs_input_name_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%s\n", SYNA_TCM_INPUT_NAME);
}

static ssize_t syna_tcm_sysfs_ts_suspend_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n",
		       tcm_hcd->polling_disabled ? "true" : "false");
}

static ssize_t syna_tcm_sysfs_ts_suspend_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	retval = kstrtouint(buf, 10, &input);
	if (retval)
		return -EINVAL;

	if (input == 0)
		syna_tcm_resume_adapter(&tcm_hcd->pdev->dev);
	else if (input == 1)
		syna_tcm_suspend(&tcm_hcd->pdev->dev);
	else
		return -EINVAL;

	return count;
}

static ssize_t syna_tcm_sysfs_reset_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	if ((input - 1) > 1)
		return -EINVAL;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = ((int (*)(struct syna_tcm_hcd *, bool, bool))
		  tcm_hcd->reset_and_reinit_fn)(tcm_hcd, input != 1, true);
	if (retval < 0) {
		dev_err(dev,
			"%s error: (line %d) Failed to do reset and reinit\n",
			__func__, 0x263);
		mutex_unlock(&tcm_hcd->extif_mutex);

		return retval;
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t syna_tcm_sysfs_irq_en_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (input == 0) {
		retval = tcm_enable_irq(tcm_hcd, false, true);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to disable interrupt\n",
				__func__, 0x235);
			mutex_unlock(&tcm_hcd->extif_mutex);

			return retval;
		}
	} else if (input == 1) {
		retval = tcm_enable_irq(tcm_hcd, true, false);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to enable interrupt\n",
				__func__, 0x23c);
			mutex_unlock(&tcm_hcd->extif_mutex);

			return retval;
		}
	} else {
		mutex_unlock(&tcm_hcd->extif_mutex);

		return -EINVAL;
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t syna_tcm_sysfs_info_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	ssize_t count;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	mutex_lock(&tcm_hcd->extif_mutex);

	count = ((int (*)(struct syna_tcm_hcd *, bool))
		 tcm_hcd->identify_fn)(tcm_hcd, true);
	if (count < 0) {
		dev_err(dev, "%s error: (line %d) Failed to do identification\n",
			__func__, 0xe7);
		goto error;
	}

	count = snprintf(buf, PAGE_SIZE, "TouchComm version:  %d\n",
			 tcm_hcd->identify_info[0]);
	if (count < 0)
		goto error;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "Driver version:     %d.%d\n", 2, 1);
	if (count < 0)
		goto error;

	switch (tcm_hcd->identify_info[1]) {
	case 0x01:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      Application Firmware\n");
		break;
	case 0x02:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      Host Download Firmware\n");
		break;
	case 0x04:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      Rom Bootloader\n");
		break;
	case 0x0b:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      Bootloader\n");
		break;
	case 0x0c:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      TDDI Bootloader\n");
		break;
	case 0x0d:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      TDDI Host Download Bootloader\n");
		break;
	default:
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "Firmware mode:      Unknown (%d)\n",
				  tcm_hcd->identify_info[1]);
		break;
	}

	if (count < 0)
		goto error;

	count += snprintf(buf + count, PAGE_SIZE - count, "Part number:        ");
	if (count < 0)
		goto error;

	if (secure_memcpy(buf + count, PAGE_SIZE - count,
			  &tcm_hcd->identify_info[2], 16)) {
		dev_err(dev,
			"%s error: (line %d) Failed to copy part number string\n",
			__func__, 0x147);
		count = -EINVAL;
		goto error;
	}

	count += 16;

	count += snprintf(buf + count, PAGE_SIZE - count, "\n");
	if (count < 0)
		goto error;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "Packrat number:     %d\n", tcm_hcd->build_id);

	error:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t syna_tcm_sysfs_info_appfw_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	int retval;
	ssize_t count;
	int i;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = syna_tcm_get_app_info(tcm_hcd);
	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to get app info\n",
			__func__, 0x173);
		count = retval;
		goto exit;
	}

	count = snprintf(buf, PAGE_SIZE, "app info version:  %d\n",
			 get_unaligned_le16(&tcm_hcd->app_info[0x00]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "app info status:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x02]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "static config size:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x04]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "dynamic config size:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x06]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "app config block:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x08]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "app config size:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x0a]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "touch report config max size:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x0c]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "touch report payload max size:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x0e]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "config id:  ");
	if (count < 0)
		goto exit;

	for (i = 0; i < 16; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "0x%2x ",
				  tcm_hcd->app_info[0x10 + i]);

	count += snprintf(buf + count, PAGE_SIZE - count, "\n");
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "max x:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x20]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "max y:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x22]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "max objects:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x24]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "num cols:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x2a]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "num rows:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x28]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "num buttons:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x26]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count, "has profile:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x2c]));
	if (count < 0)
		goto exit;

	count += snprintf(buf + count, PAGE_SIZE - count,
			  "num force electrodes:  %d\n",
			  get_unaligned_le16(&tcm_hcd->app_info[0x2e]));

	exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static DEVICE_ATTR(info, 0444, syna_tcm_sysfs_info_show, syna_tcm_store_error);
static DEVICE_ATTR(info_appfw, 0444, syna_tcm_sysfs_info_appfw_show,
		   syna_tcm_store_error);
static DEVICE_ATTR(irq_en, 0220, syna_tcm_show_error,
		   syna_tcm_sysfs_irq_en_store);
static DEVICE_ATTR(reset, 0220, syna_tcm_show_error,
		   syna_tcm_sysfs_reset_store);
static DEVICE_ATTR(ts_suspend, 0664, syna_tcm_sysfs_ts_suspend_show,
		   syna_tcm_sysfs_ts_suspend_store);
static DEVICE_ATTR(input_name, 0444, syna_tcm_sysfs_input_name_show,
		   syna_tcm_store_error);

static struct attribute *attrs_ori[] = {
	&dev_attr_info.attr,
	&dev_attr_info_appfw.attr,
	&dev_attr_irq_en.attr,
	&dev_attr_reset.attr,
	&dev_attr_ts_suspend.attr,
	&dev_attr_input_name.attr,
	NULL,
};

/* sysfs group created for every bound device */
static struct attribute_group attr_group = {
	.attrs = attrs_ori,
};

static int syna_tcm_check_f35(struct syna_tcm_hcd *tcm_hcd);
static int syna_tcm_enable_irq(struct syna_tcm_hcd *tcm_hcd, bool en,
			       bool irq_flag);
static void syna_tcm_polling_work(struct work_struct *work);
static void syna_tcm_module_work(struct work_struct *work);
static int syna_tcm_identify(struct syna_tcm_hcd *tcm_hcd, bool id);
static int syna_tcm_set_gpio(struct syna_tcm_hcd *tcm_hcd, int gpio, int state,
			     int dir);
static int touch_init(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_get_regulator() - look up the optional bus and power supplies.
 * @tcm_hcd: handle to the driver context.
 * @dev: device the supplies are attached to.
 *
 * Both supplies are optional; a missing or unnamed property simply leaves the
 * matching handle NULL and nobody ever calls regulator_enable() on it.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_get_regulator(struct syna_tcm_hcd *tcm_hcd,
				  struct device *dev)
{
	int retval = 0;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	if (bdata->bus_reg_name && *bdata->bus_reg_name) {
		tcm_hcd->bus_reg = regulator_get(dev, bdata->bus_reg_name);
		if (IS_ERR(tcm_hcd->bus_reg)) {
			retval = PTR_ERR(tcm_hcd->bus_reg);
			dev_err(dev,
				"%s error: (line %d) Failed to get bus regulator\n",
				__func__, 2302);
		}
	}

	if (bdata->pwr_reg_name && *bdata->pwr_reg_name) {
		tcm_hcd->pwr_reg = regulator_get(dev, bdata->pwr_reg_name);
		if (IS_ERR(tcm_hcd->pwr_reg)) {
			retval = PTR_ERR(tcm_hcd->pwr_reg);
			dev_err(dev,
				"%s error: (line %d) Failed to get power regulator\n",
				__func__, 2313);
		}
	}

	if (retval < 0) {
		if (tcm_hcd->bus_reg) {
			regulator_put(tcm_hcd->bus_reg);
			tcm_hcd->bus_reg = NULL;
		}

		if (tcm_hcd->pwr_reg) {
			regulator_put(tcm_hcd->pwr_reg);
			tcm_hcd->pwr_reg = NULL;
		}
	}

	return retval;
}

static int syna_tcm_enable_regulator(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	struct device *dev = &tcm_hcd->pdev->dev;

	if (tcm_hcd->bus_reg) {
		retval = regulator_enable(tcm_hcd->bus_reg);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to enable bus regulator\n",
				__func__, 2258);
			return retval;
		}
	}

	if (tcm_hcd->pwr_reg) {
		retval = regulator_enable(tcm_hcd->pwr_reg);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to enable power regulator\n",
				__func__, 2267);
			regulator_disable(tcm_hcd->bus_reg);
		}
	}

	return retval;
}

static void syna_tcm_disable_regulator(struct syna_tcm_hcd *tcm_hcd)
{
	if (tcm_hcd->pwr_reg)
		regulator_disable(tcm_hcd->pwr_reg);

	if (tcm_hcd->bus_reg)
		regulator_disable(tcm_hcd->bus_reg);
}

static void syna_tcm_put_regulator(struct syna_tcm_hcd *tcm_hcd)
{
	if (tcm_hcd->bus_reg) {
		regulator_put(tcm_hcd->bus_reg);
		tcm_hcd->bus_reg = NULL;
	}

	if (tcm_hcd->pwr_reg) {
		regulator_put(tcm_hcd->pwr_reg);
		tcm_hcd->pwr_reg = NULL;
	}
}

/**
 * syna_tcm_config_gpio() - claim and drive the interrupt, power and reset pins.
 * @tcm_hcd: handle to the driver context.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_config_gpio(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	struct device *dev = &tcm_hcd->pdev->dev;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	if (bdata->irq_gpio >= 0) {
		retval = syna_tcm_set_gpio(tcm_hcd, bdata->irq_gpio, 0,
					   bdata->irq_on_state == 0);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to configure interrupt GPIO\n",
				__func__, 2193);
			goto err_free_gpio;
		}
	}

	if (bdata->power_gpio >= 0) {
		retval = syna_tcm_set_gpio(tcm_hcd, bdata->power_gpio, 1,
					   bdata->power_on_state == 0);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to configure power GPIO\n",
				__func__, 2203);
			goto err_free_gpio;
		}
	}

	if (bdata->reset_gpio >= 0) {
		retval = syna_tcm_set_gpio(tcm_hcd, bdata->reset_gpio, 1,
					   bdata->reset_on_state == 0);
		if (retval < 0) {
			dev_err(dev,
				"%s error: (line %d) Failed to configure reset GPIO\n",
				__func__, 2213);
			goto err_free_gpio;
		}
	}

	return 0;

err_free_gpio:
	if (bdata->power_gpio >= 0)
		gpio_free(bdata->power_gpio);

	if (bdata->irq_gpio >= 0)
		gpio_free(bdata->irq_gpio);

	return retval;
}

/**
 * syna_tcm_create_file_inferface() - publish the sysfs group and the
 * "touchscreen" link the Android framework looks for.
 * @dev: device to attach the nodes to.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_create_file_inferface(struct device *dev)
{
	int retval;

	retval = sysfs_create_group(&dev->kobj, &attr_group);
	if (retval) {
		dev_err(dev,
			"%s error: (line %d) [EX]: sysfs_create_group() failed!!",
			__func__, 0xeba);
		sysfs_remove_group(&dev->kobj, &attr_group);

		return -ENOMEM;
	}

	dev_err(dev,
		"%s error: (line %d) [EX]: sysfs_create_group() succeeded!!",
		__func__, 0xebe);

	retval = sysfs_create_link(NULL, &dev->kobj, "touchscreen");
	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) link touchscreen fail",
			__func__, 0xec2);
		sysfs_remove_link(NULL, "touchscreen");
	}

	return retval;
}

/**
 * syna_tcm_sensor_detection() - find out what is sitting on the bus.
 * @tcm_hcd: handle to the driver context.
 *
 * The sensor answers a plain read with an identify report.  A -ENXIO means the
 * controller did not answer at all, which is normal for a blank part that still
 * runs its ROM boot loader; the vendor payload needs to be downloaded then.
 *
 * Return: zero on success, a negative errno otherwise.
 */
static int syna_tcm_sensor_detection(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned int size;

	retval = tcm_read_message(tcm_hcd, NULL, 0);
	if (retval < 0) {
		if (retval != -ENXIO) {
			LOGE("%s error: (line %d) Failed to read TCM message\n",
			     __func__, 0xe67);
			return retval;
		}

		if (tcm_hcd->hw_if->io->type != 0x1c) {
			LOGE("%s error: (line %d) Failed to read TCM message\n",
			     __func__, 0xe67);
			return retval;
		}

		retval = syna_tcm_check_f35(tcm_hcd);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read TCM message\n",
			     __func__, 0xe59);
			return retval;
		}

		return 0;
	}

	if (tcm_hcd->report_code != 0x10) {
		LOGE("%s error: (line %d) Unexpected report code (0x%02x)\n",
		     __func__, 0xe72, tcm_hcd->report_code);
		return -ENODEV;
	}

	tcm_hcd->helper_thread = 1;

	mutex_lock(&tcm_hcd->in.buf_mutex);

	if (tcm_hcd->payload_length) {
		size = MIN(tcm_hcd->payload_length,
			   sizeof(tcm_hcd->identify_info));

		if (secure_memcpy(tcm_hcd->identify_info,
				  sizeof(tcm_hcd->identify_info),
				  tcm_hcd->in.buf + 4, size)) {
			LOGE("%s error: (line %d) Failed to copy identification info\n",
			     __func__, 0xe83);
			mutex_unlock(&tcm_hcd->in.buf_mutex);

			return -EINVAL;
		}
	}

	mutex_unlock(&tcm_hcd->in.buf_mutex);

	return 0;
}

int syna_tcm_probe(struct platform_device *pdev)
{
	int retval = 0;
	u16 read_message_count;
	u32 build_id;
	u8 firmware_mode;
	struct syna_tcm_hw_interface *hw_if;
	struct syna_tcm_board_data *bdata;
	struct syna_tcm_hcd *tcm_hcd;

	LOGE(">>>>>>>>>>>%s start!<<<<<<<<<<<\n", __func__);

	hw_if = pdev->dev.platform_data;
	if (!hw_if) {
		LOGE("%s error: (line %d) Hardware interface not found\n",
		     __func__, 0xeff);
		return -ENODEV;
	}

	bdata = hw_if->bdata;
	if (!bdata) {
		LOGE("%s error: (line %d) Board data not found\n",
		     __func__, 0xf06);
		return -ENODEV;
	}

	tcm_hcd = kmalloc(sizeof(*tcm_hcd), GFP_KERNEL);
	if (!tcm_hcd) {
		LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd\n",
		     __func__, 0xf0d);
		return -ENOMEM;
	}

	pdev->dev.driver_data = tcm_hcd;

	tcm_hcd->pdev = pdev;
	tcm_hcd->hw_if = hw_if;

	tcm_hcd->reset_fn = (void *)syna_tcm_reset;
	tcm_hcd->reset_and_reinit_fn = (void *)syna_tcm_reset_and_reinit;
	tcm_hcd->sleep_fn = (void *)syna_tcm_sleep;
	tcm_hcd->identify_fn = (void *)syna_tcm_identify;
	tcm_hcd->enable_irq_fn = (void *)syna_tcm_enable_irq;
	tcm_hcd->switch_mode_fn = (void *)syna_tcm_switch_mode;
	tcm_hcd->read_message_fn = (void *)syna_tcm_read_message;
	tcm_hcd->write_message_fn = (void *)syna_tcm_write_message;
	tcm_hcd->get_dynamic_config_fn = (void *)syna_tcm_get_dynamic_config;
	tcm_hcd->set_dynamic_config_fn = (void *)syna_tcm_set_dynamic_config;
	tcm_hcd->get_data_location_fn = (void *)syna_tcm_get_data_location;

	/* the object comes from kmalloc(), so the flag bytes need a reset */
	tcm_hcd->helper_thread = 0;
	tcm_hcd->irq_no_disable = 0;

	tcm_hcd->max_response_size = 0x100;
	tcm_hcd->read_message_count = 1;
	tcm_hcd->command_count = 9;

	if (bdata->irq_gpio >= 0)
		tcm_hcd->irq = gpiod_to_irq(gpio_to_desc(bdata->irq_gpio));
	else
		tcm_hcd->irq = bdata->irq_gpio;

	mutex_init(&tcm_hcd->extif_mutex);
	mutex_init(&tcm_hcd->reset_mutex);
	mutex_init(&tcm_hcd->irq_en_mutex);
	mutex_init(&tcm_hcd->io_ctrl_mutex);
	mutex_init(&tcm_hcd->rw_ctrl_mutex);
	mutex_init(&tcm_hcd->command_mutex);
	mutex_init(&tcm_hcd->identify_mutex);

	mutex_init(&tcm_hcd->in.buf_mutex);
	mutex_init(&tcm_hcd->out.buf_mutex);
	mutex_init(&tcm_hcd->resp.buf_mutex);
	mutex_init(&tcm_hcd->temp.buf_mutex);
	mutex_init(&tcm_hcd->config.buf_mutex);
	mutex_init(&tcm_hcd->report_buffer.buf_mutex,
		   "&tcm_hcd->report.buffer.buf_mutex");

	tcm_hcd->in.init = false;
	tcm_hcd->out.init = false;
	tcm_hcd->resp.init = true;
	tcm_hcd->temp.init = false;
	tcm_hcd->config.init = false;
	tcm_hcd->report_buffer.init = true;

	mutex_lock(&tcm_hcd->in.buf_mutex);

	retval = syna_tcm_alloc_mem(tcm_hcd, &tcm_hcd->in,
				    tcm_hcd->command_count + 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->in.buf\n",
		     __func__, 0xf4c);
		mutex_unlock(&tcm_hcd->in.buf_mutex);
		goto err_free_mem;
	}

	mutex_unlock(&tcm_hcd->in.buf_mutex);

	device_init_wakeup(&pdev->dev, 1);

	tcm_hcd->response_ready = 0;
	tcm_hcd->dispatch_state = 0;

	init_waitqueue_head(&tcm_hcd->hdl_wq);
	init_waitqueue_head(&tcm_hcd->reflash_wq);

	if (!mod_pool.init) {
		mutex_init(&mod_pool.mutex);
		INIT_LIST_HEAD(&mod_pool.list);
		mod_pool.init = 1;
	}

	retval = syna_tcm_get_regulator(tcm_hcd, &pdev->dev);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to get regulators\n",
		     __func__, 0xf67);
		goto err_init_wakeup;
	}

	retval = syna_tcm_enable_regulator(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to enable regulators\n",
		     __func__, 0xf6e);
		goto err_put_regulator;
	}

	msleep(bdata->power_delay_ms);

	retval = syna_tcm_config_gpio(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to configure GPIO's\n",
		     __func__, 0xf75);
		goto err_disable_regulator;
	}

	if (bdata->reset_gpio >= 0) {
		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
				    bdata->reset_on_state);
		msleep(bdata->reset_active_ms);
		gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
				    bdata->reset_on_state ? 0 : 1);
		msleep(bdata->reset_delay_ms);
	}

	tcm_hcd->irq_enabled = 0;
	tcm_hcd->mode = 0;

	retval = syna_tcm_sensor_detection(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to detect the sensor\n",
		     __func__, 0xf7d);
		goto err_free_gpio;
	}

	/*
	 * The sensor is awake now: the identify report tells us whether it runs
	 * its application firmware, is stuck in the ROM boot loader or needs
	 * the vendor payload first.
	 */
	firmware_mode = tcm_hcd->identify_info[0x01];
	build_id = get_unaligned_le32(&tcm_hcd->identify_info[0x12]);
	read_message_count = get_unaligned_le16(&tcm_hcd->identify_info[0x16]);

	tcm_hcd->pad12 = build_id;

	read_message_count = MIN(read_message_count, 0x100);
	tcm_hcd->read_message_count = read_message_count ? read_message_count :
				      tcm_hcd->identify_info[0x16];

	if (firmware_mode == 1) {
		tcm_hcd->mode = 1;
		dev_notice(&pdev->dev,
			   "%s info: Application mode (build id = %d)\n",
			   __func__, build_id);
	} else if (firmware_mode == 2) {
		tcm_hcd->irq_enabled = 1;
		tcm_hcd->helper_thread = 1;
		tcm_hcd->mode = 2;
		tcm_hcd->max_response_size = 0;
		dev_notice(&pdev->dev, "%s info: F35 mode\n", __func__);
	} else if (firmware_mode == 4) {
		tcm_hcd->irq_enabled = 1;
		tcm_hcd->mode = 3;
		tcm_hcd->max_response_size = 0;
		dev_notice(&pdev->dev, "%s info: RomBoot mode\n", __func__);
	} else {
		dev_warn(&pdev->dev,
			 "%s error: (line %d) TCM is detected, but mode is 0x%02x\n",
			 __func__, 0xea1, firmware_mode);
	}

	retval = syna_tcm_create_file_inferface(&pdev->dev);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create_file_inferface.\n",
		     __func__, 0xf84);
		goto err_free_gpio;
	}

	tcm_hcd->helper_wq = alloc_workqueue("%s",
					     WQ_MEM_RECLAIM | WQ_HIGHPRI, 1,
					     "syna_tcm_helper");
	if (!tcm_hcd->helper_wq) {
		LOGE("%s error: (line %d) Failed to create workqueue\n",
		     __func__, 0xf8b);
		retval = -ENOMEM;
		goto err_remove_sysfs;
	}

	tcm_hcd->polling_wq = alloc_workqueue("%s",
					      WQ_MEM_RECLAIM | WQ_HIGHPRI, 1,
					      "syna_tcm_polling");
	if (!tcm_hcd->polling_wq) {
		LOGE("%s error: (line %d) Failed to create workqueue\n",
		     __func__, 0xf95);
		retval = -ENOMEM;
		goto err_destroy_helper_wq;
	}

	INIT_WORK(&tcm_hcd->helper_work, syna_tcm_helper_work);
	INIT_DELAYED_WORK(&tcm_hcd->polling_work, syna_tcm_polling_work);

	if (tcm_hcd->irq_enabled == 0) {
		retval = syna_tcm_enable_irq(tcm_hcd, true, false);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to enable interrupt\n",
			     __func__, 0xfb7);
			goto err_destroy_polling_wq;
		}
	}

	dev_dbg(&pdev->dev, "%s info: Interrupt is registered\n", __func__);

	retval = syna_tcm_identify(tcm_hcd, false);
	if (retval < 0) {
		LOGE("%s error: (line %d) Application firmware is not running\n",
		     __func__, 0xfc1);
		goto err_disable_irq;
	}

	retval = touch_init(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to initialze touch reporting\n",
		     __func__, 0xfc8);
		goto err_disable_irq;
	}

	mod_pool.workqueue = alloc_workqueue("%s",
					     WQ_MEM_RECLAIM | WQ_HIGHPRI, 1,
					     "syna_tcm_module");
	INIT_WORK(&mod_pool.work, syna_tcm_module_work);
	mod_pool.hcd = tcm_hcd;
	mod_pool.notify = 1;

	queue_work_on(WORK_CPU_UNBOUND, mod_pool.workqueue, &mod_pool.work);

	LOGE(">>>>>>>>>>>%s end!<<<<<<<<<<<\n", __func__);

	return 0;

err_disable_irq:
	syna_tcm_enable_irq(tcm_hcd, false, false);
err_destroy_polling_wq:
	cancel_delayed_work_sync(&tcm_hcd->polling_work);
	flush_workqueue(tcm_hcd->polling_wq);
	destroy_workqueue(tcm_hcd->polling_wq);
	cancel_work_sync(&tcm_hcd->helper_work);
	flush_workqueue(tcm_hcd->helper_wq);
	destroy_workqueue(tcm_hcd->helper_wq);
err_remove_sysfs:
	sysfs_remove_link(NULL, "touchscreen");
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
err_free_gpio:
	if (bdata->irq_gpio >= 0)
		gpio_free(bdata->irq_gpio);

	if (bdata->power_gpio >= 0)
		gpio_free(bdata->power_gpio);

	if (bdata->reset_gpio >= 0)
		gpio_free(bdata->reset_gpio);
err_disable_regulator:
	syna_tcm_disable_regulator(tcm_hcd);
err_put_regulator:
	syna_tcm_put_regulator(tcm_hcd);
err_init_wakeup:
	device_init_wakeup(&pdev->dev, 0);
err_free_mem:
	if (tcm_hcd->report_buffer.init)
		kfree(tcm_hcd->report_buffer.buf);

	if (tcm_hcd->config.init)
		kfree(tcm_hcd->config.buf);

	if (tcm_hcd->temp.init)
		kfree(tcm_hcd->temp.buf);

	if (tcm_hcd->resp.init)
		kfree(tcm_hcd->resp.buf);

	if (tcm_hcd->out.init)
		kfree(tcm_hcd->out.buf);

	if (tcm_hcd->in.init)
		kfree(tcm_hcd->in.buf);

	kfree(tcm_hcd);

	return retval;
}

static void touch_remove(struct syna_tcm_hcd *tcm_hcd);

/**
 * syna_tcm_remove() - undo everything syna_tcm_probe() set up.
 * @pdev: platform device being unbound.
 *
 * Every functional module gets its handler called one last time so it can drop
 * whatever it registered, then the pool itself is torn down.  The sensor is
 * left powered down and its pins released.
 *
 * Return: zero.
 */
int syna_tcm_remove(struct platform_device *pdev)
{
	struct syna_tcm_hcd *tcm_hcd = pdev->dev.driver_data;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	struct syna_tcm_module *module, *next;

	tcm_hcd->shut_down = 1;

	cancel_work_sync(&tcm_hcd->helper_work);
	flush_workqueue(tcm_hcd->helper_wq);
	destroy_workqueue(tcm_hcd->helper_wq);

	mutex_lock(&mod_pool.mutex);

	list_for_each_entry_safe(module, next, &mod_pool.list, node) {
		if (module->desc->remove)
			module->desc->remove(tcm_hcd);

		list_del(&module->node);
		kfree(module);
	}

	mod_pool.init = 0;

	cancel_work_sync(&mod_pool.work);
	flush_workqueue(mod_pool.workqueue);
	destroy_workqueue(mod_pool.workqueue);

	mutex_unlock(&mod_pool.mutex);

	touch_remove(tcm_hcd);

	if (tcm_hcd->irq_registered && bdata->irq_gpio >= 0) {
		disable_irq(tcm_hcd->irq);
		free_irq(tcm_hcd->irq, tcm_hcd);
	}

	cancel_delayed_work_sync(&tcm_hcd->polling_work);
	flush_workqueue(tcm_hcd->polling_wq);
	destroy_workqueue(tcm_hcd->polling_wq);

	sysfs_remove_link(NULL, "touchscreen");
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);

	if (bdata->irq_gpio >= 0)
		gpio_free(bdata->irq_gpio);

	if (bdata->power_gpio >= 0)
		gpio_free(bdata->power_gpio);

	if (bdata->reset_gpio >= 0)
		gpio_free(bdata->reset_gpio);

	syna_tcm_disable_regulator(tcm_hcd);
	syna_tcm_put_regulator(tcm_hcd);

	device_init_wakeup(&pdev->dev, 0);

	if (tcm_hcd->report_buffer.init) {
		kfree(tcm_hcd->report_buffer.buf);
		tcm_hcd->report_buffer.buf = NULL;
	}

	if (tcm_hcd->config.init) {
		kfree(tcm_hcd->config.buf);
		tcm_hcd->config.buf = NULL;
	}

	if (tcm_hcd->temp.init) {
		kfree(tcm_hcd->temp.buf);
		tcm_hcd->temp.buf = NULL;
	}

	if (tcm_hcd->resp.init) {
		kfree(tcm_hcd->resp.buf);
		tcm_hcd->resp.buf = NULL;
	}

	if (tcm_hcd->out.init) {
		kfree(tcm_hcd->out.buf);
		tcm_hcd->out.buf = NULL;
	}

	if (tcm_hcd->in.init) {
		kfree(tcm_hcd->in.buf);
		tcm_hcd->in.buf = NULL;
	}

	kfree(tcm_hcd);

	return 0;
}

void syna_tcm_shutdown(struct platform_device *pdev)
{
	syna_tcm_remove(pdev);
}

/* the panel this driver was built for, taken from the kernel command line */
static char lcd_name[100];

static const struct dev_pm_ops syna_tcm_pm_ops = {
	.suspend = syna_tcm_suspend,
	.resume = syna_tcm_resume_adapter,
};

static struct platform_driver syna_tcm_driver = {
	.driver = {
		.name = "synaptics_tcm",
		.owner = THIS_MODULE,
		.pm = &syna_tcm_pm_ops,
	},
	.probe = syna_tcm_probe,
	.remove = syna_tcm_remove,
	.shutdown = syna_tcm_shutdown,
};

/**
 * syna_tcm_add_module() - register a functional module in the pool.
 * @desc: module description carrying the report id it handles.
 * @flag: true to add the module, false to only clear its detect flags.
 *
 * Return: zero on success, a negative error code otherwise.
 */
int syna_tcm_add_module(struct syna_tcm_module_desc *desc, bool flag)
{
	struct syna_tcm_module *module;

	if (!mod_pool.init) {
		mutex_init(&mod_pool.mutex);
		INIT_LIST_HEAD(&mod_pool.list);
		mod_pool.init = 1;
	}

	mutex_lock(&mod_pool.mutex);

	if (flag) {
		module = kmalloc(sizeof(*module), GFP_KERNEL);
		if (!module) {
			pr_err("%s: Failed to allocate memory for mod_handler\n",
			       __func__);
			mutex_unlock(&mod_pool.mutex);
			return -ENOMEM;
		}

		module->desc = desc;
		module->init_pending = 1;

		list_add_tail(&module->node, &mod_pool.list);
	} else {
		list_for_each_entry(module, &mod_pool.list, node) {
			if (module->desc->id == desc->id) {
				module->remove_pending = 1;
				break;
			}
		}
	}

	mutex_unlock(&mod_pool.mutex);

	if (mod_pool.notify)
		queue_work_on(WORK_CPU_UNBOUND, mod_pool.workqueue,
			      &mod_pool.work);

	return 0;
}

static int __init syna_tcm_module_init(void)
{
	struct device_node *np;
	const char *bootargs;
	char *p;
	int retval;
	int i;

	np = of_find_node_opts_by_path("/chosen", NULL);
	if (!np) {
		pr_err("Can't get the /chosen\n");
	} else if (of_property_read_string(np, "bootargs", &bootargs) < 0) {
		pr_err("Can't get the bootargs\n");
	} else {
		p = strstr(bootargs, "lcd_name");
		if (p) {
			p += 9;
			for (i = 0; i < (int)sizeof(lcd_name); i++) {
				if (*p == ' ')
					break;
				lcd_name[i] = *p++;
			}
			if (i == (int)sizeof(lcd_name)) {
				/* the vendor build trusts the command line */
				BUG();
			}
			lcd_name[i] = 0;
		}
	}

	pr_err("lcd_name %s\n", lcd_name);

	if (strncmp(lcd_name, "lcd_td4320_truly_mipi_fhd", strlen(lcd_name))) {
		pr_err("%s: match %s fail,,return\n", __func__, lcd_name);
		return 0;
	}

	pr_err(">>>>>>>>>>>%s: synaptics td4320 init start!<<<<<<<<<<<\n",
	       __func__);

	retval = syna_tcm_bus_init();
	if (retval < 0)
		return retval;

	retval = platform_driver_register(&syna_tcm_driver);
	if (retval < 0) {
		pr_err("%s: platform_driver_register failed with: %d",
		       __func__, retval);
		return retval;
	}

	pr_err("%s: platform_driver_register success with: %d\n",
	       __func__, retval);

	retval = syna_tcm_add_module(&zeroflash_module, true);
	if (retval < 0) {
		pr_err("%s: zeroflash_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	retval = syna_tcm_add_module(&reflash_module, true);
	if (retval < 0) {
		pr_err("%s: reflash_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	retval = syna_tcm_add_module(&device_module, true);
	if (retval < 0) {
		pr_err("%s: device_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	retval = syna_tcm_add_module(&testing_module, true);
	if (retval < 0) {
		pr_err("%s: testing_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	retval = syna_tcm_add_module(&recovery_module, true);
	if (retval < 0) {
		pr_err("%s: recovery_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	retval = syna_tcm_add_module(&diag_module, true);
	if (retval < 0) {
		pr_err("%s: diag_module_init failed with: %d\n",
		       __func__, retval);
		return retval;
	}

	pr_err(">>>>>>>>>>>%s: synaptics td4320 init success!<<<<<<<<<<<\n",
	       __func__);

	return 0;
}

static void __exit syna_tcm_module_exit(void)
{
	platform_driver_unregister(&syna_tcm_driver);
	syna_tcm_bus_exit();
}

module_init(syna_tcm_module_init);
module_exit(syna_tcm_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics TCM Device Module");
MODULE_LICENSE("GPL v2");
