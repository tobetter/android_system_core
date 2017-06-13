/*
 * Copyright (C) 2017 Dongjin Kim <tobetter@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <termios.h>

#include <libxml/xmlreader.h>

#include "log.h"

#define GPS_DEVICE_LIST_FILE	"odroid-usbgps.xml"

struct gpsdevice {
    uint16_t vid;
    uint16_t pid;
    speed_t baudrate;
};

static struct gpsdevice **devices = NULL;
static int nr_devices = 0;
static int last = 0;

static char *__default_device = NULL;
static speed_t __default_baudrate = B9600;

static int __device_create_pool(int nr)
{
    if (0 >= nr)
        return -EINVAL;

    size_t size = sizeof(struct gpsdevice*) * (nr + 1);
    devices = (struct gpsdevice**)malloc(size);
    if (NULL == devices)
        return -ENOMEM;

    memset(devices, 0, size);

    nr_devices = nr;
    last = 0;

    return 0;
}

#define ARRAY_SIZE(x)		(int)(sizeof(x) / sizeof(x[0]))

static const struct {
    const char* str;
    speed_t baudrate;
} termbits[] = {
    { "2400", B2400 },
    { "4800", B4800 },
    { "9600", B9600 },
    { "115200", B115200 },
};

static speed_t __termbits(const char* str)
{
    if (str) {
        int i;
        for (i = 0; i < ARRAY_SIZE(termbits); i++) {
            if (0 == strcmp(termbits[i].str, str))
                return termbits[i].baudrate;
        }
    }

    return __default_baudrate;
}

static const char* nameof_termbits(speed_t baudrate)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(termbits); i++) {
        if (termbits[i].baudrate == baudrate)
            return termbits[i].str;
    }

    return "unknown";
}

static int __device_add(xmlNode *node)
{
    int ret = 0;
    struct gpsdevice *dev = NULL;

    if (NULL == node)
        return -EINVAL;

    /* USB device attributes */
    char *vid = (char*)xmlGetProp(node, (xmlChar*)"vid");
    char *pid = (char*)xmlGetProp(node, (xmlChar*)"pid");
    char *baudrate = (char*)xmlGetProp(node,
            (xmlChar*)"baudrate");

    /* idVendor and idProduct must be specified */
    if ((NULL == vid) || (NULL == pid)) {
        ERROR("idVendor or idProduct is missing\n");
        ret = -EINVAL;
        goto exit_device_add;
    }

    dev = (struct gpsdevice*)malloc(sizeof(struct gpsdevice));
    if (NULL == dev) {
        ret = -EINVAL;
        goto exit_device_add;
    }

    /* convert string value to interger */
    dev->vid = strtol(vid, NULL, 16);       /* idVendor */
    dev->pid = strtol(pid, NULL, 16);       /* idProduct */
    dev->baudrate = __termbits(baudrate);	/* Baudrate */

    devices[last++] = dev;

exit_device_add:
    if (vid)
        xmlFree(vid);
    if (pid)
        xmlFree(pid);
    if (baudrate)
        xmlFree(baudrate);

    return ret;
}

static int set_default_device(const char *device)
{
    char *buf = NULL;

    if (device) {
        buf = strdup(device);
        if (NULL == buf)
            return -ENOMEM;
    }

    if (__default_device)
        free(__default_device);

    __default_device = buf;

    return 0;
}

static int set_default_baudrate(const char *baudrate)
{
    if (NULL == baudrate)
        return -EINVAL;

    __default_baudrate = __termbits(baudrate);

    return 0;
}

