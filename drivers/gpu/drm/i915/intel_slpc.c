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
#include <linux/firmware.h>
#include <asm/msr-index.h>
#include "i915_drv.h"
#include "intel_uc.h"
#include <linux/seq_file.h>
#include <linux/debugfs.h>

struct slpc_param slpc_paramlist[SLPC_MAX_PARAM] = {
	{SLPC_PARAM_TASK_ENABLE_GTPERF, "Enable task GTPERF"},
	{SLPC_PARAM_TASK_DISABLE_GTPERF, "Disable task GTPERF"},
	{SLPC_PARAM_TASK_ENABLE_BALANCER, "Enable task BALANCER"},
	{SLPC_PARAM_TASK_DISABLE_BALANCER, "Disable task BALANCER"},
	{SLPC_PARAM_TASK_ENABLE_DCC, "Enable task DCC"},
	{SLPC_PARAM_TASK_DISABLE_DCC, "Disable task DCC"},
	{SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
				"Minimum GT frequency request for unslice"},
	{SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ,
				"Maximum GT frequency request for unslice"},
	{SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ,
				"Minimum GT frequency request for slice"},
	{SLPC_PARAM_GLOBAL_MAX_GT_SLICE_FREQ_MHZ,
				"Maximum GT frequency request for slice"},
	{SLPC_PARAM_GTPERF_THRESHOLD_MAX_FPS,
				"If non-zero, algorithm will slow down "
				"frame-based applications to this frame-rate"},
	{SLPC_PARAM_GLOBAL_DISABLE_GT_FREQ_MANAGEMENT,
				"Lock GT frequency request to RPe"},
	{SLPC_PARAM_GTPERF_ENABLE_FRAMERATE_STALLING,
				"Set to TRUE to enable slowing framerate"},
	{SLPC_PARAM_GLOBAL_DISABLE_RC6_MODE_CHANGE,
				"Prevent from changing the RC mode"},
	{SLPC_PARAM_GLOBAL_OC_UNSLICE_FREQ_MHZ,
				"Override fused value of unslice RP0"},
	{SLPC_PARAM_GLOBAL_OC_SLICE_FREQ_MHZ,
				"Override fused value of slice RP0"},
	{SLPC_PARAM_GLOBAL_ENABLE_IA_GT_BALANCING,
				"TRUE means enable Intelligent Bias Control"},
	{SLPC_PARAM_GLOBAL_ENABLE_ADAPTIVE_BURST_TURBO,
				"TRUE = enable eval mode when transitioning "
				"from idle to active."},
	{SLPC_PARAM_GLOBAL_ENABLE_EVAL_MODE,
				"FALSE = disable eval mode completely"},
	{SLPC_PARAM_GLOBAL_ENABLE_BALANCER_IN_NON_GAMING_MODE,
				"Enable IBC when non-Gaming Mode is enabled"}
};

static int slpc_param_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct intel_slpc *slpc = &dev_priv->guc.slpc;

	if (!dev_priv->guc.slpc.active) {
		seq_puts(m, "SLPC not active\n");
		return 0;
	}

	seq_printf(m, "%s=%u, override=%s\n",
			slpc_paramlist[slpc->debug_param_id].description,
			slpc->debug_param_value,
			yesno(!!slpc->debug_param_override));

	return 0;
}

static int slpc_param_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, slpc_param_ctl_show, inode->i_private);
}

static const char *read_token = "read", *write_token = "write",
		  *revert_token = "revert";

/*
 * Parse SLPC parameter control strings: (Similar to Pipe CRC handling)
 *   command: wsp* op wsp+ param id wsp+ [value] wsp*
 *   op: "read"/"write"/"revert"
 *   param id: slpc_param_id
 *   value: u32 value
 *   wsp: (#0x20 | #0x9 | #0xA)+
 *
 * eg.:
 *  "read 0"		-> read SLPC_PARAM_TASK_ENABLE_GTPERF
 *  "write 7 500"	-> set SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ to 500MHz
 *  "revert 7"		-> revert SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ to
 *			   default value.
 */
