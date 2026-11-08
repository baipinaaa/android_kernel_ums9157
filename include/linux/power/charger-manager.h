/* SPDX-License-Identifier: GPL-2.0-only */


#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/alarmtimer.h>
#include <linux/extcon.h>
#include <linux/power_supply.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/power/sprd_vote.h>
#include <linux/power/sprd_vchg_detect.h>
#include <linux/power/sprd_fchg_extcon.h>

enum cm_charge_info_cmd {
	CM_CHARGE_INFO_CHARGE_LIMIT = BIT(0),
	CM_CHARGE_INFO_INPUT_LIMIT = BIT(1),
	CM_CHARGE_INFO_THERMAL_LIMIT = BIT(2),
	CM_CHARGE_INFO_JEITA_LIMIT = BIT(3),
};

enum cm_charger_type_flag {
	CM_USB_TYPE = 0,
	CM_FCHG_TYPE,
	CM_WL_TYPE,
};

enum cm_charger_type {
	CM_CHARGER_TYPE_UNKNOWN = 0,
	CM_CHARGER_TYPE_SDP,
	CM_CHARGER_TYPE_DCP,
	CM_CHARGER_TYPE_CDP,
	CM_CHARGER_TYPE_FAST,
	CM_CHARGER_TYPE_ADAPTIVE,
	CM_WIRELESS_CHARGER_TYPE_BPP,
	CM_WIRELESS_CHARGER_TYPE_EPP,
};

enum power_supply_wireless_charger_type {
	POWER_SUPPLY_WIRELESS_CHARGER_TYPE_UNKNOWN = 0x20,
	POWER_SUPPLY_WIRELESS_CHARGER_TYPE_BPP,		
	POWER_SUPPLY_WIRELESS_CHARGER_TYPE_EPP,		
};

enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum cm_event_types {
	CM_EVENT_UNKNOWN = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_BATT_OVERHEAT,
	CM_EVENT_BATT_COLD,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_WL_CHG_START_STOP,
	CM_EVENT_FAST_CHARGE,
	CM_EVENT_INT,
	CM_EVENT_BATT_OVERVOLTAGE,
	CM_EVENT_OTHERS,
};

enum cm_capacity_cmd {
	CM_CAPACITY = 0,
	CM_BOOT_CAPACITY,
};

enum cm_ir_comp_state {
	CM_IR_COMP_STATE_UNKNOWN,
	CM_IR_COMP_STATE_NORMAL,
	CM_IR_COMP_STATE_CP,
};

enum cm_cp_state {
	CM_CP_STATE_UNKNOWN,
	CM_CP_STATE_RECOVERY,
	CM_CP_STATE_ENTRY,
	CM_CP_STATE_CHECK_VBUS,
	CM_CP_STATE_TUNE,
	CM_CP_STATE_EXIT,
};

enum cm_charge_status {
	CM_CHARGE_TEMP_OVERHEAT = BIT(0),
	CM_CHARGE_TEMP_COLD = BIT(1),
	CM_CHARGE_VOLTAGE_ABNORMAL = BIT(2),
	CM_CHARGE_HEALTH_ABNORMAL = BIT(3),
	CM_CHARGE_DURATION_ABNORMAL = BIT(4),
	CM_CHARGE_BATT_OVERVOLTAGE = BIT(5),
};

enum cm_fast_charge_command {
	CM_FAST_CHARGE_NORMAL_CMD = 1,
	CM_FAST_CHARGE_OVP_ENABLE_CMD,
	CM_FAST_CHARGE_OVP_DISABLE_CMD,
	CM_PPS_CHARGE_ENABLE_CMD,
	CM_PPS_CHARGE_DISABLE_CMD,
	CM_POWER_PATH_ENABLE_CMD,
	CM_POWER_PATH_DISABLE_CMD,
};

enum cm_present_command {
	CM_USB_PRESENT_CMD,
	CM_BATTERY_PRESENT_CMD,
	CM_VBUS_PRESENT_CMD,
};

enum cm_temperature_command {
	CMD_BATT_TEMP_CMD,
	CM_BUS_TEMP_CMD,
	CM_DIE_TEMP_CMD,
};

