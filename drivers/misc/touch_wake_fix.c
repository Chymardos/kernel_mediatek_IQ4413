/*
 * drivers/misc/touch_wake_fix.c
 *
 * Copyright (c) 2016, Vasilii Kovalev <vgdn1942@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>
#include <linux/touch_wake_fix.h>

int screen_state;
int touch_vibrate = 1;

int touch_vibrate_set(void) {

	if(touch_vibrate == 1)
		return 1;
	else
		return 0;

}

/********************* SYSFS INTERFACE ***********************/
static ssize_t screen_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", screen_state);
}

static ssize_t screen_state_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return 0;
}

static ssize_t ps_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", cm36283_ps_check());
}

static ssize_t ps_state_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return 0;
}

static ssize_t vibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", touch_vibrate);
}

static ssize_t vibrate_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val = 0;

	sscanf(buf, "%u\n", &val);

	if ( ( val == 0 ) || ( val == 1 ) )
		touch_vibrate = val;

	return size;
}

static DEVICE_ATTR(screen_state_enable, 0777,
		screen_state_show, screen_state_set);

static DEVICE_ATTR(ps_state_enable, 0777,
		ps_state_show, ps_state_set);

static DEVICE_ATTR(vibrate_enable, 0777,
		vibrate_show, vibrate_set);

/********************************************************************************************/
static struct attribute *touch_wake_fix_attributes[] =
{
	&dev_attr_vibrate_enable.attr,
	&dev_attr_ps_state_enable.attr,
	&dev_attr_screen_state_enable.attr,
	NULL
};

static struct attribute_group touch_wake_fix_group =
{
	.attrs  = touch_wake_fix_attributes,
};

extern struct kobject *touch_wake_fix_kobj;

static int touch_wake_fix_init_sysfs(void) {

	int rc = 0;

	struct kobject *touch_wake_fix_kobj;
	touch_wake_fix_kobj = kobject_create_and_add("twf_debug", NULL);

	dev_attr_screen_state_enable.attr.name = "screen_state";
	dev_attr_ps_state_enable.attr.name = "ps_state";
	dev_attr_vibrate_enable.attr.name = "vibrate";

	rc = sysfs_create_group(touch_wake_fix_kobj,
			&touch_wake_fix_group);

	if (unlikely(rc < 0))
		pr_err("touch_wake_fix: sysfs_create_group failed: %d\n", rc);

	return rc;

}

module_init(touch_wake_fix_init_sysfs);
