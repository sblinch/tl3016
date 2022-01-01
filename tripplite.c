// SPDX-License-Identifier: GPL-2.0
/*
 * Tripp-Lite SMART1500LCDT (09AE:3016) Brute-Force Poller
 * Copyright 2022, Steve Blinch
 *
 * Includes code from https://github.com/ralight/usb-reset
 * Copyright (c) 2017 Roger Light <roger@atchoo.org>.
 *
 * Based on Hidraw Userspace Example
 * Copyright (c) 2010 Alan Ott <alan@signal11.us>
 * Copyright (c) 2010 Signal 11 Software
 *
 *
 * LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* Linux */
#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>

/* Unix */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <libusb.h>

const int TL_VENDOR = 0x09ae;
const int TL_PRODUCT = 0x3016;

int libusb_initialized = 0;

int debug_mode = 0;

// these are global because we frequently need to close/reopen the hidraw device and/or reset the device entirely during
// arbitrary function calls, and this code is a steaming pile of horse puckie anyway
int fd = 0;
char *device = NULL;

// reset_by_vid_pid from https://github.com/ralight/usb-reset/blob/master/usb-reset.c
// Copyright (c) 2017 Roger Light <roger@atchoo.org>. MIT License.
int reset_by_vid_pid(int vid, int pid) {
	int rc = 0;
	struct libusb_device_handle *handle;

	if (libusb_initialized == 0) {
		rc = libusb_init(NULL);
		if (rc) {
			printf("failed to initialize libusb: %d\n", rc);
			return 1;
		}
		libusb_initialized = 1;
	}

	handle = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!handle) {
		printf("cannot open %08x:%08x\n", vid, pid);
		return 1;
	}

	if (libusb_reset_device(handle)) {
		printf("Reset failed, you may need to replug your device.\n");
		rc = 1;
	}

	libusb_close(handle);

	return rc;
}

int openfd() {
	fd = open(device, O_RDWR | O_NONBLOCK);

	if (fd < 0) {
		perror("Unable to open device");
		// if we can't open the device, there's a good chance that either the device needs to be reset or the USB port needs
		// to be power cycled; only the former is practical to accomplish from here, so we try that before bailing
		if (reset_by_vid_pid(TL_VENDOR, TL_PRODUCT) != 0) {
			printf("unable to reset\n");
			return 1;
		}

		fd = open(device, O_RDWR | O_NONBLOCK);
		if (fd < 0) {
			perror("Unable to open device");
			return 1;
		}
	}

	return 0;
}

int reopenfd() {
	close(fd);
	return openfd();
}

int getReport(int fd, int report_id, char *buf, int buflen) {
	if (buflen < 2) {
		return -1;
	}

	buf[0] = (unsigned char) report_id;
	int res = ioctl(fd, HIDIOCGFEATURE(buflen), buf);
	if (res < 0) {
		if (debug_mode > 0) perror("HIDIOCGFEATURE");
		return -1;
	} else {
		if (debug_mode > 0) {
			printf("report %d: %d bytes; ", report_id, res);
			for (int i = 0; i < res; i++)
				printf("%02hhx ", buf[i]);
			puts("\n");
		}

		return res;
	}
}

int retryGetReport(int fd, int report_id, char *buf, int buflen) {
	// the UPS returns EPIPE errors seemingly randomly, often 2-3 at a time, so we retry several times in the event of failure
	int attempts = 10;

	for (int i = 0; i < attempts; i++) {
		int res = getReport(fd, report_id, buf, buflen);
		if (res != -1) {
			return res;
		}

		if (i == attempts / 2) {
			// if first half our attempts fail, try closing and reopening the hidraw device (resetting the device if needed)
			// before making the remaining attempts
			if (reopenfd() != 0) {
				return -1;
			}
		}
	}

	return -1;
}

int getReport8Bit(int fd, int report_id) {
	char buf[128];
	int len = retryGetReport(fd, report_id, (char *) &buf, sizeof(buf));
	if (len < 0) {
		return len;
	}
	return (int) buf[1];
}

int getReport16Bit(int fd, int report_id) {
	char buf[128];
	int len = retryGetReport(fd, report_id, (char *) &buf, sizeof(buf));
	if (len < 0) {
		return len;
	}

	int16_t *i = (int16_t * ) & buf[1];
	return (int) (*i);
}

int getRuntimeToEmpty(int fd) {
	return getReport16Bit(fd, 53);
}

int getRemainingCapacityPercent(int fd) {
	int remainingCapacity = getReport8Bit(fd, 52);
	if (remainingCapacity < 0) {
		return remainingCapacity;
	}
	int totalCapacity = getReport8Bit(fd, 54);
	if (totalCapacity < 0) {
		return totalCapacity;
	}

	return remainingCapacity * 100 / totalCapacity;
}

int getInputVoltage(int fd) {
	return getReport16Bit(fd, 24);
}

int getOutputVoltage(int fd) {
	return getReport16Bit(fd, 27);
}

int getLoadPercentage(int fd) {
	return getReport8Bit(fd, 30);
}

int getPresentPowerStatus(int fd) {
	return getReport16Bit(fd, 34);
}

int getPresentBatteryStatus(int fd) {
	return getReport16Bit(fd, 35);
}

