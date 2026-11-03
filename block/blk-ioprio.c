// SPDX-License-Identifier: GPL-2.0
/*
 * cgroup policy for block IO priority.
 *
 * Restored from the UMS9157 device kernel.  The Image carries the whole
 * machinery (blk_ioprio_init() at 0x88e2a4, the rq_qos hooks at
 * 0x88e454/0x88e4dc, and the cgroup file called "prio.class"), so this is
 * not a re-implementation -- it is the same thing written back out.
 *
 * The policy is applied in ->track(), i.e. after a bio has been assigned to
 * a request and charged to a blkcg: the cgroup's policy rewrites
 * bio->bi_ioprio to the floor it allows, so a task cannot ask for a better
 * IO class than its cgroup permits.
 */
#include <linux/blk-cgroup.h>
#include <linux/blkdev.h>
#include <linux/ioprio.h>
#include <linux/kernfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "blk.h"
#include "blk-rq-qos.h"

enum prio_policy {
	PRIO_POLICY_NONE = 0,
	PRIO_POLICY_NONE_TO_RT = 1,
	PRIO_POLICY_RESTRICT_TO_BE = 2,
	PRIO_POLICY_IDLE = 3,
};

static const char *policy_name[] = {
	[PRIO_POLICY_NONE] = "no-change",
	[PRIO_POLICY_NONE_TO_RT] = "none-to-rt",
	[PRIO_POLICY_RESTRICT_TO_BE] = "restrict-to-be",
	[PRIO_POLICY_IDLE] = "idle",
};

struct ioprio_blkcg {
	struct blkcg_policy_data cpd;	/* must be the first member */
	enum prio_policy prio_policy;
};

struct ioprio_blkg {
	struct blkg_policy_data pd;	/* must be the first member */
};

static struct blkcg_policy ioprio_policy;

static struct ioprio_blkcg *blkcg_to_ioprio_blkcg(struct blkcg *blkcg)
{
	return container_of(blkcg_to_cpd(blkcg, &ioprio_policy),
			    struct ioprio_blkcg, cpd);
}

static struct ioprio_blkcg *css_to_ioprio_blkcg(struct cgroup_subsys_state *css)
{
	return blkcg_to_ioprio_blkcg(css_to_blkcg(css));
}

static struct ioprio_blkg *pd_to_ioprio_blkg(struct blkg_policy_data *pd)
{
	return container_of(pd, struct ioprio_blkg, pd);
}

static int ioprio_show_prio_policy(struct seq_file *sf, void *v)
{
	struct ioprio_blkcg *blkcg_ioprio = css_to_ioprio_blkcg(seq_css(sf));

	seq_printf(sf, "%s\n", policy_name[blkcg_ioprio->prio_policy]);
	return 0;
}

static ssize_t ioprio_set_prio_policy(struct kernfs_open_file *of, char *buf,
				      size_t nbytes, loff_t off)
{
	struct ioprio_blkcg *blkcg_ioprio = css_to_ioprio_blkcg(of_css(of));

	if (off != 0)
		return -EIO;

	if (sysfs_streq(buf, "no-change"))
		blkcg_ioprio->prio_policy = PRIO_POLICY_NONE;
	else if (sysfs_streq(buf, "none-to-rt"))
		blkcg_ioprio->prio_policy = PRIO_POLICY_NONE_TO_RT;
	else if (sysfs_streq(buf, "restrict-to-be"))
		blkcg_ioprio->prio_policy = PRIO_POLICY_RESTRICT_TO_BE;
	else if (sysfs_streq(buf, "idle"))
		blkcg_ioprio->prio_policy = PRIO_POLICY_IDLE;
	else
		return -EINVAL;

	return nbytes;
}

static struct cftype ioprio_files[] = {
	{
		.name		= "prio.class",
		.write		= ioprio_set_prio_policy,
		.seq_show	= ioprio_show_prio_policy,
	},
	{ }	/* terminate */
};