enum cm_health_command {
	CM_FAULT_HEALTH_CMD,
	CM_ALARM_HEALTH_CMD,
	CM_SOFT_ALARM_HEALTH_CMD,
	CM_BUS_ERR_HEALTH_CMD,
	CM_GOOD_HEALTH_CMD = 0x7f7f7f7f,
};

enum cm_current_now_command {
	CM_IBAT_CURRENT_NOW_CMD,
	CM_IBUS_CURRENT_NOW_CMD,
};

enum power_supply_wireless_type {
	POWER_SUPPLY_WIRELESS_TYPE_UNKNOWN = 0x20,
	POWER_SUPPLY_WIRELESS_TYPE_BPP,		
	POWER_SUPPLY_WIRELESS_TYPE_EPP,		
};

enum power_supply_charge_type {
	USB_CHARGE_TYPE_NORMAL = 0,		
	USB_CHARGE_TYPE_FAST,			
	USB_CHARGE_TYPE_FLASH,			
	USB_CHARGE_TYPE_TURBE,			
	USB_CHARGE_TYPE_SUPER,			
	WIRELESS_CHARGE_TYPE_NORMAL,
	WIRELESS_CHARGE_TYPE_FAST,
	WIRELESS_CHARGE_TYPE_FLASH,
	CHARGE_MAX,
};

enum cm_charger_fault_status_mask {
	CM_CHARGER_BAT_OVP_FAULT_MASK = BIT(0),
	CM_CHARGER_BAT_OCP_FAULT_MASK = BIT(1),
	CM_CHARGER_BUS_OVP_FAULT_MASK = BIT(2),
	CM_CHARGER_BUS_OCP_FAULT_MASK = BIT(3),
	CM_CHARGER_BAT_THERM_FAULT_MASK = BIT(4),
	CM_CHARGER_BUS_THERM_FAULT_MASK = BIT(5),
	CM_CHARGER_DIE_THERM_FAULT_MASK = BIT(6),
	CM_CHARGER_BAT_OVP_ALARM_MASK = BIT(8),
	CM_CHARGER_BAT_OCP_ALARM_MASK = BIT(9),
	CM_CHARGER_BUS_OVP_ALARM_MASK = BIT(10),
	CM_CHARGER_BUS_OCP_ALARM_MASK = BIT(11),
	CM_CHARGER_BAT_THERM_ALARM_MASK = BIT(12),
	CM_CHARGER_BUS_THERM_ALARM_MASK = BIT(13),
	CM_CHARGER_DIE_THERM_ALARM_MASK = BIT(14),
	CM_CHARGER_BAT_UCP_ALARM_MASK = BIT(15),
	CM_CHARGER_BUS_ERR_LO_MASK = BIT(24),
	CM_CHARGER_BUS_ERR_HI_MASK = BIT(25),
};

enum cm_charger_fault_status_shift {
	CM_CHARGER_BAT_OVP_FAULT_SHIFT = 0,
	CM_CHARGER_BAT_OCP_FAULT_SHIFT = 1,
	CM_CHARGER_BUS_OVP_FAULT_SHIFT = 2,
	CM_CHARGER_BUS_OCP_FAULT_SHIFT = 3,
	CM_CHARGER_BAT_THERM_FAULT_SHIFT = 4,
	CM_CHARGER_BUS_THERM_FAULT_SHIFT = 5,
	CM_CHARGER_DIE_THERM_FAULT_SHIFT = 6,
	CM_CHARGER_BAT_OVP_ALARM_SHIFT = 8,
	CM_CHARGER_BAT_OCP_ALARM_SHIFT = 9,
	CM_CHARGER_BUS_OVP_ALARM_SHIFT = 10,
	CM_CHARGER_BUS_OCP_ALARM_SHIFT = 11,
	CM_CHARGER_BAT_THERM_ALARM_SHIFT = 12,
	CM_CHARGER_BUS_THERM_ALARM_SHIFT = 13,
	CM_CHARGER_DIE_THERM_ALARM_SHIFT = 14,
	CM_CHARGER_BAT_UCP_ALARM_SHIFT = 15,
	CM_CHARGER_BUS_ERR_LO_SHIFT = 24,
	CM_CHARGER_BUS_ERR_HI_SHIFT = 25,
};


enum uvlo_shutdown_modes {
	CM_SHUTDOWN_MODE_ORDERLY = 0,
	CM_SHUTDOWN_MODE_KERNEL,
	CM_SHUTDOWN_MODE_ANDROID,
};