static int slpc_param_ctl_parse(struct drm_i915_private *dev_priv,
				char *buf, size_t len, char **op,
				u32 *id, u32 *value)
{
#define MAX_WORDS 3
	int n_words;
	char *words[MAX_WORDS];
	ssize_t ret;

	n_words = buffer_tokenize(buf, words, MAX_WORDS);
	if (!(n_words == 3) && !(n_words == 2)) {
		DRM_DEBUG_DRIVER("tokenize failed, a command is %d words\n",
				 MAX_WORDS);
		return -EINVAL;
	}

	if (strcmp(words[0], read_token) && strcmp(words[0], write_token) &&
	    strcmp(words[0], revert_token)) {
		DRM_DEBUG_DRIVER("unknown operation\n");
		return -EINVAL;
	}

	*op = words[0];

	ret = kstrtou32(words[1], 0, id);
	if (ret)
		return ret;

	if (n_words == 3) {
		ret = kstrtou32(words[2], 0, value);
		if (ret)
			return ret;
	}

	return 0;
}

static ssize_t slpc_param_ctl_write(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct intel_slpc *slpc = &dev_priv->guc.slpc;
	char *tmpbuf, *op = NULL;
	u32 id, value;
	int ret;

	if (len == 0)
		return 0;

	if (len > 40) {
		DRM_DEBUG_DRIVER("expected <40 chars into slpc param control\n");
		return -E2BIG;
	}

	tmpbuf = kmalloc(len + 1, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	if (copy_from_user(tmpbuf, ubuf, len)) {
		ret = -EFAULT;
		goto out;
	}
	tmpbuf[len] = '\0';

	ret = slpc_param_ctl_parse(dev_priv, tmpbuf, len, &op, &id, &value);

	if (id >= SLPC_MAX_PARAM) {
		ret = -EINVAL;
		goto out;
	}

	if (!strcmp(op, read_token)) {
		intel_slpc_get_param(dev_priv, id,
				     &slpc->debug_param_override,
				     &slpc->debug_param_value);
		slpc->debug_param_id = id;
	} else if (!strcmp(op, write_token) || !strcmp(op, revert_token)) {
		if ((id >= SLPC_PARAM_TASK_ENABLE_GTPERF) &&
		    (id <= SLPC_PARAM_TASK_DISABLE_DCC)) {
			DRM_DEBUG_DRIVER("Tasks are not controlled by "
					 "this interface\n");
			ret = -EINVAL;
			goto out;
		}

		/*
		 * After updating parameters, RESET event has to be sent to GuC
		 * SLPC for ensuring parameters take effect.
		 */
		intel_runtime_pm_get(dev_priv);
		if (!strcmp(op, write_token))
			intel_slpc_set_param(dev_priv, id, value);
		else if (!strcmp(op, revert_token))
			intel_slpc_unset_param(dev_priv, id);
		intel_slpc_enable(dev_priv);
		intel_runtime_pm_put(dev_priv);
	}

out:
	kfree(tmpbuf);
	if (ret < 0)
		return ret;

	*offp += len;
	return len;
}

const struct file_operations i915_slpc_param_ctl_fops = {
	.owner = THIS_MODULE,
	.open = slpc_param_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = slpc_param_ctl_write
};

static void slpc_task_param_show(struct seq_file *m, u32 enable_id,
				 u32 disable_id)
{
	struct drm_i915_private *dev_priv = m->private;
	const char *status;
	u64 val;
	int ret;

	ret = intel_slpc_task_status(dev_priv, &val, enable_id, disable_id);

	if (ret) {
		seq_printf(m, "error %d\n", ret);
	} else {
		switch (val) {
		case SLPC_PARAM_TASK_DEFAULT:
			status = "default\n";
			break;

		case SLPC_PARAM_TASK_ENABLED:
			status = "enabled\n";
			break;

		case SLPC_PARAM_TASK_DISABLED:
			status = "disabled\n";
			break;

		default:
			status = "unknown\n";
			break;
		}

		seq_puts(m, status);
	}
}

static int slpc_task_param_write(struct seq_file *m, const char __user *ubuf,
			    size_t len, u32 enable_id, u32 disable_id)
{
	struct drm_i915_private *dev_priv = m->private;
	u64 val;
	int ret = 0;
	char buf[10];

	if (len >= sizeof(buf))
		ret = -EINVAL;
	else if (copy_from_user(buf, ubuf, len))
		ret = -EFAULT;
	else
		buf[len] = '\0';

	if (!ret) {
		if (!strncmp(buf, "default", 7))
			val = SLPC_PARAM_TASK_DEFAULT;
		else if (!strncmp(buf, "enabled", 7))
			val = SLPC_PARAM_TASK_ENABLED;
		else if (!strncmp(buf, "disabled", 8))
			val = SLPC_PARAM_TASK_DISABLED;
		else
			ret = -EINVAL;
	}

	if (!ret)
		ret = intel_slpc_task_control(dev_priv, val, enable_id,
					      disable_id);

	return ret;
}

static int slpc_gtperf_show(struct seq_file *m, void *data)
{
	slpc_task_param_show(m, SLPC_PARAM_TASK_ENABLE_GTPERF,
			SLPC_PARAM_TASK_DISABLE_GTPERF);

	return 0;
}

static int slpc_gtperf_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, slpc_gtperf_show, dev_priv);
}