static struct cftype ioprio_legacy_files[] = {
	{
		.name		= "prio.class",
		.write		= ioprio_set_prio_policy,
		.seq_show	= ioprio_show_prio_policy,
	},
	{ }	/* terminate */
};

static struct blkcg_policy_data *ioprio_alloc_cpd(gfp_t gfp)
{
	struct ioprio_blkcg *blkcg;

	blkcg = kzalloc(sizeof(*blkcg), gfp);
	if (!blkcg)
		return NULL;
	blkcg->prio_policy = PRIO_POLICY_NONE;
	return &blkcg->cpd;
}

static void ioprio_free_cpd(struct blkcg_policy_data *cpd)
{
	kfree(container_of(cpd, struct ioprio_blkcg, cpd));
}

static struct blkg_policy_data *ioprio_alloc_pd(gfp_t gfp,
						struct request_queue *q)
{
	struct ioprio_blkg *blkg;

	blkg = kzalloc(sizeof(*blkg), gfp);
	if (!blkg)
		return NULL;
	return &blkg->pd;
}

static void ioprio_free_pd(struct blkg_policy_data *pd)
{
	kfree(pd_to_ioprio_blkg(pd));
}

/*
 * The device kernel dereferences bio->bi_blkg, blkg->pd[plid] and
 * blkcg->cpd[plid] with no NULL test at all (all three `cbz` in the
 * disassembly land on `mov x8, xzr` and then load straight through x8).
 * We keep the tests: they cost three branches and turn a policy run on a
 * bio that was never charged from a panic into a no-op.
 */
static void blkcg_ioprio_track(struct rq_qos *rqos, struct request *rq,
			       struct bio *bio)
{
	struct ioprio_blkcg *blkcg_ioprio;
	struct blkcg *blkcg;
	u16 prio;

	blkcg = bio_blkcg(bio);
	if (!blkcg)
		return;
	blkcg_ioprio = blkcg_to_ioprio_blkcg(blkcg);
	if (!blkcg_ioprio)
		return;

	/*
	 * IOPRIO_PRIO_VALUE(policy, 0): policy 0 asks for the lowest class,
	 * so "no-change" never lowers anything; the other three raise the
	 * floor to RT / BE / IDLE respectively.
	 */
	prio = IOPRIO_PRIO_VALUE(blkcg_ioprio->prio_policy, 0);
	if (prio > bio->bi_ioprio) {
		bio->bi_ioprio = prio;
		rq->rq_flags |= RQF_ELV;
	}
}

static void blkcg_ioprio_exit(struct rq_qos *rqos)
{
	blkcg_deactivate_policy(rqos->q, &ioprio_policy);
	kfree(rqos);
}

static struct rq_qos_ops blkcg_ioprio_ops = {
	.track = blkcg_ioprio_track,
	.exit = blkcg_ioprio_exit,
};

int blk_ioprio_init(struct request_queue *q)
{
	struct rq_qos *rqos;
	int ret;

	rqos = kzalloc(sizeof(*rqos), GFP_KERNEL);
	if (!rqos)
		return -ENOMEM;
	rqos->ops = &blkcg_ioprio_ops;
	rqos->q = q;
	rqos->id = RQ_QOS_IOPRIO;

	ret = blkcg_activate_policy(q, &ioprio_policy);
	if (ret) {
		kfree(rqos);
		return ret;
	}

	rq_qos_add(q, rqos);
	return 0;
}

static struct blkcg_policy ioprio_policy = {
	.dfl_cftypes	= ioprio_files,
	.legacy_cftypes	= ioprio_legacy_files,

	.cpd_alloc_fn	= ioprio_alloc_cpd,
	.cpd_free_fn	= ioprio_free_cpd,

	.pd_alloc_fn	= ioprio_alloc_pd,
	.pd_free_fn	= ioprio_free_pd,
};

static int __init ioprio_init(void)
{
	return blkcg_policy_register(&ioprio_policy);
}

static void __exit ioprio_exit(void)
{
	blkcg_policy_unregister(&ioprio_policy);
}
module_init(ioprio_init);
module_exit(ioprio_exit);
