/*
 * drivers/input/touchscreen/doubletap2wake.c
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
#include <asm-generic/cputime.h>
#include <linux/input/doubletap2wake.h>
#include <linux/touch_wake_fix.h>

#include <vibrator_hal.h>

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

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

/* if Sweep2Wake is compiled it will already have taken care of this */
#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

/* Tuneables */
#define DT2W_ON			0
#define DT2S_ON			0

#define DT2W_TAP_DEFAULT	2
#define DT2S_TAP_DEFAULT	2

#define DT2W_PWRKEY_DUR		60

/* Doubletap2wake */
#define DT2W_ACCURACY_DEFAULT	50
#define DT2W_TIME_DEFAULT	600

#define DT2W_DOWN_DEFAULT	960
#define DT2W_UP_DEFAULT		0
#define DT2W_RIGHT_DEFAULT	540
#define DT2W_LEFT_DEFAULT	0

/* Doubletap2sleep */
#define DT2S_ACCURACY_DEFAULT	50
#define DT2S_TIME_DEFAULT	600

#define DT2S_DOWN_DEFAULT	960
#define DT2S_UP_DEFAULT		0
#define DT2S_RIGHT_DEFAULT	540
#define DT2S_LEFT_DEFAULT	0

/* Resources */
int dt2w_on = DT2W_ON;
int dt2s_on = DT2S_ON;

int dt2w_tap = DT2W_TAP_DEFAULT;
int dt2s_tap = DT2S_TAP_DEFAULT;

/* Doubletap2wake */
int dt2w_accuracy = DT2W_ACCURACY_DEFAULT;
int dt2w_time = DT2W_TIME_DEFAULT;

int dt2w_down = DT2W_DOWN_DEFAULT;
int dt2w_up = DT2W_UP_DEFAULT;
int dt2w_right = DT2W_RIGHT_DEFAULT;
int dt2w_left = DT2W_LEFT_DEFAULT;

/* Doubletap2sleep */
int dt2s_accuracy = DT2S_ACCURACY_DEFAULT;
int dt2s_time = DT2S_TIME_DEFAULT;

int dt2s_down = DT2S_DOWN_DEFAULT;
int dt2s_up = DT2S_UP_DEFAULT;
int dt2s_right = DT2S_RIGHT_DEFAULT;
int dt2s_left = DT2S_LEFT_DEFAULT;

static cputime64_t tap_time_pre = 0;
static int touch_x = 0, touch_y = 0, touch_nr = 0, x_pre = 0, y_pre = 0;
static bool touch_x_called = false, touch_y_called = false, touch_cnt = true;
static bool exec_count = true;
bool dt2w_scr_suspended = false;
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static struct notifier_block dt2w_lcd_notif;
#endif
#endif
static struct input_dev * doubletap2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *dt2w_input_wq;
static struct work_struct dt2w_input_work;

/* PowerKey setter */
void doubletap2wake_setdev(struct input_dev * input_device) {
	doubletap2wake_pwrdev = input_device;
}

/* reset on finger release */
static void doubletap2wake_reset(void) {
	exec_count = true;
	touch_nr = 0;
	tap_time_pre = 0;
	x_pre = 0;
	y_pre = 0;
}

