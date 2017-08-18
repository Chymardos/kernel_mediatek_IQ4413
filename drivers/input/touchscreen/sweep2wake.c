/*
 * drivers/input/touchscreen/sweep2wake.c
 *
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com>
 * Copyright (c) 2015, Vineeth Raj <contact.twn@openmailbox.org>
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
#include <linux/input/sweep2wake.h>
#include <linux/touch_wake_fix.h>

#include <vibrator_hal.h>

#define WAKE_HOOKS_DEFINED

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
#include <linux/lcd_notify.h>
#else
#include <linux/earlysuspend.h>
#endif
#endif

#ifdef CONFIG_POCKETMOD
#include <linux/pocket_mod.h>
#endif

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

/* Tuneables */
#define S2W_DEFAULT			0 // 0 - off; 1 - on
#define S2S_DEFAULT			0 // 0 - off; 1 - on

#define S2W_INVERT_DEFAULT		0 // 0 - horizontal; 1 - vertical
#define S2W_PWRKEY_DUR			60

#define S2W_UP_BORDER_DEFAULT		0 // up border
#define S2W_DOWN_BORDER_DEFAULT		960 // down border
#define S2W_RIGHT_BORDER_DEFAULT	540 // right border
#define S2W_LEFT_BORDER_DEFAULT		0 // left border

#define S2W_UP_DEFAULT			600 // upper border trigger
#define S2W_DOWN_DEFAULT		950 // bottom border trigger
#define S2W_RIGHT_DEFAULT		420 // right border trigger
#define S2W_LEFT_DEFAULT		120 // left border trigger

#define S2S_UP_DEFAULT			600 // upper border trigger
#define S2S_DOWN_DEFAULT		950 // bottom border trigger
#define S2S_RIGHT_DEFAULT		420 // right border trigger
#define S2S_LEFT_DEFAULT		120 // left border trigger

/* Resources */
int s2w_switch = S2W_DEFAULT;
int s2s_switch = S2S_DEFAULT;

int s2w_invert = S2W_INVERT_DEFAULT;

int s2w_left_border = S2W_LEFT_BORDER_DEFAULT;
int s2w_right_border = S2W_RIGHT_BORDER_DEFAULT;
int s2w_up_border = S2W_UP_BORDER_DEFAULT;
int s2w_down_border = S2W_DOWN_BORDER_DEFAULT;

int s2w_down = S2W_DOWN_DEFAULT;
int s2w_up = S2W_UP_DEFAULT;
int s2w_right = S2W_RIGHT_DEFAULT;
int s2w_left = S2W_LEFT_DEFAULT;

int s2s_down = S2S_DOWN_DEFAULT;
int s2s_up = S2S_UP_DEFAULT;
int s2s_right = S2S_RIGHT_DEFAULT;
int s2s_left = S2S_LEFT_DEFAULT;

static int touch_x = 0, touch_y = 0;
static bool touch_x_called = false, touch_y_called = false;
static bool exec_count = true;
bool s2w_scr_suspended = false;
static bool scr_on_touch = false, barrier[2] = {false, false};
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static struct notifier_block s2w_lcd_notif;
#endif
#endif
static struct input_dev * sweep2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *s2w_input_wq;
static struct work_struct s2w_input_work;

/* PowerKey setter */
void sweep2wake_setdev(struct input_dev * input_device) {
	sweep2wake_pwrdev = input_device;
}

