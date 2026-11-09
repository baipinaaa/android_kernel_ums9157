/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ENERGY_MODEL_H
#define _LINUX_ENERGY_MODEL_H
#include <linux/cpumask.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/rcupdate.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/topology.h>
#include <linux/types.h>

#ifdef CONFIG_ENERGY_MODEL

struct em_cap_state {
	unsigned long frequency;
	unsigned long power;
	unsigned long cost;
};


struct em_perf_domain {
	struct em_cap_state *table;
	int nr_cap_states;
	unsigned long cpus[0];
};

#define EM_CPU_MAX_POWER 0xFFFF

#ifdef CONFIG_64BIT
#define em_scale_power(p) ((p) * 1000)
#else
#define em_scale_power(p) (p)
#endif


#ifdef CONFIG_64BIT
#define em_scale_power(p) ((p) * 1000)
#else
#define em_scale_power(p) (p)
#endif

struct em_data_callback {
	
	int (*active_power)(unsigned long *power, unsigned long *freq, int cpu);
};
#define EM_DATA_CB(_active_power_cb) { .active_power = &_active_power_cb }

struct em_perf_domain *em_cpu_get(int cpu);
int em_register_perf_domain(cpumask_t *span, unsigned int nr_states,
						struct em_data_callback *cb);


static inline unsigned long em_pd_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util)
{
	unsigned long freq, scale_cpu;
	struct em_cap_state *cs;
	int i, cpu;

	
	cpu = cpumask_first(to_cpumask(pd->cpus));
	scale_cpu = arch_scale_cpu_capacity(cpu);
	cs = &pd->table[pd->nr_cap_states - 1];
	freq = map_util_freq(max_util, cs->frequency, scale_cpu);

	
	for (i = 0; i < pd->nr_cap_states; i++) {
		cs = &pd->table[i];
		if (cs->frequency >= freq)
			break;
	}

	
	return cs->cost * sum_util / scale_cpu;
}


static inline int em_pd_nr_cap_states(struct em_perf_domain *pd)
{
	return pd->nr_cap_states;
}

#else
struct em_perf_domain {};
struct em_data_callback {};
#define EM_DATA_CB(_active_power_cb) { }

static inline int em_register_perf_domain(cpumask_t *span,
			unsigned int nr_states, struct em_data_callback *cb)
{
	return -EINVAL;
}
static inline struct em_perf_domain *em_cpu_get(int cpu)
{
	return NULL;
}
static inline unsigned long em_pd_energy(struct em_perf_domain *pd,
			unsigned long max_util, unsigned long sum_util)
{
	return 0;
}
static inline int em_pd_nr_cap_states(struct em_perf_domain *pd)
{
	return 0;
}
#endif

#endif