#define CM_IBAT_BUFF_CNT 7

struct cm_power_supply_data {
	struct power_supply_desc psd;
	struct power_supply *psy;
	struct charger_manager *cm;
	int ONLINE;
};

struct charger_type {
	int psy_type;
	enum cm_charger_type adap_type;
};


struct charger_sysfs_ctl_item {
	
	int externally_control;

	int cp_id;

	struct attribute_group attr_grp;
	struct device_attribute attr_stop_charge;
	struct device_attribute attr_externally_control;
	struct device_attribute attr_jeita_control;
	struct device_attribute attr_cp_num;
	struct device_attribute attr_charge_pump_present;
	struct device_attribute attr_charge_pump_current;
	struct device_attribute attr_enable_power_path;
	struct device_attribute attr_keep_awake;
	struct device_attribute attr_support_fast_charge;
	struct attribute *attrs[10];

	struct charger_manager *cm;
};


struct cap_remap_table {
	int cnt;
	int lcap;
	int hcap;
	int lb;
	int hb;
};


struct cm_ir_compensation {
	int us;
	int rc;
	int ibat_buf[CM_IBAT_BUFF_CNT];
	int us_upper_limit;
	int cp_upper_limit_offset;
	int us_lower_limit;
	bool ir_compensation_en;
	int ibat_index;
	int last_target_cccv;
};


struct cm_fault_status {
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;
	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;
	bool vbus_error_lo;
	bool vbus_error_hi;
};


struct cm_alarm_status {
	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;
	bool bat_ucp_alarm;
	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;
};


struct cm_charge_pump_status {
	bool cp_running;
	bool check_cp_threshold;
	bool recovery;
	int cp_state;
	int cp_target_ibat;
	int cp_target_vbat;
	int cp_target_ibus;
	int cp_target_vbus;
	int cp_last_target_vbus;
	int cp_max_ibat;
	int cp_max_ibus;
	int adapter_max_ibus;
	int adapter_max_vbus;
	int vbat_uV;
	int ibat_uA;
	int ibus_uA;
	int vbus_uV;
	int tune_vbus_retry;
	int cp_taper_trigger_cnt;
	int cp_adjust_cnt;
	int cp_ibat_ucp_cnt;
	int cp_taper_current;
	bool cp_soft_alarm_event;
	bool cp_fault_event;
	bool cp_state_tune_log;

	struct cm_fault_status  flt;
	struct cm_alarm_status  alm;
};

struct cm_charge_current {
	int sdp_limit;
	int sdp_cur;
	int dcp_limit;
	int dcp_cur;
	int cdp_limit;
	int cdp_cur;
	int unknown_limit;
	int unknown_cur;
	int fchg_limit;
	int fchg_cur;
	int flash_limit;
	int flash_cur;
	int wl_bpp_cur;
	int wl_bpp_limit;
	int wl_epp_cur;
	int wl_epp_limit;
};


struct cm_thermal_info {
	u32 adapter_default_charge_vol;
	u32 thm_pwr;
	int thm_adjust_cur;
	bool need_calib_charge_lmt;
};

struct cm_jeita_info {
	bool jeita_changed;
	int jeita_status;
	int jeita_temperature;
	int temp_up_trigger;
	int temp_down_trigger;
};


struct charger_desc {
	const char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	unsigned int fullbatt_vchkdrop_ms;
	unsigned int fullbatt_vchkdrop_uV;
	unsigned int fullbatt_uV;
	unsigned int fullbatt_uA;
	unsigned int first_fullbatt_uA;
	unsigned int fullbatt_soc;
	unsigned int fullbatt_full_capacity;
	unsigned int constant_charge_voltage_max_uv;
	unsigned int fullbatt_voltage_offset_uv;

	enum data_source battery_present;

	const char **psy_charger_stat;
	const char **psy_alt_cp_adpt_stat;
	const char **psy_cp_stat;
	const char **psy_wl_charger_stat;
	const char **psy_cp_converter_stat;

	int num_sysfs;
	struct charger_sysfs_ctl_item *sysfs;
	const struct attribute_group **sysfs_groups;

	const char *psy_fuel_gauge;

	const char *thermal_zone;

