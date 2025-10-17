/*
 * This file is part of the libserialport project.
 *
 * Copyright (C) 2013 Martin Ling <martin-libserialport@earth.li>
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "libserialport.h"
#include "libserialport_internal.h"
#include <android/log.h>

#define LOG_TAG "FLUTTER_LIBSERIALPORT"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Function declarations */
SP_PRIV enum sp_return list_ports_fallback(struct sp_port ***list);


/*
 * The 'e' modifier for O_CLOEXEC is glibc >= 2.7 only, hence not
 * portable, so provide an own wrapper for this functionality.
 */
static FILE *fopen_cloexec_rdonly(const char *pathname)
{
	int fd;
	if ((fd = open(pathname, O_RDONLY | O_CLOEXEC)) < 0)
		return NULL;
	return fdopen(fd, "r");
}

SP_PRIV enum sp_return get_port_details(struct sp_port *port)
{
	LOGI("get_port_details() called for port: %s", port->name);
	
	/*
	 * Description limited to 127 char, anything longer
	 * would not be user friendly anyway.
	 */
	char description[128];
	int bus, address;
	unsigned int vid, pid;
	char manufacturer[128], product[128], serial[128];
	char baddr[32];
	const char dir_name[] = "/sys/class/tty/%s/device/%s%s";
	char sub_dir[32] = "", link_name[PATH_MAX], file_name[PATH_MAX];
	char *ptr, *dev = port->name + 5;
	FILE *file;
	int i, count;
	struct stat statbuf;

	if (strncmp(port->name, "/dev/", 5))
		RETURN_ERROR(SP_ERR_ARG, "Device name not recognized");

	snprintf(link_name, sizeof(link_name), "/sys/class/tty/%s", dev);
	if (lstat(link_name, &statbuf) == -1) {
		DEBUG("Cannot access sysfs, using fallback detection");
		// Fallback: try to detect transport type from device name
		if (strstr(port->name, "ttyUSB") || strstr(port->name, "ttyACM"))
			port->transport = SP_TRANSPORT_USB;
		else if (strstr(port->name, "rfcomm"))
			port->transport = SP_TRANSPORT_BLUETOOTH;
		else
			port->transport = SP_TRANSPORT_NATIVE;
		
		// Set basic description
		port->description = strdup(port->name);
		RETURN_OK();
	}
	if (!S_ISLNK(statbuf.st_mode))
		snprintf(link_name, sizeof(link_name), "/sys/class/tty/%s/device", dev);
	count = readlink(link_name, file_name, sizeof(file_name));
	if (count <= 0 || count >= (int)(sizeof(file_name) - 1)) {
		DEBUG("Cannot read sysfs link, using fallback detection");
		// Fallback: try to detect transport type from device name
		if (strstr(port->name, "ttyUSB") || strstr(port->name, "ttyACM"))
			port->transport = SP_TRANSPORT_USB;
		else if (strstr(port->name, "rfcomm"))
			port->transport = SP_TRANSPORT_BLUETOOTH;
		else
			port->transport = SP_TRANSPORT_NATIVE;
		
		// Set basic description
		port->description = strdup(port->name);
		RETURN_OK();
	}
	file_name[count] = 0;
	if (strstr(file_name, "bluetooth"))
		port->transport = SP_TRANSPORT_BLUETOOTH;
	else if (strstr(file_name, "usb"))
		port->transport = SP_TRANSPORT_USB;

	if (port->transport == SP_TRANSPORT_USB) {
		for (i = 0; i < 5; i++) {
			strcat(sub_dir, "../");

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "busnum");
			if (!(file = fopen_cloexec_rdonly(file_name))) {
				DEBUG("Cannot access USB device info from sysfs, using fallback");
				// Fallback: set basic USB device info
				port->description = strdup(port->name);
				port->usb_manufacturer = strdup("Unknown");
				port->usb_product = strdup("USB Serial Device");
				port->usb_serial = strdup("Unknown");
				RETURN_OK();
			}
			count = fscanf(file, "%d", &bus);
			fclose(file);
			if (count != 1)
				continue;

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "devnum");
			if (!(file = fopen_cloexec_rdonly(file_name)))
				continue;
			count = fscanf(file, "%d", &address);
			fclose(file);
			if (count != 1)
				continue;

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "idVendor");
			if (!(file = fopen_cloexec_rdonly(file_name)))
				continue;
			count = fscanf(file, "%4x", &vid);
			fclose(file);
			if (count != 1)
				continue;

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "idProduct");
			if (!(file = fopen_cloexec_rdonly(file_name)))
				continue;
			count = fscanf(file, "%4x", &pid);
			fclose(file);
			if (count != 1)
				continue;

			port->usb_bus = bus;
			port->usb_address = address;
			port->usb_vid = vid;
			port->usb_pid = pid;

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "product");
			if ((file = fopen_cloexec_rdonly(file_name))) {
				if ((ptr = fgets(description, sizeof(description), file))) {
					ptr = description + strlen(description) - 1;
					if (ptr >= description && *ptr == '\n')
						*ptr = 0;
					port->description = strdup(description);
				}
				fclose(file);
			}
			if (!file || !ptr)
				port->description = strdup(dev);

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "manufacturer");
			if ((file = fopen_cloexec_rdonly(file_name))) {
				if ((ptr = fgets(manufacturer, sizeof(manufacturer), file))) {
					ptr = manufacturer + strlen(manufacturer) - 1;
					if (ptr >= manufacturer && *ptr == '\n')
						*ptr = 0;
					port->usb_manufacturer = strdup(manufacturer);
				}
				fclose(file);
			}

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "product");
			if ((file = fopen_cloexec_rdonly(file_name))) {
				if ((ptr = fgets(product, sizeof(product), file))) {
					ptr = product + strlen(product) - 1;
					if (ptr >= product && *ptr == '\n')
						*ptr = 0;
					port->usb_product = strdup(product);
				}
				fclose(file);
			}

			snprintf(file_name, sizeof(file_name), dir_name, dev, sub_dir, "serial");
			if ((file = fopen_cloexec_rdonly(file_name))) {
				if ((ptr = fgets(serial, sizeof(serial), file))) {
					ptr = serial + strlen(serial) - 1;
					if (ptr >= serial && *ptr == '\n')
						*ptr = 0;
					port->usb_serial = strdup(serial);
				}
				fclose(file);
			}

			/* If present, add serial to description for better identification. */
			if (port->usb_serial && strlen(port->usb_serial)) {
				snprintf(description, sizeof(description),
					"%s - %s", port->description, port->usb_serial);
				if (port->description)
					free(port->description);
				port->description = strdup(description);
			}

			break;
		}
	} else {
		port->description = strdup(dev);

		if (port->transport == SP_TRANSPORT_BLUETOOTH) {
			snprintf(file_name, sizeof(file_name), dir_name, dev, "", "address");
			if ((file = fopen_cloexec_rdonly(file_name))) {
				if ((ptr = fgets(baddr, sizeof(baddr), file))) {
					ptr = baddr + strlen(baddr) - 1;
					if (ptr >= baddr && *ptr == '\n')
						*ptr = 0;
					port->bluetooth_address = strdup(baddr);
				}
				fclose(file);
			}
		}
	}

	LOGI("get_port_details() completed for port: %s", port->name);
	RETURN_OK();
}

