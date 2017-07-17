/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#ifndef _INTEL_SLPC_H_
#define _INTEL_SLPC_H_

struct intel_slpc {
	bool active;
	struct i915_vma *vma;

	u32 rp_control;
	/* i915 cached SLPC frequency limits */
	u32 min_unslice_freq;
	u32 max_unslice_freq;

	u32 debug_param_id;
	u32 debug_param_value;
	u32 debug_param_override;
};

enum slpc_status {
	SLPC_STATUS_OK = 0,
	SLPC_STATUS_ERROR = 1,
	SLPC_STATUS_ILLEGAL_COMMAND = 2,
	SLPC_STATUS_INVALID_ARGS = 3,
	SLPC_STATUS_INVALID_PARAMS = 4,
	SLPC_STATUS_INVALID_DATA = 5,
	SLPC_STATUS_OUT_OF_RANGE = 6,
	SLPC_STATUS_NOT_SUPPORTED = 7,
	SLPC_STATUS_NOT_IMPLEMENTED = 8,
	SLPC_STATUS_NO_DATA = 9,
	SLPC_STATUS_EVENT_NOT_REGISTERED = 10,
	SLPC_STATUS_REGISTER_LOCKED = 11,
	SLPC_STATUS_TEMPORARILY_UNAVAILABLE = 12,
	SLPC_STATUS_VALUE_ALREADY_SET = 13,
	SLPC_STATUS_VALUE_ALREADY_UNSET = 14,
	SLPC_STATUS_VALUE_NOT_CHANGED = 15,
	SLPC_STATUS_MEMIO_ERROR = 16,
	SLPC_STATUS_EVENT_QUEUED_REQ_DPC = 17,
	SLPC_STATUS_EVENT_QUEUED_NOREQ_DPC = 18,
	SLPC_STATUS_NO_EVENT_QUEUED = 19,
	SLPC_STATUS_OUT_OF_SPACE = 20,
	SLPC_STATUS_TIMEOUT = 21,
	SLPC_STATUS_NO_LOCK = 22,
};

enum slpc_event_id {
	SLPC_EVENT_RESET = 0,
	SLPC_EVENT_SHUTDOWN = 1,
	SLPC_EVENT_PLATFORM_INFO_CHANGE = 2,
	SLPC_EVENT_DISPLAY_MODE_CHANGE = 3,
	SLPC_EVENT_FLIP_COMPLETE = 4,
	SLPC_EVENT_QUERY_TASK_STATE = 5,
	SLPC_EVENT_PARAMETER_SET = 6,
	SLPC_EVENT_PARAMETER_UNSET = 7,
};

enum slpc_param_id {
	SLPC_PARAM_TASK_ENABLE_GTPERF = 0,
	SLPC_PARAM_TASK_DISABLE_GTPERF = 1,
	SLPC_PARAM_TASK_ENABLE_BALANCER = 2,
	SLPC_PARAM_TASK_DISABLE_BALANCER = 3,
	SLPC_PARAM_TASK_ENABLE_DCC = 4,
	SLPC_PARAM_TASK_DISABLE_DCC = 5,
	SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ = 6,
	SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ = 7,
	SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ = 8,
	SLPC_PARAM_GLOBAL_MAX_GT_SLICE_FREQ_MHZ = 9,
	SLPC_PARAM_GTPERF_THRESHOLD_MAX_FPS = 10,
	SLPC_PARAM_GLOBAL_DISABLE_GT_FREQ_MANAGEMENT = 11,
	SLPC_PARAM_GTPERF_ENABLE_FRAMERATE_STALLING = 12,
	SLPC_PARAM_GLOBAL_DISABLE_RC6_MODE_CHANGE = 13,
	SLPC_PARAM_GLOBAL_OC_UNSLICE_FREQ_MHZ = 14,
	SLPC_PARAM_GLOBAL_OC_SLICE_FREQ_MHZ = 15,
	SLPC_PARAM_GLOBAL_ENABLE_IA_GT_BALANCING = 16,
	SLPC_PARAM_GLOBAL_ENABLE_ADAPTIVE_BURST_TURBO = 17,
	SLPC_PARAM_GLOBAL_ENABLE_EVAL_MODE = 18,
	SLPC_PARAM_GLOBAL_ENABLE_BALANCER_IN_NON_GAMING_MODE = 19,
	SLPC_MAX_PARAM,
	SLPC_KMD_MAX_PARAM = 32,
};

enum slpc_platform_sku {
	SLPC_PLATFORM_SKU_UNDEFINED = 0,
	SLPC_PLATFORM_SKU_ULX = 1,
	SLPC_PLATFORM_SKU_ULT = 2,
	SLPC_PLATFORM_SKU_T = 3,
	SLPC_PLATFORM_SKU_MOBL = 4,
	SLPC_PLATFORM_SKU_DT = 5,
	SLPC_PLATFORM_SKU_UNKNOWN = 6,
};

enum slpc_power_source {
	SLPC_POWER_SOURCE_UNDEFINED = 0,
	SLPC_POWER_SOURCE_AC = 1,
	SLPC_POWER_SOURCE_DC = 2,
	SLPC_POWER_SOURCE_UNKNOWN = 3,
};

enum slpc_power_plan {
	SLPC_POWER_PLAN_UNDEFINED = 0,
	SLPC_POWER_PLAN_BATTERY_SAVER = 1,
	SLPC_POWER_PLAN_BALANCED = 2,
	SLPC_POWER_PLAN_PERFORMANCE = 3,
	SLPC_POWER_PLAN_UNKNOWN = 4,
};