static ssize_t slpc_gtperf_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	int ret = 0;

	ret = slpc_task_param_write(m, ubuf, len, SLPC_PARAM_TASK_ENABLE_GTPERF,
			       SLPC_PARAM_TASK_DISABLE_GTPERF);
	if (ret)
		return ret;

	return len;
}

const struct file_operations i915_slpc_gtperf_fops = {
	.owner	 = THIS_MODULE,
	.open	 = slpc_gtperf_open,
	.release = single_release,
	.read	 = seq_read,
	.write	 = slpc_gtperf_write,
	.llseek	 = seq_lseek
};

static int slpc_balancer_show(struct seq_file *m, void *data)
{
	slpc_task_param_show(m, SLPC_PARAM_TASK_ENABLE_BALANCER,
			SLPC_PARAM_TASK_DISABLE_BALANCER);

	return 0;
}

static int slpc_balancer_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, slpc_balancer_show, dev_priv);
}

static ssize_t slpc_balancer_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	int ret = 0;

	ret = slpc_task_param_write(m, ubuf, len,
				SLPC_PARAM_TASK_ENABLE_BALANCER,
				SLPC_PARAM_TASK_DISABLE_BALANCER);
	if (ret)
		return ret;

	return len;
}

const struct file_operations i915_slpc_balancer_fops = {
	.owner	 = THIS_MODULE,
	.open	 = slpc_balancer_open,
	.release = single_release,
	.read	 = seq_read,
	.write	 = slpc_balancer_write,
	.llseek	 = seq_lseek
};

static int slpc_dcc_show(struct seq_file *m, void *data)
{
	slpc_task_param_show(m, SLPC_PARAM_TASK_ENABLE_DCC,
			SLPC_PARAM_TASK_DISABLE_DCC);

	return 0;
}

static int slpc_dcc_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, slpc_dcc_show, dev_priv);
}

static ssize_t slpc_dcc_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	int ret = 0;

	ret = slpc_task_param_write(m, ubuf, len, SLPC_PARAM_TASK_ENABLE_DCC,
			       SLPC_PARAM_TASK_DISABLE_DCC);
	if (ret)
		return ret;

	return len;
}