SP_PRIV enum sp_return list_ports(struct sp_port ***list)
{
	// Add detailed logging for debugging (will show in Android logcat)
	LOGI("========================================");
	LOGI("list_ports() called");
	LOGI("Starting /dev direct enumeration (SELinux-safe)");
	LOGI("This avoids all sysfs access");
	LOGI("========================================");
	DEBUG("Enumerating tty devices using /dev direct enumeration");
	
	// For new devices with SELinux restrictions, use direct /dev enumeration
	// This avoids all sysfs access that triggers SELinux errors
	return list_ports_fallback(list);
}

/* Direct /dev enumeration for devices with SELinux restrictions */
SP_PRIV enum sp_return list_ports_fallback(struct sp_port ***list)
{
	DIR *dir;
	struct dirent *entry;
	char name[PATH_MAX];
	int ret = SP_OK;
	struct stat statbuf;

	LOGI("Scanning /dev for serial devices");
	LOGI("Opening /dev directory...");
	DEBUG("Direct enumeration: Scanning /dev for serial devices");
	if (!(dir = opendir("/dev"))) {
		LOGE("ERROR - Could not open /dev directory! Error code: %d", errno);
		RETURN_FAIL("Could not open /dev");
	}
	LOGI("Successfully opened /dev directory");

	LOGI("Starting to iterate over /dev entries...");
	DEBUG("Iterating over /dev entries");
	int total_entries = 0;
	int matching_entries = 0;
	
	while ((entry = readdir(dir))) {
		total_entries++;
		
		// Look for common serial device patterns
		if (strncmp(entry->d_name, "ttyUSB", 6) == 0 ||    // USB serial devices
		    strncmp(entry->d_name, "ttyACM", 6) == 0 ||    // USB CDC devices
		    strncmp(entry->d_name, "ttyS", 4) == 0 ||      // Serial ports
		    strncmp(entry->d_name, "ttyAMA", 6) == 0 ||    // ARM serial ports
		    strncmp(entry->d_name, "ttyXR", 5) == 0 ||     // XR serial ports
		    strncmp(entry->d_name, "rfcomm", 6) == 0) {   // Bluetooth serial
			
			matching_entries++;
			snprintf(name, sizeof(name), "/dev/%s", entry->d_name);
			LOGI("Found potential device: %s", name);
			DEBUG_FMT("Found potential device %s", name);
			
			// Check if device exists and is accessible
			LOGI("Checking device: %s", name);
			if (stat(name, &statbuf) == -1) {
				LOGE("Stat failed for %s (errno: %d)", name, errno);
				DEBUG_FMT("Stat failed for %s", name);
				continue;
			}
			LOGI("Stat successful for %s", name);
			
			if (!S_ISCHR(statbuf.st_mode)) {
				LOGI("Not a character device: %s", name);
				DEBUG_FMT("Not a character device: %s", name);
				continue;
			}
			LOGI("Is a character device: %s", name);
			
			// Try to open the device to verify it's accessible
			LOGI("Attempting to open: %s", name);
			int fd = open(name, O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
			if (fd < 0) {
				LOGE("Open failed for %s: %s (errno: %d)", name, strerror(errno), errno);
				DEBUG_FMT("Open failed for %s: %s", name, strerror(errno));
				continue;
			}
			close(fd);
			LOGI("Successfully opened and closed: %s", name);
			
			LOGI("Adding to port list: %s", name);
			DEBUG_FMT("Found accessible port %s", name);
			*list = list_append(*list, name);
			if (!*list) {
				LOGE("ERROR - List append failed!");
				SET_ERROR(ret, SP_ERR_MEM, "List append failed");
				break;
			}
			LOGI("Successfully added to list: %s", name);
		}
	}
	closedir(dir);

	LOGI("Finished iterating /dev directory");
	LOGI("Total entries scanned: %d", total_entries);
	LOGI("Matching entries found: %d", matching_entries);

	// Count the number of ports found
	int port_count = 0;
	if (*list) {
		struct sp_port **current = *list;
		while (*current) {
			port_count++;
			current++;
		}
	}
	
	LOGI("========================================");
	LOGI("FINAL RESULT");
	LOGI("Direct enumeration found %d accessible ports", port_count);
	LOGI("Return code: %d", ret);
	LOGI("========================================");
	DEBUG_FMT("Direct enumeration found %d ports", port_count);
	return ret;
}