/* PowerKey work func */
static void sweep2wake_presspwr(struct work_struct * sweep2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
		return;
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	if (touch_vibrate_set()) {
		vibr_Enable_HW();
		msleep(S2W_PWRKEY_DUR);
		vibr_Disable_HW();
	} else {
		msleep(S2W_PWRKEY_DUR);
	}
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	msleep(S2W_PWRKEY_DUR);
	mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(sweep2wake_presspwr_work, sweep2wake_presspwr);

/* PowerKey trigger */
static void sweep2wake_pwrtrigger(void) {
	#ifdef CONFIG_POCKETMOD
	if (device_is_pocketed()) {
	return;
	}
	else
	#endif
	schedule_work(&sweep2wake_presspwr_work);
	return;
}

/* reset on finger release */
static void sweep2wake_reset(void) {
	exec_count = true;
	barrier[0] = false;
	barrier[1] = false;
	scr_on_touch = false;
}

/* Sweep2wake main function */
static void detect_sweep2wake(int x, int y, bool st)
{
	int prevx = 0, nextx = 0, prevy = 0, nexty = 0;
	bool single_touch = st;
	//left->right unlock
	if ((single_touch) && (s2w_scr_suspended == true) && (s2w_switch == 1) && (s2w_invert == 0)) {
		prevx = s2w_left_border;
		nextx = s2w_left;
		if ((barrier[0] == true) ||
		   ((x > prevx) &&
		    (x < nextx) &&
		    (y > s2w_up_border) &&
		    (y < s2w_down_border))) {
			prevx = nextx;
			nextx = s2w_right;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x > prevx) &&
			    (x < nextx) &&
			    (y > s2w_up_border) &&
			    (y < s2w_down_border))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x > prevx) &&
				    (y > s2w_up_border) &&
				    (y < s2w_down_border)) {
					if (exec_count) {
						sweep2wake_pwrtrigger();
						exec_count = false;
					}
				}
			}
		}
	//right->left lock
	} else if ((single_touch) && (s2w_scr_suspended == false) && (s2s_switch == 1) && (s2w_invert == 0)) {
		scr_on_touch = true;
		prevx = s2w_right_border;
		nextx = s2s_right;
		if ((barrier[0] == true) ||
		   ((x < prevx) &&
		    (x > nextx) &&
		    (y > s2w_up_border) &&
		    (y < s2w_down_border))) {
			prevx = nextx;
			nextx = s2s_left;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((x < prevx) &&
			    (x > nextx) &&
			    (y > s2w_up_border) &&
			    (y < s2w_down_border))) {
				prevx = nextx;
				barrier[1] = true;
				if ((x < prevx) &&
				    (y > s2w_up_border) &&
				    (y < s2w_down_border)) {
					if (exec_count) {
						sweep2wake_pwrtrigger();
						exec_count = false;
					}
				}
			}
		}
	//up->down unlock
	} else if ((single_touch) && (s2w_scr_suspended == true) && (s2w_switch == 1) && (s2w_invert == 1)) {
		prevy = s2w_up_border;
		nexty = s2w_up;
		if ((barrier[0] == true) ||
		   ((y > prevy) &&
		    (y < nexty) &&
		    (x > s2w_left_border) &&
		    (x < s2w_right_border))) {
			prevy = nexty;
			nexty = s2w_down;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((y > prevy) &&
			    (y < nexty) &&
			    (x > s2w_left_border) &&
			    (x < s2w_right_border))) {
				prevy = nexty;
				barrier[1] = true;
				if ((y > prevy) &&
				    (x > s2w_left_border) &&
				    (x < s2w_right_border)) {
					if (exec_count) {
						sweep2wake_pwrtrigger();
						exec_count = false;
					}
				}
			}
		}
	//down->up lock
	} else if ((single_touch) && (s2w_scr_suspended == false) && (s2s_switch == 1) && (s2w_invert == 1)) {
		scr_on_touch = true;
		prevy = s2w_down_border;
		nexty = s2s_down;
		if ((barrier[0] == true) ||
		   ((y < prevy) &&
		    (y > nexty) &&
		    (x > s2w_left_border) &&
		    (x < s2w_right_border))) {
			prevy = nexty;
			nexty = s2s_up;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((y < prevy) &&
			    (y > nexty) &&
			    (x > s2w_left_border) &&
			    (x < s2w_right_border))) {
				prevy = nexty;
				barrier[1] = true;
				if ((y < prevy) &&
				    (x > s2w_left_border) &&
				    (x < s2w_right_border)) {
					if (exec_count) {
						sweep2wake_pwrtrigger();
						exec_count = false;
					}
				}
			}
		}
	}
}