const struct file_operations i915_slpc_dcc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = slpc_dcc_open,
	.release = single_release,
	.read	 = seq_read,
	.write	 = slpc_dcc_write,
	.llseek	 = seq_lseek
};

static void host2guc_slpc(struct drm_i915_private *dev_priv,
			  struct slpc_event_input *input, u32 len)
{
	u32 *data;
	u32 output[SLPC_EVENT_MAX_OUTPUT_ARGS];
	int ret = 0;

	/*
	 * We have only 15 scratch registers for communication.
	 * the first we will use for the event ID in input and
	 * output data. Event processing status will be present
	 * in SOFT_SCRATCH(1) register.
	 */
	BUILD_BUG_ON(SLPC_EVENT_MAX_INPUT_ARGS > 14);
	BUILD_BUG_ON(SLPC_EVENT_MAX_OUTPUT_ARGS < 1);
	BUILD_BUG_ON(SLPC_EVENT_MAX_OUTPUT_ARGS > 14);

	data = (u32 *) input;
	data[0] = INTEL_GUC_ACTION_SLPC_REQUEST;
	ret = __intel_guc_send_mmio(&dev_priv->guc, data, len, output);

	if (ret)
		DRM_ERROR("event 0x%x status %d\n",
			  ((output[0] & 0xFF00) >> 8), ret);
}

void slpc_mem_set_param(struct slpc_shared_data *data,
			      u32 id,
			      u32 value)
{
	data->override_parameters_set_bits[id >> 5]
						|= (1 << (id % 32));
	data->override_parameters_values[id] = value;
}

void slpc_mem_unset_param(struct slpc_shared_data *data,
				u32 id)
{
	data->override_parameters_set_bits[id >> 5]
						&= (~(1 << (id % 32)));
	data->override_parameters_values[id] = 0;
}

static void host2guc_slpc_set_param(struct drm_i915_private *dev_priv,
				    u32 id, u32 value)
{
	struct slpc_event_input data = {0};

	data.header.value = SLPC_EVENT(SLPC_EVENT_PARAMETER_SET, 2);
	data.args[0] = id;
	data.args[1] = value;

	host2guc_slpc(dev_priv, &data, 4);
}

static void host2guc_slpc_unset_param(struct drm_i915_private *dev_priv,
				      u32 id)
{
	struct slpc_event_input data = {0};

	data.header.value = SLPC_EVENT(SLPC_EVENT_PARAMETER_UNSET, 1);
	data.args[0] = id;

	host2guc_slpc(dev_priv, &data, 3);
}

void intel_slpc_set_param(struct drm_i915_private *dev_priv,
			  u32 id,
			  u32 value)
{
	struct page *page;
	struct slpc_shared_data *data = NULL;

	WARN_ON(id >= SLPC_MAX_PARAM);

	if (!dev_priv->guc.slpc.vma)
		return;

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	data = kmap_atomic(page);
	slpc_mem_set_param(data, id, value);
	kunmap_atomic(data);

	host2guc_slpc_set_param(dev_priv, id, value);
}

void intel_slpc_unset_param(struct drm_i915_private *dev_priv,
			    u32 id)
{
	struct page *page;
	struct slpc_shared_data *data = NULL;

	WARN_ON(id >= SLPC_MAX_PARAM);

	if (!dev_priv->guc.slpc.vma)
		return;

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	data = kmap_atomic(page);
	slpc_mem_unset_param(data, id);
	kunmap_atomic(data);

	host2guc_slpc_unset_param(dev_priv, id);
}

void intel_slpc_get_param(struct drm_i915_private *dev_priv,
			  u32 id,
			  int *overriding, u32 *value)
{
	struct page *page;
	struct slpc_shared_data *data = NULL;
	u32 bits;

	WARN_ON(id >= SLPC_MAX_PARAM);

	if (!dev_priv->guc.slpc.vma)
		return;

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	data = kmap_atomic(page);
	if (overriding) {
		bits = data->override_parameters_set_bits[id >> 5];
		*overriding = (0 != (bits & (1 << (id % 32))));
	}
	if (value)
		*value = data->override_parameters_values[id];

	kunmap_atomic(data);
}