	int temp_min;
	int temp_max;
	int temp_diff;

	int cap;
	bool measure_battery_temp;
	bool keep_awake;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;

	u32 charge_voltage_max;
	u32 charge_voltage_drop;
	u32 normal_charge_voltage_max;
	u32 normal_charge_voltage_drop;
	u32 fast_charge_voltage_max;
	u32 fast_charge_voltage_drop;
	u32 flash_charge_voltage_max;
	u32 flash_charge_voltage_drop;
	u32 wireless_normal_charge_voltage_max;
	u32 wireless_normal_charge_voltage_drop;
	u32 wireless_fast_charge_voltage_max;
	u32 wireless_fast_charge_voltage_drop;

	int charger_status;
	u32 charger_type;
	int trigger_cnt;
	int first_trigger_cnt;
	int uvlo_trigger_cnt;
	enum uvlo_shutdown_modes uvlo_shutdown_mode;
	int low_temp_trigger_cnt;

	u32 cap_one_time;
	u32 default_cap_one_time;

	u32 trickle_time_out;
	u64 trickle_time;
	u64 trickle_start_time;

	u64 update_capacity_time;
	u64 last_query_time;

	bool force_set_full;
	u32 shutdown_voltage;

	u32 wdt_interval;

	int charge_limit_cur;
	int input_limit_cur;

	int thm_adjust_cur;

	struct sprd_battery_jeita_table *jeita_tab_array[SPRD_BATTERY_JEITA_MAX];
	u32 jeita_size[SPRD_BATTERY_JEITA_MAX];
	u32 max_current_jeita_index[SPRD_BATTERY_JEITA_MAX];
	struct sprd_battery_jeita_table *jeita_tab;
	u32 jeita_tab_size;
	int force_jeita_status;
	bool jeita_disabled;
	struct cm_jeita_info jeita_info;

	int temperature;
	int internal_resist;
	int cap_table_len;
	struct power_supply_battery_ocv_table *cap_table;
	struct cap_remap_table *cap_remap_table;
	int cap_remap_table_len;
	int cap_remap_total_cnt;
	int cap_remap_full_percent;
	bool is_fast_charge;
	bool enable_fast_charge;
	bool fixed_fchg_running;
	bool wait_vbus_stable;
	bool check_fixed_fchg_threshold;
	bool force_pps_diasbled;
	u32 fast_charge_enable_count;
	u32 fast_charge_disable_count;
	u32 double_ic_total_limit_current;
	u32 cp_nums;
	u32 psy_cp_nums;
	bool enable_alt_cp_adapt;

	bool cm_check_int;
	bool cm_check_fault;
	u32 fast_charger_type;

	int fchg_ocv_threshold;

	struct cm_charge_pump_status cp;
	struct cm_ir_compensation ir_comp;

	struct cm_charge_current cur;

	bool wl_charge_en;
	bool usb_charge_en;

	struct cm_thermal_info thm_info;

	struct mutex charger_type_mtx;
	struct mutex charge_info_mtx;
	struct mutex keep_awake_mtx;

	bool xts_limit_cur;
	int adapter_max_vbus;

	u32 pd_port_partner;
};

#define PSY_NAME_MAX	30


struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

#if IS_ENABLED(CONFIG_THERMAL)
	struct thermal_zone_device *tzd_batt;
#endif
	bool charger_enabled;

	unsigned long fullbatt_vchk_jiffies_at;
	struct delayed_work fullbatt_vchk_work;
	struct delayed_work cap_update_work;
	struct delayed_work uvlo_work;
	struct delayed_work ir_compensation_work;
	struct delayed_work fixed_fchg_work;
	struct delayed_work cp_work;
	int emergency_stop;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply_desc charger_psy_desc;
	struct power_supply *charger_psy;

	u64 charging_start_time;
	u64 charging_end_time;
	u32 charging_status;
	int battery_status;

	struct wakeup_source *charge_ws;
	struct wakeup_source *cp_ws;
	struct sprd_vote *cm_charge_vote;
	struct sprd_vchg_info *vchg_info;
	struct sprd_fchg_info *fchg_info;
};

#if IS_ENABLED(CONFIG_CHARGER_MANAGER)
extern void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg);
#else
static inline void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg) { }
#endif
#endif 
