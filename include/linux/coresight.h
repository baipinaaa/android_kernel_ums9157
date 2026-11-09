/* SPDX-License-Identifier: GPL-2.0 */


#ifndef _LINUX_CORESIGHT_H
#define _LINUX_CORESIGHT_H

#include <linux/device.h>
#include <linux/perf_event.h>
#include <linux/sched.h>


#define CORESIGHT_PERIPHIDR4	0xfd0
#define CORESIGHT_PERIPHIDR5	0xfd4
#define CORESIGHT_PERIPHIDR6	0xfd8
#define CORESIGHT_PERIPHIDR7	0xfdC
#define CORESIGHT_PERIPHIDR0	0xfe0
#define CORESIGHT_PERIPHIDR1	0xfe4
#define CORESIGHT_PERIPHIDR2	0xfe8
#define CORESIGHT_PERIPHIDR3	0xfeC

#define CORESIGHT_COMPIDR0	0xff0
#define CORESIGHT_COMPIDR1	0xff4
#define CORESIGHT_COMPIDR2	0xff8
#define CORESIGHT_COMPIDR3	0xffC

#define ETM_ARCH_V3_3		0x23
#define ETM_ARCH_V3_5		0x25
#define PFT_ARCH_V1_0		0x30
#define PFT_ARCH_V1_1		0x31

#define CORESIGHT_UNLOCK	0xc5acce55

extern struct bus_type coresight_bustype;

enum coresight_dev_type {
	CORESIGHT_DEV_TYPE_NONE,
	CORESIGHT_DEV_TYPE_SINK,
	CORESIGHT_DEV_TYPE_LINK,
	CORESIGHT_DEV_TYPE_LINKSINK,
	CORESIGHT_DEV_TYPE_SOURCE,
	CORESIGHT_DEV_TYPE_HELPER,
};

enum coresight_dev_subtype_sink {
	CORESIGHT_DEV_SUBTYPE_SINK_NONE,
	CORESIGHT_DEV_SUBTYPE_SINK_PORT,
	CORESIGHT_DEV_SUBTYPE_SINK_BUFFER,
};

enum coresight_dev_subtype_link {
	CORESIGHT_DEV_SUBTYPE_LINK_NONE,
	CORESIGHT_DEV_SUBTYPE_LINK_MERG,
	CORESIGHT_DEV_SUBTYPE_LINK_SPLIT,
	CORESIGHT_DEV_SUBTYPE_LINK_FIFO,
};

enum coresight_dev_subtype_source {
	CORESIGHT_DEV_SUBTYPE_SOURCE_NONE,
	CORESIGHT_DEV_SUBTYPE_SOURCE_PROC,
	CORESIGHT_DEV_SUBTYPE_SOURCE_BUS,
	CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE,
};

enum coresight_dev_subtype_helper {
	CORESIGHT_DEV_SUBTYPE_HELPER_NONE,
	CORESIGHT_DEV_SUBTYPE_HELPER_CATU,
};


union coresight_dev_subtype {
	
	struct {
		enum coresight_dev_subtype_sink sink_subtype;
		enum coresight_dev_subtype_link link_subtype;
	};
	enum coresight_dev_subtype_source source_subtype;
	enum coresight_dev_subtype_helper helper_subtype;
};


struct coresight_platform_data {
	int nr_inport;
	int nr_outport;
	struct coresight_connection *conns;
};


struct coresight_desc {
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct coresight_platform_data *pdata;
	struct device *dev;
	const struct attribute_group **groups;
	const char *name;
};


struct coresight_connection {
	int outport;
	int child_port;
	struct fwnode_handle *child_fwnode;
	struct coresight_device *child_dev;
};


struct coresight_device {
	struct coresight_platform_data *pdata;
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct device dev;
	atomic_t *refcnt;
	bool orphan;
	bool enable;	
	
	bool activated;	
	struct dev_ext_attribute *ea;
};


struct coresight_dev_list {
	int			nr_idx;
	const char		*pfx;
	struct fwnode_handle	**fwnode_list;
};