int slpc_mem_task_control(struct slpc_shared_data *data, u64 val,
			  u32 enable_id, u32 disable_id)
{
	int ret = 0;

	if (val == SLPC_PARAM_TASK_DEFAULT) {
		/* set default */
		slpc_mem_unset_param(data, enable_id);
		slpc_mem_unset_param(data, disable_id);
	} else if (val == SLPC_PARAM_TASK_ENABLED) {
		/* set enable */
		slpc_mem_set_param(data, enable_id, 1);
		slpc_mem_unset_param(data, disable_id);
	} else if (val == SLPC_PARAM_TASK_DISABLED) {
		/* set disable */
		slpc_mem_set_param(data, disable_id, 1);
		slpc_mem_unset_param(data, enable_id);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int intel_slpc_task_control(struct drm_i915_private *dev_priv, u64 val,
			    u32 enable_id, u32 disable_id)
{
	int ret = 0;

	if (!dev_priv->guc.slpc.active)
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	if (val == SLPC_PARAM_TASK_DEFAULT) {
		/* set default */
		intel_slpc_unset_param(dev_priv, enable_id);
		intel_slpc_unset_param(dev_priv, disable_id);
	} else if (val == SLPC_PARAM_TASK_ENABLED) {
		/* set enable */
		intel_slpc_set_param(dev_priv, enable_id, 1);
		intel_slpc_unset_param(dev_priv, disable_id);
	} else if (val == SLPC_PARAM_TASK_DISABLED) {
		/* set disable */
		intel_slpc_set_param(dev_priv, disable_id, 1);
		intel_slpc_unset_param(dev_priv, enable_id);
	} else {
		ret = -EINVAL;
	}

	intel_slpc_enable(dev_priv);
	intel_runtime_pm_put(dev_priv);

	return ret;
}

int intel_slpc_task_status(struct drm_i915_private *dev_priv, u64 *val,
			   u32 enable_id, u32 disable_id)
{
	int override_enable, override_disable;
	u32 value_enable, value_disable;
	int ret = 0;

	if (!dev_priv->guc.slpc.active) {
		ret = -ENODEV;
	} else if (val) {
		intel_slpc_get_param(dev_priv, enable_id, &override_enable,
				     &value_enable);
		intel_slpc_get_param(dev_priv, disable_id, &override_disable,
				     &value_disable);

		/*
		 * Set the output value:
		 * 0: default
		 * 1: enabled
		 * 2: disabled
		 * 3: unknown (should not happen)
		 */
		if (override_disable && (value_disable == 1))
			*val = SLPC_PARAM_TASK_DISABLED;
		else if (override_enable && (value_enable == 1))
			*val = SLPC_PARAM_TASK_ENABLED;
		else if (!override_enable && !override_disable)
			*val = SLPC_PARAM_TASK_DEFAULT;
		else
			*val = SLPC_PARAM_TASK_UNKNOWN;

	} else {
		ret = -EINVAL;
	}

	return ret;
}

static unsigned int slpc_get_platform_sku(struct drm_i915_private *dev_priv)
{
	enum slpc_platform_sku platform_sku;

	if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv))
		platform_sku = SLPC_PLATFORM_SKU_ULX;
	else if (IS_SKL_ULT(dev_priv) || IS_KBL_ULT(dev_priv))
		platform_sku = SLPC_PLATFORM_SKU_ULT;
	else
		platform_sku = SLPC_PLATFORM_SKU_DT;

	WARN_ON(platform_sku > 0xFF);

	return platform_sku;
}

static unsigned int slpc_get_slice_count(struct drm_i915_private *dev_priv)
{
	unsigned int slice_count = 1;

	if (IS_SKYLAKE(dev_priv))
		slice_count = hweight8(INTEL_INFO(dev_priv)->sseu.slice_mask);

	return slice_count;
}