static void s2w_input_callback(struct work_struct *unused) {

// unlock
	if ((s2w_scr_suspended == true) &&
	    (s2w_invert == 0) &&
	    (touch_y < s2w_down) &&
	    (touch_y > s2w_up)) {
	detect_sweep2wake(touch_x, touch_y, true);
// unlock invert
	} else if ((s2w_scr_suspended == true) &&
	    (s2w_invert == 1) &&
	    (touch_x < s2w_right) &&
	    (touch_x > s2w_left)) {
	detect_sweep2wake(touch_x, touch_y, true);
// lock
	} else if ((s2w_scr_suspended == false) &&
	    (s2w_invert == 0) &&
	    (touch_y < s2s_down) &&
	    (touch_y > s2s_up)) {
	detect_sweep2wake(touch_x, touch_y, true);
// lock invert
	} else if ((s2w_scr_suspended == false) &&
	    (s2w_invert == 1) &&
	    (touch_x < s2s_right) &&
	    (touch_x > s2s_left)) {
	detect_sweep2wake(touch_x, touch_y, true);
	}
	return;
}

static void s2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {

	if (code == ABS_MT_SLOT) {
		sweep2wake_reset();
		return;
	}

	/*
	 * '330'? Many touch panels are 'broken' in the sense of not following the
	 * multi-touch protocol given in Documentation/input/multi-touch-protocol.txt.
	 * According to the docs, touch panels using the type B protocol must send in
	 * a ABS_MT_TRACKING_ID event after lifting the contact in the first slot.
	 * This should in the flow of events, help us reset the sweep2wake variables
	 * and proceed as per the algorithm.
	 *
	 * This however is not the case with various touch panel drivers, and hence
	 * there is no reliable way of tracking ABS_MT_TRACKING_ID on such panels.
	 * Some of the panels however do track the lifting of contact, but with a
	 * different event code, and a different event value.
	 *
	 * So, add checks for those event codes and values to keep the algo flow.
	 *
	 * synaptics_s3203 => code: 330; val: 0
	 *
	 * Note however that this is not possible with panels like the CYTTSP3 panel
	 * where there are no such events being reported for the lifting of contacts
	 * though i2c data has a ABS_MT_TRACKING_ID or equivalent event variable
	 * present. In such a case, make sure the sweep2wake_reset() function is
	 * publicly available for external calls.
	 *
	 */
	if ((code == ABS_MT_TRACKING_ID && value == -1) ||
		(code == 330 && value == 0)) {
		sweep2wake_reset();
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called && touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, s2w_input_wq, &s2w_input_work);
	}
}

static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "touch")||
			strstr(dev->name, "mtk-tpd")) {
		return 0;
	} else {
		return 1;
	}
}

static int s2w_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "s2w";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void s2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id s2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler s2w_input_handler = {
	.event		= s2w_input_event,
	.connect	= s2w_input_connect,
	.disconnect	= s2w_input_disconnect,
	.name		= "s2w_inputreq",
	.id_table	= s2w_ids,
};

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_END:
		s2w_scr_suspended = false;
		break;
	case LCD_EVENT_OFF_END:
		s2w_scr_suspended = true;
		break;
	default:
		break;
	}

	return 0;
}
#else
static void s2w_early_suspend(struct early_suspend *h) {
	s2w_scr_suspended = true;
}

static void s2w_late_resume(struct early_suspend *h) {
	s2w_scr_suspended = false;
}

static struct early_suspend s2w_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = s2w_early_suspend,
	.resume = s2w_late_resume,
};
#endif
#endif

/*
 * SYSFS stuff below here
 */
// sweep2wake
static ssize_t s2w_sweep2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_switch);

	return count;
}

static ssize_t s2w_sweep2wake_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
	    if(data == 0)
		s2w_switch = 0;
	    if(data == 1)
		s2w_switch = 1;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_show, s2w_sweep2wake_dump);

// sweep2wake_invert
static ssize_t s2w_sweep2wake_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_invert);

	return count;
}

static ssize_t s2w_sweep2wake_invert_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_invert = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake_invert, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_invert_show, s2w_sweep2wake_invert_dump);

// sweep2wake_down
static ssize_t s2w_sweep2wake_down_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_down);

	return count;
}

