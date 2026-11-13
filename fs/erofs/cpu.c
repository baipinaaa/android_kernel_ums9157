// SPDX-License-Identifier: GPL-2.0-only
/*
 * Per-CPU decompression workers for EROFS.
 *
 * Restored from the device kernel (UMS9157, boot_a.bin).  The Image carries
 * z_erofs_pcpu_workers / z_erofs_pcpu_worker_lock / erofs_cpuhp_state, and
 * erofs_cpu_online() (0xffffffc01082b1fc, 177 insns) creates one kthread per
 * CPU via kthread_create_worker_on_cpu(cpu, 0, "erofs_worker/%u", cpu) and
 * then raises it to SCHED_FIFO.  erofs_cpu_offline() (147 insns) clears the
 * slot and destroys the worker.
 */
#include "internal.h"
#include <linux/cpuhotplug.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>

static DEFINE_SPINLOCK(z_erofs_pcpu_worker_lock);
static struct kthread_worker *z_erofs_pcpu_workers[NR_CPUS];
static enum cpuhp_state erofs_cpuhp_state;

static int erofs_cpu_online(unsigned int cpu)
{
	struct kthread_worker *worker;

	worker = kthread_create_worker_on_cpu(cpu, 0, "erofs_worker/%u", cpu);
	if (IS_ERR(worker))
		return PTR_ERR(worker);

#ifdef CONFIG_EROFS_FS_PCPU_KTHREAD_HIPRI
	{
		struct sched_param sp = { .sched_priority = MAX_RT_PRIO / 2 };

		sched_setscheduler_nocheck(worker->task, SCHED_FIFO, &sp);
	}
#endif

	spin_lock(&z_erofs_pcpu_worker_lock);
	if (z_erofs_pcpu_workers[cpu]) {
		spin_unlock(&z_erofs_pcpu_worker_lock);
		kthread_destroy_worker(worker);
		return 0;
	}
	rcu_assign_pointer(z_erofs_pcpu_workers[cpu], worker);
	spin_unlock(&z_erofs_pcpu_worker_lock);
	return 0;
}

static int erofs_cpu_offline(unsigned int cpu)
{
	struct kthread_worker *worker;

	spin_lock(&z_erofs_pcpu_worker_lock);
	worker = z_erofs_pcpu_workers[cpu];
	rcu_assign_pointer(z_erofs_pcpu_workers[cpu], NULL);
	spin_unlock(&z_erofs_pcpu_worker_lock);
	if (worker)
		kthread_destroy_worker(worker);
	return 0;
}

int z_erofs_init_cpuhp(void)
{
	int err;

	/* 必须用 cpuhp_setup_state（invoke=false）：设备在
	 * z_erofs_init_zip_subsystem 里调用 __cpuhp_setup_state 时 w2=wzr，
	 * 即会遍历已在线 CPU 逐个调 startup。之前误用 _nocalls（invoke=true），
	 * 开机时 8 个 CPU 全部已在线，于是 erofs_cpu_online() 一次都没被调用，
	 * 运行时刻 erofs_worker/N 线程数为 0（实测），per-CPU 快路径永远用不上。
	 */
	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"fs/erofs:online",
				erofs_cpu_online, erofs_cpu_offline);
	if (err < 0)
		return err;
	erofs_cpuhp_state = err;
	return 0;
}

void z_erofs_exit_cpuhp(void)
{
	if (erofs_cpuhp_state)
		cpuhp_remove_state(erofs_cpuhp_state);
}

bool z_erofs_queue_pcpu_work(struct kthread_work *work)
{
	struct kthread_worker *worker;
	bool queued = false;

	rcu_read_lock();
	worker = rcu_dereference(z_erofs_pcpu_workers[smp_processor_id()]);
	if (worker) {
		kthread_queue_work(worker, work);
		queued = true;
	}
	rcu_read_unlock();
	return queued;
}