int main(int argc, char **argv) {
	int res;
	struct hidraw_devinfo info;

	int argn = 1;
	if (argc > argn && strcmp(argv[argn], "--debug") == 0) {
		debug_mode = 1;
		argn++;
	}

	if (argc > argn) {
		device = argv[argn];
		if (openfd() != 0) {
			return 1;
		}

	} else {

		struct stat statbuf;

		char devicebuf[32];
		device = (char *) &devicebuf;

		// there are undoubtedly better ways to enumerate the hidraw devices, but this approach better fits the
		// "utter shitshow" theme of this project
		for (int i = 0; i < 10; i++) {
			sprintf(device, "/dev/hidraw%d", i);
			if (stat(device, &statbuf) != 0) {
				continue;
			}

			if (debug_mode > 0) printf("Testing device %s ...\n", device);
			if (openfd() != 0) {
				continue;
			}

			res = ioctl(fd, HIDIOCGRAWINFO, &info);
			if (res == 0) {
				if (info.vendor == TL_VENDOR || info.product == TL_PRODUCT) {
					if (debug_mode > 0) printf("Found dodgy Tripp-Lite UPS at %s\n", device);
					break;
				}
			}
			close(fd);
			fd = 0;
		}

		if (fd == 0) {
			printf("Cannot find Tripp-Lite UPS device /dev/hidraw*\n");
			return 1;
		}
	}


	int secs = -1;
	int battery_pct = -1;
	int inv = -1;
	int outv = -1;
	int load_pct = -1;
	int pstatus = -1;
	int bstatus = -1;

	int done = 0;

	// As described in the header comments, when encountering a persistent EPIPE, the UPS seems to like it if we switch up the
	// report ID we're requesting. Thus, if we fail to fetch a particular datapoint (bearing in mind that each getXXX(fd) call
	// will also retry the datapoint several times), we skip it and move on to the next. Then we take another crack at it on the
	// next loop iteration, and pray that by the final loop iteration we manage to catch'em all.
	for (int attempts = 0; attempts < 10; attempts++) {

		if (secs < 0) {
			secs = getRuntimeToEmpty(fd);
			if (secs >= 0 && debug_mode > 0) printf("runtime to empty: %d mins\n", secs / 60);
		}

		if (battery_pct < 0) {
			battery_pct = getRemainingCapacityPercent(fd);
			if (battery_pct >= 0 && debug_mode > 0) printf("remaining capacity: %d%%\n", battery_pct);
		}

		if (inv < 0) {
			inv = getInputVoltage(fd);
			if (inv >= 0 && debug_mode > 0) printf("input voltage: %d.%d\n", inv / 10, inv % 10);
		}

		if (outv < 0) {
			outv = getOutputVoltage(fd);
			if (outv >= 0 && debug_mode > 0) printf("input voltage: %d.%d\n", outv / 10, outv % 10);
		}


		if (load_pct < 0) {
			load_pct = getLoadPercentage(fd);
			if (load_pct >= 0 && debug_mode > 0) printf("current load: %d%%\n", load_pct);
		}

		if (pstatus < 0) {
			pstatus = getPresentPowerStatus(fd);
			if (pstatus >= 0 && debug_mode > 0) {
				printf("power status: ");
				if (pstatus > 0) {
					if ((pstatus & 1) != 0) printf("voltage out of range  ");
					if ((pstatus & 2) != 0) printf("buck  ");
					if ((pstatus & 4) != 0) printf("boost  ");
					if ((pstatus & 8) != 0) printf("undefined-3  ");
					if ((pstatus & 16) != 0) printf("overload  ");
					if ((pstatus & 32) != 0) printf("ups off  ");
					if ((pstatus & 64) != 0) printf("over temperature  ");
					if ((pstatus & 128) != 0) printf("internal failure  ");
					if ((pstatus & 256) != 0) printf("undefined-8  ");
					if ((pstatus & 512) != 0) printf("reserved-9  ");
					if ((pstatus & 1024) != 0) printf("undefined-10  ");
					if ((pstatus & 2048) != 0) printf("undefined-11  ");
					if ((pstatus & 4096) != 0) printf("undefined-12  ");
					if ((pstatus & 8192) != 0) printf("undefined-13  ");
					if ((pstatus & 16384) != 0) printf("awaiting power  ");
					if ((pstatus & 32768) != 0) printf("undefined-14  ");
				}
				printf("\n");
			}
		}


		if (bstatus < 0) {
			bstatus = getPresentBatteryStatus(fd);
			if (bstatus >= 0 && debug_mode > 0) {
				printf("battery status: ");
				if (bstatus > 0) {
					if ((bstatus & 1) != 0) printf("charging  ");
					if ((bstatus & 2) != 0) printf("discharging  ");
					if ((bstatus & 4) != 0) printf("need replacement  ");
					if ((bstatus & 8) != 0) printf("reserved-3  ");
					if ((bstatus & 16) != 0) printf("reserved-4  ");
					if ((bstatus & 32) != 0) printf("reserved-5  ");
					if ((bstatus & 64) != 0) printf("reserved-6  ");
					if ((bstatus & 128) != 0) printf("reserved-7  ");
				}
				printf("\n");
			}
		}

		if (secs >= 0 && battery_pct >= 0 && inv >= 0 && outv >= 0 && load_pct >= 0 && pstatus >= 0 && bstatus >= 0) {
			done = 1;
			break;
		}

	}

	close(fd);

	// if we don't have ALL of the datapoints, we consider the whole run a failure and return a nonzero exit code; at that
	// point, the wrapper script should use uhubctl to power cycle the USB port and then run this program again
	if (done > 0) {
		int t = time(NULL);
		printf(
			"{\"runtime\":%d,\"battery_percent\":%d,\"input_voltage\":%d,\"output_voltage\":%d,\"load_percent\":%d,\"power_status\":%d,\"battery_status\":%d,\"updated\":%d}",
			secs, battery_pct, inv, outv, load_pct, pstatus, bstatus, t);
		return 0;
	} else {
		return 1;
	}
}