static ssize_t s2w_sweep2wake_down_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_down = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake_down, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_down_show, s2w_sweep2wake_down_dump);

// sweep2wake_up
static ssize_t s2w_sweep2wake_up_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_up);

	return count;
}

static ssize_t s2w_sweep2wake_up_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_up = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake_up, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_up_show, s2w_sweep2wake_up_dump);

// sweep2wake_right
static ssize_t s2w_sweep2wake_right_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_right);

	return count;
}

static ssize_t s2w_sweep2wake_right_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_right = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake_right, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_right_show, s2w_sweep2wake_right_dump);

// sweep2wake_left
static ssize_t s2w_sweep2wake_left_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_left);

	return count;
}

static ssize_t s2w_sweep2wake_left_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_left = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2wake_left, (S_IWUGO|S_IRUGO),
	s2w_sweep2wake_left_show, s2w_sweep2wake_left_dump);

// sweep2sleep
static ssize_t s2w_sweep2sleep_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2s_switch);

	return count;
}

static ssize_t s2w_sweep2sleep_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
	    if(data == 0)
		s2s_switch = 0;
	    if(data == 1)
		s2s_switch = 1;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2sleep, (S_IWUGO|S_IRUGO),
	s2w_sweep2sleep_show, s2w_sweep2sleep_dump);

// sweep2sleep_down
static ssize_t s2w_sweep2sleep_down_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2s_down);

	return count;
}

static ssize_t s2w_sweep2sleep_down_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2s_down = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2sleep_down, (S_IWUGO|S_IRUGO),
	s2w_sweep2sleep_down_show, s2w_sweep2sleep_down_dump);

// sweep2sleep_up
static ssize_t s2w_sweep2sleep_up_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2s_up);

	return count;
}

static ssize_t s2w_sweep2sleep_up_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2s_up = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2sleep_up, (S_IWUGO|S_IRUGO),
	s2w_sweep2sleep_up_show, s2w_sweep2sleep_up_dump);

// sweep2sleep_right
static ssize_t s2w_sweep2sleep_right_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2s_right);

	return count;
}

static ssize_t s2w_sweep2sleep_right_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2s_right = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2sleep_right, (S_IWUGO|S_IRUGO),
	s2w_sweep2sleep_right_show, s2w_sweep2sleep_right_dump);

// sweep2sleep_left
static ssize_t s2w_sweep2sleep_left_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2s_left);

	return count;
}

static ssize_t s2w_sweep2sleep_left_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2s_left = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(sweep2sleep_left, (S_IWUGO|S_IRUGO),
	s2w_sweep2sleep_left_show, s2w_sweep2sleep_left_dump);

/*
 * INIT / EXIT stuff below here
 */
#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif
static int __init sweep2wake_init(void)
{
	int rc = 0;

	s2w_input_wq = create_workqueue("s2wiwq");
	if (!s2w_input_wq) {
		pr_err("%s: Failed to create s2wiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&s2w_input_work, s2w_input_callback);
	rc = input_register_handler(&s2w_input_handler);
	if (rc)
		pr_err("%s: Failed to register s2w_input_handler\n", __func__);

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	s2w_lcd_notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&s2w_lcd_notif) != 0) {
		pr_err("%s: Failed to register lcd callback\n", __func__);
	}
#else
	register_early_suspend(&s2w_early_suspend_handler);
#endif
#endif

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake_invert.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake_invert\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake_down.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake_down\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake_up.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake_up\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake_right.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake_right\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2wake_left.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2wake_left\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2sleep\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_down.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2sleep_down\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_up.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2sleep_up\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_right.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2sleep_right\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_sweep2sleep_left.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for sweep2sleep_left\n", __func__);
	}

	return 0;
}

static void __exit sweep2wake_exit(void)
{
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	lcd_unregister_client(&s2w_lcd_notif);
#endif
#endif
	input_unregister_handler(&s2w_input_handler);
	destroy_workqueue(s2w_input_wq);
	return;
}

module_init(sweep2wake_init);
module_exit(sweep2wake_exit);