static void slpc_shared_data_init(struct drm_i915_private *dev_priv)
{
	struct page *page;
	struct slpc_shared_data *data;
	u64 val;

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	data = kmap_atomic(page);

	memset(data, 0, sizeof(struct slpc_shared_data));

	data->shared_data_size = sizeof(struct slpc_shared_data);
	data->global_state = SLPC_GLOBAL_STATE_NOT_RUNNING;
	data->platform_info.platform_sku =
				slpc_get_platform_sku(dev_priv);
	data->platform_info.slice_count =
				slpc_get_slice_count(dev_priv);
	data->platform_info.power_plan_source =
		SLPC_POWER_PLAN_SOURCE(SLPC_POWER_PLAN_PERFORMANCE,
					    SLPC_POWER_SOURCE_AC);
	rdmsrl(MSR_TURBO_RATIO_LIMIT, val);
	data->platform_info.P0_freq = val;
	rdmsrl(MSR_PLATFORM_INFO, val);
	data->platform_info.P1_freq = val >> 8;
	data->platform_info.Pe_freq = val >> 40;
	data->platform_info.Pn_freq = val >> 48;

	/* Enable only GTPERF task, Disable others */
	val = SLPC_PARAM_TASK_ENABLED;
	slpc_mem_task_control(data, val,
			      SLPC_PARAM_TASK_ENABLE_GTPERF,
			      SLPC_PARAM_TASK_DISABLE_GTPERF);

	slpc_mem_task_control(data, val,
			      SLPC_PARAM_TASK_ENABLE_BALANCER,
			      SLPC_PARAM_TASK_DISABLE_BALANCER);

	val = SLPC_PARAM_TASK_DISABLED;
	slpc_mem_task_control(data, val,
			      SLPC_PARAM_TASK_ENABLE_DCC,
			      SLPC_PARAM_TASK_DISABLE_DCC);

	slpc_mem_set_param(data, SLPC_PARAM_GTPERF_THRESHOLD_MAX_FPS, 0);

	slpc_mem_set_param(data, SLPC_PARAM_GTPERF_ENABLE_FRAMERATE_STALLING,
			   0);

	slpc_mem_set_param(data, SLPC_PARAM_GLOBAL_ENABLE_IA_GT_BALANCING,
			   1);

	slpc_mem_set_param(data,
			   SLPC_PARAM_GLOBAL_ENABLE_ADAPTIVE_BURST_TURBO,
			   0);

	slpc_mem_set_param(data, SLPC_PARAM_GLOBAL_ENABLE_EVAL_MODE, 0);

	slpc_mem_set_param(data,
			   SLPC_PARAM_GLOBAL_ENABLE_BALANCER_IN_NON_GAMING_MODE,
			   1);

	slpc_mem_set_param(data,
			   SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
			   intel_gpu_freq(dev_priv,
				dev_priv->rps.efficient_freq));
	slpc_mem_set_param(data,
			   SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ,
			   intel_gpu_freq(dev_priv,
				dev_priv->rps.efficient_freq));

	kunmap_atomic(data);
        //intel_slpc_enable(dev_priv);
}

static void host2guc_slpc_reset(struct drm_i915_private *dev_priv)
{
	struct slpc_event_input data = {0};
	u32 shared_data_gtt_offset = guc_ggtt_offset(dev_priv->guc.slpc.vma);

	data.header.value = SLPC_EVENT(SLPC_EVENT_RESET, 2);
	data.args[0] = shared_data_gtt_offset;
	data.args[1] = 0;

	host2guc_slpc(dev_priv, &data, 4);
}