#define DEFINE_CORESIGHT_DEVLIST(var, dev_pfx)				\
static struct coresight_dev_list (var) = {				\
						.pfx = dev_pfx,		\
						.nr_idx = 0,		\
						.fwnode_list = NULL,	\
}

#define to_coresight_device(d) container_of(d, struct coresight_device, dev)

#define source_ops(csdev)	csdev->ops->source_ops
#define sink_ops(csdev)		csdev->ops->sink_ops
#define link_ops(csdev)		csdev->ops->link_ops
#define helper_ops(csdev)	csdev->ops->helper_ops


struct coresight_ops_sink {
	int (*enable)(struct coresight_device *csdev, u32 mode, void *data);
	int (*disable)(struct coresight_device *csdev);
	void *(*alloc_buffer)(struct coresight_device *csdev,
			      struct perf_event *event, void **pages,
			      int nr_pages, bool overwrite);
	void (*free_buffer)(void *config);
	unsigned long (*update_buffer)(struct coresight_device *csdev,
			      struct perf_output_handle *handle,
			      void *sink_config);
};


struct coresight_ops_link {
	int (*enable)(struct coresight_device *csdev, int iport, int oport);
	void (*disable)(struct coresight_device *csdev, int iport, int oport);
};


struct coresight_ops_source {
	int (*cpu_id)(struct coresight_device *csdev);
	int (*trace_id)(struct coresight_device *csdev);
	int (*enable)(struct coresight_device *csdev,
		      struct perf_event *event,  u32 mode);
	void (*disable)(struct coresight_device *csdev,
			struct perf_event *event);
};


struct coresight_ops_helper {
	int (*enable)(struct coresight_device *csdev, void *data);
	int (*disable)(struct coresight_device *csdev, void *data);
};

struct coresight_ops {
	const struct coresight_ops_sink *sink_ops;
	const struct coresight_ops_link *link_ops;
	const struct coresight_ops_source *source_ops;
	const struct coresight_ops_helper *helper_ops;
};

#ifdef CONFIG_CORESIGHT
extern struct coresight_device *
coresight_register(struct coresight_desc *desc);
extern void coresight_unregister(struct coresight_device *csdev);
extern int coresight_enable(struct coresight_device *csdev);
extern void coresight_disable(struct coresight_device *csdev);
extern int coresight_timeout(void __iomem *addr, u32 offset,
			     int position, int value);

extern int coresight_claim_device(void __iomem *base);
extern int coresight_claim_device_unlocked(void __iomem *base);

extern void coresight_disclaim_device(void __iomem *base);
extern void coresight_disclaim_device_unlocked(void __iomem *base);
extern char *coresight_alloc_device_name(struct coresight_dev_list *devs,
					 struct device *dev);
#else
static inline struct coresight_device *
coresight_register(struct coresight_desc *desc) { return NULL; }
static inline void coresight_unregister(struct coresight_device *csdev) {}
static inline int
coresight_enable(struct coresight_device *csdev) { return -ENOSYS; }
static inline void coresight_disable(struct coresight_device *csdev) {}
static inline int coresight_timeout(void __iomem *addr, u32 offset,
				     int position, int value) { return 1; }
static inline int coresight_claim_device_unlocked(void __iomem *base)
{
	return -EINVAL;
}

static inline int coresight_claim_device(void __iomem *base)
{
	return -EINVAL;
}

static inline void coresight_disclaim_device(void __iomem *base) {}
static inline void coresight_disclaim_device_unlocked(void __iomem *base) {}

#endif

extern int coresight_get_cpu(struct device *dev);

struct coresight_platform_data *coresight_get_platform_data(struct device *dev);

extern struct device *of_coresight_get_device_by_node(struct device_node *endpoint);

int coresight_enable_sink_show_export(struct coresight_device *csdev);
int coresight_enable_sink_store_export(struct coresight_device *csdev, int val);
int coresight_enable_source_show_export(struct coresight_device *csdev);
int coresight_enable_source_store_export(struct coresight_device *csdev, int val);

int tmc_enable_sink_show(struct device *dev);
int tmc_enable_sink_store(struct device *dev, int val);
int etm4_enable_source_show(struct device *dev);
int etm4_enable_source_store(struct device *dev, int val);

#endif