struct slpc_platform_info {
	u8 platform_sku;
	u8 slice_count;
	u8 reserved;
	u8 power_plan_source;
	u8 P0_freq;
	u8 P1_freq;
	u8 Pe_freq;
	u8 Pn_freq;
	u32 reserved1;
	u32 reserved2;
} __packed;

enum slpc_global_state {
	SLPC_GLOBAL_STATE_NOT_RUNNING = 0,
	SLPC_GLOBAL_STATE_INITIALIZING = 1,
	SLPC_GLOBAL_STATE_RESETTING = 2,
	SLPC_GLOBAL_STATE_RUNNING = 3,
	SLPC_GLOBAL_STATE_SHUTTING_DOWN = 4,
	SLPC_GLOBAL_STATE_ERROR = 5
};

struct slpc_task_state_data {
	union {
		u32 bitfield1;
		struct {
			u32 gtperf_task_active:1;
			u32 gtperf_stall_possible:1;
			u32 gtperf_gaming_mode:1;
			u32 gtperf_target_fps:8;
			u32 dcc_task_active:1;
			u32 in_dcc:1;
			u32 in_dct:1;
			u32 freq_switch_active:1;
			u32 ibc_enabled:1;
			u32 ibc_active:1;
			u32 pg1_enabled:1;
			u32 pg1_active:1;
			u32 reserved:13;
		};
	};
	union {
		u32 bitfield2;
		struct {
			u32 max_unslice_freq:8;
			u32 min_unslice_freq:8;
			u32 max_slice_freq:8;
			u32 min_slice_freq:8;
		};
	};
} __packed;

#define SLPC_MAX_OVERRIDE_PARAMETERS 192
#define SLPC_OVERRIDE_BITFIELD_SIZE ((SLPC_MAX_OVERRIDE_PARAMETERS + 31) / 32)

struct slpc_shared_data {
	u32 reserved;
	u32 shared_data_size;
	u32 global_state;
	struct slpc_platform_info platform_info;
	struct slpc_task_state_data task_state_data;
	u32 override_parameters_set_bits[SLPC_OVERRIDE_BITFIELD_SIZE];
	u32 override_parameters_values[SLPC_MAX_OVERRIDE_PARAMETERS];
} __packed;

enum slpc_reset_flags {
	SLPC_RESET_FLAG_TDR_OCCURRED
};

#define SLPC_EVENT_MAX_INPUT_ARGS  7
#define SLPC_EVENT_MAX_OUTPUT_ARGS 1

union slpc_event_input_header {
	u32 value;
	struct {
		u32 num_args:8;
		u32 event_id:8;
	};
};

struct slpc_event_input {
	u32 h2g_action_id;
	union slpc_event_input_header header;
	u32 args[SLPC_EVENT_MAX_INPUT_ARGS];
} __packed;

union slpc_event_output_header {
	u32 value;
	struct {
		u32 num_args:8;
		u32 event_id:8;
		u32 status:16;
	};
};

struct slpc_event_output {
	u32 reserved;
	union slpc_event_output_header header;
	u32 args[SLPC_EVENT_MAX_OUTPUT_ARGS];
} __packed;

#define SLPC_EVENT(id, argc)	((u32) (id) << 8 | (argc))
#define SLPC_POWER_PLAN_SOURCE(plan, source) ((plan) | ((source) << 6))
#define SLPC_POWER_PLAN(plan_source) ((plan_source) & 0x3F)
#define SLPC_POWER_SOURCE(plan_source) ((plan_source) >> 6)

/* Structures for exposing parameter details to user. */
#define MAX_PARAM_DESCRIPTION_SIZE	160
struct slpc_param {
	enum slpc_param_id id;
	char description[MAX_PARAM_DESCRIPTION_SIZE];
};

extern struct slpc_param slpc_paramlist[];

#define SLPC_PARAM_TASK_DEFAULT  0
#define SLPC_PARAM_TASK_ENABLED  1
#define SLPC_PARAM_TASK_DISABLED 2
#define SLPC_PARAM_TASK_UNKNOWN  3

extern const struct file_operations i915_slpc_param_ctl_fops;
extern const struct file_operations i915_slpc_gtperf_fops;
extern const struct file_operations i915_slpc_balancer_fops;
extern const struct file_operations i915_slpc_dcc_fops;

/* intel_slpc.c */
void intel_slpc_set_param(struct drm_i915_private *dev_priv, u32 id, u32 value);
void intel_slpc_unset_param(struct drm_i915_private *dev_priv, u32 id);
void intel_slpc_get_param(struct drm_i915_private *dev_priv, u32 id,
			  int *overriding, u32 *value);
int intel_slpc_task_control(struct drm_i915_private *dev_priv, u64 val,
			    u32 enable_id, u32 disable_id);
int intel_slpc_task_status(struct drm_i915_private *dev_priv, u64 *val,
			   u32 enable_id, u32 disable_id);
void intel_slpc_read_shared_data(struct drm_i915_private *dev_priv,
				struct slpc_shared_data *data);
const char *intel_slpc_get_state_str(enum slpc_global_state state);
bool intel_slpc_get_status(struct drm_i915_private *dev_priv);
void intel_slpc_save_default_rps(struct drm_i915_private *dev_priv);
int intel_slpc_max_freq_set(struct drm_i915_private *dev_priv, u32 val);
int intel_slpc_min_freq_set(struct drm_i915_private *dev_priv, u32 val);
void intel_slpc_init(struct drm_i915_private *dev_priv);
void intel_slpc_cleanup(struct drm_i915_private *dev_priv);
void intel_slpc_enable(struct drm_i915_private *dev_priv);
void intel_slpc_disable(struct drm_i915_private *dev_priv);

#endif