static void host2guc_slpc_query_task_state(struct drm_i915_private *dev_priv)
{
	struct slpc_event_input data = {0};
	u32 shared_data_gtt_offset = guc_ggtt_offset(dev_priv->guc.slpc.vma);

	data.header.value = SLPC_EVENT(SLPC_EVENT_QUERY_TASK_STATE, 2);
	data.args[0] = shared_data_gtt_offset;
	data.args[1] = 0;

	host2guc_slpc(dev_priv, &data, 4);
}

static void host2guc_slpc_shutdown(struct drm_i915_private *dev_priv)
{
	struct slpc_event_input data = {0};
	u32 shared_data_gtt_offset = guc_ggtt_offset(dev_priv->guc.slpc.vma);

	data.header.value = SLPC_EVENT(SLPC_EVENT_SHUTDOWN, 2);
	data.args[0] = shared_data_gtt_offset;
	data.args[1] = 0;

	host2guc_slpc(dev_priv, &data, 4);
}

void intel_slpc_query_task_state(struct drm_i915_private *dev_priv)
{
	if (dev_priv->guc.slpc.active)
		host2guc_slpc_query_task_state(dev_priv);
}

/*
 * This function will reads the state updates from GuC SLPC into shared data
 * by invoking H2G action. Returns current state of GuC SLPC.
 */
void intel_slpc_read_shared_data(struct drm_i915_private *dev_priv,
				 struct slpc_shared_data *data)
{
	struct page *page;
	void *pv = NULL;

	intel_slpc_query_task_state(dev_priv);

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	pv = kmap_atomic(page);

	drm_clflush_virt_range(pv, sizeof(struct slpc_shared_data));
	memcpy(data, pv, sizeof(struct slpc_shared_data));

	kunmap_atomic(pv);
}

const char *intel_slpc_get_state_str(enum slpc_global_state state)
{
	if (state == SLPC_GLOBAL_STATE_NOT_RUNNING)
		return "not running";
	else if (state == SLPC_GLOBAL_STATE_INITIALIZING)
		return "initializing";
	else if (state == SLPC_GLOBAL_STATE_RESETTING)
		return "resetting";
	else if (state == SLPC_GLOBAL_STATE_RUNNING)
		return "running";
	else if (state == SLPC_GLOBAL_STATE_SHUTTING_DOWN)
		return "shutting down";
	else if (state == SLPC_GLOBAL_STATE_ERROR)
		return "error";
	else
		return "unknown";
}

bool intel_slpc_get_status(struct drm_i915_private *dev_priv)
{
	struct slpc_shared_data data;
	bool ret = false;

	intel_slpc_read_shared_data(dev_priv, &data);
	DRM_INFO("SLPC state: %s\n",
		 intel_slpc_get_state_str(data.global_state));

	switch (data.global_state) {
	case SLPC_GLOBAL_STATE_RUNNING:
		/* Capture required state from SLPC here */
		dev_priv->guc.slpc.max_unslice_freq =
				data.task_state_data.max_unslice_freq *
				GEN9_FREQ_SCALER;
		dev_priv->guc.slpc.min_unslice_freq =
				data.task_state_data.min_unslice_freq *
				GEN9_FREQ_SCALER;
		ret = true;
		break;
	case SLPC_GLOBAL_STATE_ERROR:
		DRM_ERROR("SLPC in error state.\n");
		break;
	case SLPC_GLOBAL_STATE_RESETTING:
		/*
		 * SLPC enabling in GuC should be completing fast.
		 * If SLPC is taking time to initialize (unlikely as we are
		 * sending reset event during GuC load itself).
		 * TODO: Need to wait till state changes to RUNNING.
		 */
		ret = true;
		DRM_ERROR("SLPC not running yet.!!!");
		break;
	default:
		break;
	}
	return ret;
}

/*
 * Uncore sanitize clears RPS state in Host GTPM flows set by BIOS, Save the
 * initial BIOS programmed RPS state that is needed by SLPC and not set by SLPC.
 * Set this state while enabling SLPC.
 */