static int device_traverse_trees(xmlNode *node)
{
    if (NULL == node)
        return -EINVAL;

    xmlNode *curr = node;

    for (; curr; curr = curr->next) {
        if (curr->type == XML_ELEMENT_NODE) {
            if (0 == xmlStrcmp(curr->name, (xmlChar*)"default")) {
                char *device = (char*)xmlGetProp(curr,
                        (xmlChar*)"device");
                set_default_device(device);
                xmlFree(device);

                char *baudrate = (char*)xmlGetProp(curr,
                        (xmlChar*)"baudrate");
                set_default_baudrate(baudrate);
                xmlFree(baudrate);
            } if (0 == xmlStrcmp(curr->name, (xmlChar*)"devices")) {
                int ret = __device_create_pool(
                        (int)xmlChildElementCount(curr));
                if (0 > ret)
                    break;
            } else if (0 == xmlStrcmp(curr->name,
                        (xmlChar*)"usbdev")) {
                __device_add(curr);
            }
        }

        device_traverse_trees(curr->children);
    }

    return 0;
}

static int read_usb_gps_list(const char* filename)
{
    if (filename == NULL)
        return -EINVAL;

    xmlDoc *doc = xmlReadFile(filename, NULL, 0);
    if (doc == NULL)
        return -EINVAL;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        ERROR("empty document??\n");
        xmlFreeDoc(doc);
        return -EINVAL;
    }

    if (0 == xmlStrcmp(root->name, (xmlChar*)"odroid-gps")) {
        device_traverse_trees(root);
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    if (0 <= nr_devices)
        INFO("%d device(s) are listed\n", nr_devices);

    return nr_devices;
}

int odroid_tty_set_baudrate(const char* devname, speed_t baudrate)
{
    int fd = open(devname, O_RDONLY);
    if (fd < 0)
            return -EINVAL;

    if (0 == isatty(fd)) {
        ERROR("The device '%s' seems not a TTY device (%s)\n",
                devname, strerror(errno));
        close(fd);
        return -errno;
    }

    struct termios  ios;
    tcgetattr(fd, &ios);

    ios.c_lflag = 0;                    /* disable ECHO, ICANON, etc... */
    ios.c_oflag &= (~ONLCR);            /* Stop \n -> \r\n translation on output */
    ios.c_iflag &= (~(ICRNL | INLCR));  /* Stop \r -> \n & \n -> \r translation on input */
    ios.c_iflag |= (IGNCR | IXOFF);     /* Ignore \r & XON/XOFF on input */
    cfsetispeed(&ios, baudrate);

    tcsetattr(fd, TCSANOW, &ios);

    return close(fd);
}

static int usbdev_lookup_by_vid_pid(uint16_t vid, uint16_t pid, speed_t *_baudrate)
{
    if (devices == NULL) {
        int nr = read_usb_gps_list(GPS_DEVICE_LIST_FILE);
        if (nr <= 0)
            return -EINVAL;
    }

    struct gpsdevice **devs = devices;
    for (; *devs; devs++) {
        if ((*devs)->vid == vid && (*devs)->pid == pid) {
            if (_baudrate)
                *_baudrate = (*devs)->baudrate;
            INFO("USB tty device %04x:%04x (baudrate=%s) is found!\n",
                    vid, pid, nameof_termbits((*devs)->baudrate));
            return 1;
        }
    }

    return 0;
}

int odroid_usbtty_find_by_product(char* str, speed_t *_baudrate)
{
    if (str == NULL)
        return -EINVAL;

    char *vendor = strtok(str, "/");
    char *product = strtok(NULL, "/");

    uint16_t vid = strtol(vendor, NULL, 16);
    uint16_t pid = strtol(product, NULL, 16);

    return usbdev_lookup_by_vid_pid(vid, pid, _baudrate);
}

int parse_uevent_from_sys(const char *file, const char *keyword, char **_out)
{
    int ret = -EINVAL;

    if ((file == NULL) || (keyword == NULL) || (_out == NULL))
            return ret;

    int fd = open(file, O_RDONLY);
    if (fd < 0)
            return ret;

    char buf[2048];
    int len = read(fd, buf, sizeof(buf));
    if (len <= 0) {
        close(fd);
        return -errno;
    }

    buf[len] = 0;

    char* str = strstr(buf, keyword);
    if (str) {
        char* out = strtok(str + strlen(keyword), "\n");
        if (out) {
            *_out = strdup(out);
            ret = 0;
        }
    }

    close(fd);

    return ret;
}

/* vim: set ts=4 sw=4 expandtab: */