/* PowerKey work func */
static void doubletap2wake_presspwr(struct work_struct * doubletap2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
		return;
	input_event(doubletap2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(doubletap2wake_pwrdev, EV_SYN, 0, 0);
	if (touch_vibrate_set()) {
		vibr_Enable_HW();
		msleep(DT2W_PWRKEY_DUR);
		vibr_Disable_HW();
	} else {
		msleep(DT2W_PWRKEY_DUR);
	}
	input_event(doubletap2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(doubletap2wake_pwrdev, EV_SYN, 0, 0);
	msleep(DT2W_PWRKEY_DUR);
	mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(doubletap2wake_presspwr_work, doubletap2wake_presspwr);

/* PowerKey trigger */
static void doubletap2wake_pwrtrigger(void) {
	#ifdef CONFIG_POCKETMOD
	if (device_is_pocketed()) {
	return;
	}
	else
	#endif
	schedule_work(&doubletap2wake_presspwr_work);
	return;
}

/* unsigned */
static unsigned int calc_feather(int coord, int prev_coord) {
	int calc_coord = 0;
	calc_coord = coord-prev_coord;
	if (calc_coord < 0)
		calc_coord = calc_coord * (-1);
	return calc_coord;
}

/* init a new touch */
static void new_touch(int x, int y) {
	tap_time_pre = ktime_to_ms(ktime_get());
	x_pre = x;
	y_pre = y;
	touch_nr++;
}

/* Doubletap2wake main function */
static void detect_doubletap2wake(int x, int y, bool st)
{
	bool single_touch = st;
	// unlock
	if ((single_touch) && (dt2w_scr_suspended == true) && (dt2w_on == 1) && (exec_count) && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {
			new_touch(x, y);
		} else if (touch_nr >= 1) {
			if ((calc_feather(x, x_pre) < dt2w_accuracy) &&
			    (calc_feather(y, y_pre) < dt2w_accuracy) &&
			    ((ktime_to_ms(ktime_get())-tap_time_pre) < dt2w_time))
				touch_nr++;
			else {
				doubletap2wake_reset();
				new_touch(x, y);
			}
		} else {
			doubletap2wake_reset();
			new_touch(x, y);
		}
		if ((touch_nr == dt2w_tap)) {
			exec_count = false;
			doubletap2wake_pwrtrigger();
			doubletap2wake_reset();
		}
	// lock
	} else if ((single_touch) && (dt2w_scr_suspended == false) && (dt2s_on == 1) && (exec_count) && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {
			new_touch(x, y);
		} else if (touch_nr >= 1) {
			if ((calc_feather(x, x_pre) < dt2s_accuracy) &&
			    (calc_feather(y, y_pre) < dt2s_accuracy) &&
			    ((ktime_to_ms(ktime_get())-tap_time_pre) < dt2s_time))
				touch_nr++;
			else {
				doubletap2wake_reset();
				new_touch(x, y);
			}
		} else {
			doubletap2wake_reset();
			new_touch(x, y);
		}
		if ((touch_nr == dt2s_tap)) {
			exec_count = false;
			doubletap2wake_pwrtrigger();
			doubletap2wake_reset();
		}
	}
}

static void dt2w_input_callback(struct work_struct *unused) {

	if ((dt2w_scr_suspended == true) &&
	    (touch_y < dt2w_down) &&
	    (touch_y > dt2w_up) &&
	    (touch_x < dt2w_right) &&
	    (touch_x > dt2w_left)) {
	detect_doubletap2wake(touch_x, touch_y, true);
	} else if ((dt2w_scr_suspended == false) &&
	    (touch_y < dt2s_down) &&
	    (touch_y > dt2s_up) &&
	    (touch_x < dt2s_right) &&
	    (touch_x > dt2s_left)) {
	detect_doubletap2wake(touch_x, touch_y, true);
	}
	return;
}

static void dt2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {

	if (code == ABS_MT_SLOT) {
		doubletap2wake_reset();
		return;
	}

	/*
	 * '330'? Many touch panels are 'broken' in the sense of not following the
	 * multi-touch protocol given in Documentation/input/multi-touch-protocol.txt.
	 * According to the docs, touch panels using the type B protocol must send in
	 * a ABS_MT_TRACKING_ID event after lifting the contact in the first slot.
	 * This should in the flow of events, help us set the necessary doubletap2wake
	 * variable and proceed as per the algorithm.
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
	 * present. In such a case, make sure the touch_cnt variable is publicly
	 * available for modification.
	 *
	 */
	if ((code == ABS_MT_TRACKING_ID && value == -1) || (code == 330 && value == 0)) {
		touch_cnt = true;
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

	if (touch_x_called || touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, dt2w_input_wq, &dt2w_input_work);
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

static int dt2w_input_connect(struct input_handler *handler,
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
	handle->name = "dt2w";

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

static void dt2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dt2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler dt2w_input_handler = {
	.event		= dt2w_input_event,
	.connect	= dt2w_input_connect,
	.disconnect	= dt2w_input_disconnect,
	.name		= "dt2w_inputreq",
	.id_table	= dt2w_ids,
};

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_END:
		dt2w_scr_suspended = false;
		break;
	case LCD_EVENT_OFF_END:
		dt2w_scr_suspended = true;
		break;
	default:
		break;
	}

	return 0;
}
#else
static void dt2w_early_suspend(struct early_suspend *h) {
	dt2w_scr_suspended = true;
}

static void dt2w_late_resume(struct early_suspend *h) {
	dt2w_scr_suspended = false;
}

static struct early_suspend dt2w_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = dt2w_early_suspend,
	.resume = dt2w_late_resume,
};
#endif
#endif

/*
 * SYSFS stuff below here
 */

// doubletap2wake
static ssize_t dt2w_doubletap2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_on);

	return count;
}

static ssize_t dt2w_doubletap2wake_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if((sscanf(buf, "%i\n", &data) == 1))
	    if((data == 1) || (data == 0))
		dt2w_on = data;
	    else
		dt2w_on = 0;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_show, dt2w_doubletap2wake_dump);

// doubletap2wake_tap
static ssize_t dt2w_doubletap2wake_tap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_tap);

	return count;
}

static ssize_t dt2w_doubletap2wake_tap_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if((sscanf(buf, "%i\n", &data) == 1))
	    if(data > 1)
		dt2w_tap = data;
	    if(data == 1)
		dt2w_tap = 2;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_tap, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_tap_show, dt2w_doubletap2wake_tap_dump);

// doubletap2wake_accuracy
static ssize_t dt2w_doubletap2wake_accuracy_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_accuracy);

	return count;
}

static ssize_t dt2w_doubletap2wake_accuracy_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_accuracy = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_accuracy, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_accuracy_show, dt2w_doubletap2wake_accuracy_dump);

// doubletap2wake_time
static ssize_t dt2w_doubletap2wake_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_time);

	return count;
}

static ssize_t dt2w_doubletap2wake_time_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_time = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_time, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_time_show, dt2w_doubletap2wake_time_dump);