void intel_slpc_save_default_rps(struct drm_i915_private *dev_priv)
{
	dev_priv->guc.slpc.rp_control = I915_READ(GEN6_RP_CONTROL);
}

static void intel_slpc_restore_default_rps(struct drm_i915_private *dev_priv)
{
	I915_WRITE(GEN6_RP_CONTROL, dev_priv->guc.slpc.rp_control);
}

/*
 * TODO: Add separate interfaces to set Max/Min Slice frequency.
 * Since currently both slice and unslice are configured to same
 * frequencies these unified interface relying on Unslice frequencies
 * should be sufficient. These functions take frequency opcode as input.
 */
int intel_slpc_max_freq_set(struct drm_i915_private *dev_priv, u32 val)
{
	if (val < dev_priv->rps.min_freq ||
	    val > dev_priv->rps.max_freq ||
	    val < dev_priv->guc.slpc.min_unslice_freq)
		return -EINVAL;

	intel_slpc_set_param(dev_priv,
			     SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ,
			     intel_gpu_freq(dev_priv, val));
	intel_slpc_set_param(dev_priv,
			     SLPC_PARAM_GLOBAL_MAX_GT_SLICE_FREQ_MHZ,
			     intel_gpu_freq(dev_priv, val));

	intel_slpc_enable(dev_priv);

	dev_priv->guc.slpc.max_unslice_freq = val;

	return 0;
}

int intel_slpc_min_freq_set(struct drm_i915_private *dev_priv, u32 val)
{
	if (val < dev_priv->rps.min_freq ||
	    val > dev_priv->rps.max_freq ||
	    val > dev_priv->guc.slpc.max_unslice_freq)
		return -EINVAL;

	intel_slpc_set_param(dev_priv,
			     SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
			     intel_gpu_freq(dev_priv, val));
	intel_slpc_set_param(dev_priv,
			     SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ,
			     intel_gpu_freq(dev_priv, val));

	intel_slpc_enable(dev_priv);

	dev_priv->guc.slpc.min_unslice_freq = val;

	return 0;
}

void intel_slpc_init(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_vma *vma;

	dev_priv->guc.slpc.active = false;

	mutex_lock(&dev_priv->rps.hw_lock);
	gen6_init_rps_frequencies(dev_priv);
	mutex_unlock(&dev_priv->rps.hw_lock);

	/* Allocate shared data structure */
	vma = dev_priv->guc.slpc.vma;
	if (!vma) {
		vma = intel_guc_allocate_vma(guc,
			       PAGE_ALIGN(sizeof(struct slpc_shared_data)));
		if (IS_ERR(vma)) {
			DRM_ERROR("slpc_shared_data allocation failed\n");
			i915.enable_slpc = 0;
			return;
		}

		dev_priv->guc.slpc.vma = vma;
		slpc_shared_data_init(dev_priv);
	}
}

void intel_slpc_cleanup(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	struct slpc_shared_data data;

	/* Ensure SLPC is not running prior to releasing Shared data */
	intel_slpc_read_shared_data(dev_priv, &data);
	WARN_ON(data.global_state != SLPC_GLOBAL_STATE_NOT_RUNNING);

	/* Release shared data structure */
	i915_vma_unpin_and_release(&guc->slpc.vma);
}

void intel_slpc_enable(struct drm_i915_private *dev_priv)
{
	struct page *page;
	struct slpc_shared_data *data;

	intel_slpc_restore_default_rps(dev_priv);

	page = i915_vma_first_page(dev_priv->guc.slpc.vma);
	data = kmap_atomic(page);
	data->global_state = SLPC_GLOBAL_STATE_NOT_RUNNING;
	kunmap_atomic(data);

	host2guc_slpc_reset(dev_priv);
	dev_priv->guc.slpc.active = true;
}

void intel_slpc_disable(struct drm_i915_private *dev_priv)
{
	host2guc_slpc_shutdown(dev_priv);
	dev_priv->guc.slpc.active = false;
}
