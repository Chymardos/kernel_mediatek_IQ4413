/*
 * include/linux/input/doubletap2wake.h
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

#ifndef _LINUX_DOUBLETAP2WAKE_H
#define _LINUX_DOUBLETAP2WAKE_H

extern int dt2w_on;
extern bool dt2w_scr_suspended;

void doubletap2wake_setdev(struct input_dev *);

#endif	/* _LINUX_DOUBLETAP2WAKE_H */