// doubletap2wake_down
static ssize_t dt2w_doubletap2wake_down_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_down);

	return count;
}

static ssize_t dt2w_doubletap2wake_down_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_down = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_down, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_down_show, dt2w_doubletap2wake_down_dump);

// doubletap2wake_up
static ssize_t dt2w_doubletap2wake_up_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_up);

	return count;
}

static ssize_t dt2w_doubletap2wake_up_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_up = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_up, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_up_show, dt2w_doubletap2wake_up_dump);

// doubletap2wake_right
static ssize_t dt2w_doubletap2wake_right_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_right);

	return count;
}

static ssize_t dt2w_doubletap2wake_right_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_right = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_right, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_right_show, dt2w_doubletap2wake_right_dump);

// doubletap2wake_left
static ssize_t dt2w_doubletap2wake_left_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_left);

	return count;
}

static ssize_t dt2w_doubletap2wake_left_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2w_left = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2wake_left, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2wake_left_show, dt2w_doubletap2wake_left_dump);

// doubletap2sleep
static ssize_t dt2w_doubletap2sleep_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_on);

	return count;
}

static ssize_t dt2w_doubletap2sleep_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if((sscanf(buf, "%i\n", &data) == 1))
	    if((data == 1) || (data == 0))
		dt2s_on = data;
	    else
		dt2s_on = 0;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_show, dt2w_doubletap2sleep_dump);

// doubletap2sleep_tap
static ssize_t dt2w_doubletap2sleep_tap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_tap);

	return count;
}

static ssize_t dt2w_doubletap2sleep_tap_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if((sscanf(buf, "%i\n", &data) == 1))
	    if(data > 1)
		dt2s_tap = data;
	    if(data == 1)
		dt2s_tap = 2;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_tap, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_tap_show, dt2w_doubletap2sleep_tap_dump);

// doubletap2sleep_accuracy
static ssize_t dt2w_doubletap2sleep_accuracy_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_accuracy);

	return count;
}

static ssize_t dt2w_doubletap2sleep_accuracy_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_accuracy = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_accuracy, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_accuracy_show, dt2w_doubletap2sleep_accuracy_dump);

// doubletap2sleep_time
static ssize_t dt2w_doubletap2sleep_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_time);

	return count;
}

static ssize_t dt2w_doubletap2sleep_time_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_time = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_time, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_time_show, dt2w_doubletap2sleep_time_dump);

// doubletap2sleep_down
static ssize_t dt2w_doubletap2sleep_down_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_down);

	return count;
}

static ssize_t dt2w_doubletap2sleep_down_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_down = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_down, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_down_show, dt2w_doubletap2sleep_down_dump);

// doubletap2sleep_up
static ssize_t dt2w_doubletap2sleep_up_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_up);

	return count;
}

static ssize_t dt2w_doubletap2sleep_up_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_up = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_up, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_up_show, dt2w_doubletap2sleep_up_dump);

// doubletap2sleep_right
static ssize_t dt2w_doubletap2sleep_right_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_right);

	return count;
}

static ssize_t dt2w_doubletap2sleep_right_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_right = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_right, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_right_show, dt2w_doubletap2sleep_right_dump);

// doubletap2sleep_left
static ssize_t dt2w_doubletap2sleep_left_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2s_left);

	return count;
}

static ssize_t dt2w_doubletap2sleep_left_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		dt2s_left = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static DEVICE_ATTR(doubletap2sleep_left, (S_IWUGO|S_IRUGO),
	dt2w_doubletap2sleep_left_show, dt2w_doubletap2sleep_left_dump);

/*
 * INIT / EXIT stuff below here
 */
#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif
static int __init doubletap2wake_init(void)
{
	int rc = 0;

	dt2w_input_wq = create_workqueue("dt2wiwq");
	if (!dt2w_input_wq) {
		pr_err("%s: Failed to create dt2wiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&dt2w_input_work, dt2w_input_callback);
	rc = input_register_handler(&dt2w_input_handler);
	if (rc)
		pr_err("%s: Failed to register dt2w_input_handler\n", __func__);

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	dt2w_lcd_notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&dt2w_lcd_notif) != 0) {
		pr_err("%s: Failed to register lcd callback\n", __func__);
	}
#else
	register_early_suspend(&dt2w_early_suspend_handler);
#endif
#endif

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_tap.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_tap\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_accuracy.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_accuracy\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_time.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_time\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_down.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_down\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_up.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_up\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_right.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_right\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake_left.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake_left\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_tap.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_tap\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_accuracy.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_accuracy\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_time.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_time\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_down.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_down\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_up.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_up\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_right.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_right\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2sleep_left.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2sleep_left\n", __func__);
	}

	return 0;
}

static void __exit doubletap2wake_exit(void)
{
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	lcd_unregister_client(&dt2w_lcd_notif);
#endif
#endif
	input_unregister_handler(&dt2w_input_handler);
	destroy_workqueue(dt2w_input_wq);
	return;
}

module_init(doubletap2wake_init);
module_exit(doubletap2wake_exit);
