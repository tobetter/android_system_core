/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "log.h"

#ifdef ANDROID
#include <cutils/memory.h>
#else
void android_memset16(void *_ptr, unsigned short val, unsigned count)
{
    unsigned short *ptr = _ptr;
    count >>= 1;
    while(count--)
        *ptr++ = val;
}
void android_memset32(void *_ptr, uint32_t val, unsigned count)
{
    uint32_t *ptr = _ptr;
    count >>= 2;
    while(count--)
        *ptr++ = val;
}
#endif

struct FB {
    uint32_t *bits;
    unsigned size;
    int fd;
    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;
};

#define fb_width(fb) ((fb)->vi.xres)
#define fb_height(fb) ((fb)->vi.yres)
#define fb_bpp(fb) ((fb)->vi.bits_per_pixel)
#define fb_size(fb) ((fb)->vi.xres * (fb)->vi.yres * ((fb)->vi.bits_per_pixel / 8))

static int fb_open(struct FB *fb)
{
    fb->fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb->fd < 0)
        return -1;

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        goto fail;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vi) < 0)
        goto fail;

    fb->bits = mmap(0, fb_size(fb), PROT_READ | PROT_WRITE, 
                    MAP_SHARED, fb->fd, 0);
    if (fb->bits == MAP_FAILED)
        goto fail;

    return 0;

fail:
    close(fb->fd);
    return -1;
}

static void fb_close(struct FB *fb)
{
    munmap(fb->bits, fb_size(fb));
    close(fb->fd);
}

/* there's got to be a more portable way to do this ... */
static void fb_update(struct FB *fb)
{
    fb->vi.yoffset = 1;
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi);
    fb->vi.yoffset = 0;
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi);
}

static int vt_set_mode(int graphics)
{
    int fd, r;
    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (fd < 0)
        return -1;
    r = ioctl(fd, KDSETMODE, (void*) (graphics ? KD_GRAPHICS : KD_TEXT));
    close(fd);
    return r;
}

/* 565RLE image format: [count(2 bytes), rle(2 bytes)] */
#ifndef MATCH_LOGO_SIZE
int load_565rle_image(char *fn)
{
    struct FB fb;
    struct stat s;
    unsigned short *data, *ptr;
    unsigned count, max;
    int fd;

    if (vt_set_mode(1)) 
        return -1;

    fd = open(fn, O_RDONLY);
    if (fd < 0) {
        ERROR("cannot open '%s'\n", fn);
        goto fail_restore_text;
    }

    if (fstat(fd, &s) < 0) {
        goto fail_close_file;
    }

    data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
        goto fail_close_file;

    if (fb_open(&fb))
        goto fail_unmap_data;

    max = fb_width(&fb) * fb_height(&fb);
    ptr = data;
    count = s.st_size;
    if (fb_bpp(&fb) == 16) {
        uint16_t *bits = (uint16_t*) fb.bits;
        while (count > 3) {
            unsigned n = ptr[0];
            if (n > max)
                break;
            android_memset16(bits, ptr[1], n << 1);
            bits += n;
            max -= n;
            ptr += 2;
            count -= 4;
        }
    } else if (fb_bpp(&fb) == 32) {
        uint32_t *bits = fb.bits;
        while (count > 3) {
            unsigned n = ptr[0];
            if (n > max)
                break;
            uint32_t data32 = (0xff << 24) | (((ptr[1]>>11)&0x1f)<<19) | (((ptr[1]>>5)&0x3f)<<10) | (( ptr[1]&0x1f)<<3);
            android_memset32(bits, data32, n << 2);
            bits += n;
            max -= n;
            ptr += 2;
            count -= 4;
        }
    }

    munmap(data, s.st_size);
    fb_update(&fb);
    fb_close(&fb);
    close(fd);
    unlink(fn);
    return 0;

fail_unmap_data:
    munmap(data, s.st_size);    
fail_close_file:
    close(fd);
fail_restore_text:
    vt_set_mode(0);
    return -1;
}
#else
int load_565rle_image_mbx(char *fn,char* resolution,char* hdmimode,char* cvbsmode)
{
#ifdef TVMODE_ALL_SCALE

    int result = 0;

    struct FB fb;
    struct stat s;
    unsigned short *data, *ptr;
    unsigned count, max;
    int hpd_state_value;
    int fd;
    int fd_vaxis, fd_daxis, fd_faxis, fd_waxis, fd_freescale,  fd_blank, fd_freescalemode, fd1_freescalemode, fd_hpd_state, fd_dmode;

    if((fd_vaxis = open("/sys/class/video/axis", O_RDWR)) < 0) {
        ERROR("open /sys/class/video/axis fail.");
    }
    if((fd_daxis = open("/sys/class/display/axis", O_RDWR)) < 0) {
        ERROR("open /sys/class/display/axis fail.");
    }
    if((fd_dmode = open("/sys/class/display/mode", O_RDWR)) < 0) {
        ERROR("open /sys/class/display/mode fail.");
    }
    if((fd_faxis = open("/sys/class/graphics/fb0/free_scale_axis", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb0/free_scale_axis fail.");
    }
    if((fd_waxis = open("/sys/class/graphics/fb0/window_axis", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb0/window_axis fail.");
    }
    if((fd_freescale = open("/sys/class/graphics/fb0/free_scale", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb0/free_scale fail.");
    }
    if((fd_freescalemode = open("/sys/class/graphics/fb0/freescale_mode", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb0/freescale_mode fail.");
    }
    if((fd1_freescalemode = open("/sys/class/graphics/fb1/freescale_mode", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb1/freescale_mode fail.");
    }
    if((fd_blank = open("/sys/class/graphics/fb0/blank", O_RDWR)) < 0) {
        ERROR("open /sys/class/graphics/fb0/blank fail.");
    }

    if((fd_hpd_state = open("/sys/class/amhdmitx/amhdmitx0/hpd_state", O_RDWR)) < 0) {
        ERROR("open /sys/class/amhdmitx/amhdmitx0/hpd_state.");
    }

#ifdef HAS_HDMIONLY_FUNCTION
    read(fd_hpd_state,&hpd_state_value, 2);
    ERROR("hdmi hpd_status is :%d\n",hpd_state_value);
    if(hpd_state_value==49)//ASCII 49 =1; hdmi connected
        resolution = hdmimode;
    else
    {
        if((strncmp(cvbsmode, "480cvbs", 7)!=0) && (strncmp(cvbsmode, "576cvbs", 7)!=0))
        { 
            strlcpy(cvbsmode, "480cvbs", sizeof("480cvbs"));
            ERROR("cvbsmode is null, use default value:%s\n",cvbsmode);
        }
        resolution = cvbsmode;
    }

#endif      
    //write(fd_blank, "1", strlen("1"));
    write(fd_dmode, resolution, strlen(resolution));
    // if(strcmp(realoutput,"true")==0){
    ERROR("realoutput:true---");
    write(fd_daxis, "0 0 1920 1080 0 0 18 18", strlen("0 0 1920 1080 0 0 18 18"));
    write(fd_freescalemode, "1", strlen("1"));
    write(fd1_freescalemode, "1", strlen("1"));
    write(fd_faxis, "0 0 1919 1079", strlen("0 0 1919 1079"));
    write(fd_vaxis, "0 0 1919 1079", strlen("0 0 1919 1079"));
    /*}else{
    ERROR("realoutput:false---");
    write(fd_daxis, "0 0 1280 720 0 0 18 18", strlen("0 0 1280 720 0 0 18 18"));
    write(fd_faxis, "0 0 1279 719", strlen("0 0 1279 719"));
    write(fd_vaxis, "0 0 1279 719", strlen("0 0 1279 719"));
    }*/
    
    if(!strncmp(resolution, "480", 3))
    {
        write(fd_waxis, "0 0 719 479", strlen("0 0 719 479"));
    }
    else if(!strncmp(resolution, "576", 3))
    {
        write(fd_waxis, "0 0 719 575", strlen("0 0 719 575"));
    }
    else if(!strncmp(resolution, "720", 3))
    {
        write(fd_waxis, "0 0 1279 719", strlen("0 0 1279 719"));
    }
    else if(!strncmp(resolution, "1080", 4))
    {
        write(fd_waxis, "0 0 1919 1079", strlen("0 0 1919 1079"));
    }
    else if(!strncmp(resolution, "4k2k", 4))
    {
        write(fd_waxis, "0 0 3839 2159", strlen("0 0 3839 2159"));
    }
    else
    {
        write(fd_waxis, "0 0 1919 1079", strlen("0 0 1919 1079"));
    }
	  
    if (vt_set_mode(1)) 
        return -1;

    fd = open(fn, O_RDONLY);
    if (fd < 0) {
        ERROR("cannot open '%s'\n", fn);
        goto fail_restore_text;
    }

    if (fstat(fd, &s) < 0) {
        goto fail_close_file;
    }

    data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
        goto fail_close_file;

    if (fb_open(&fb))
        goto fail_unmap_data;

    max = fb_width(&fb) * fb_height(&fb);
    ptr = data;
    count = s.st_size;
    if (fb_bpp(&fb) == 16) {
        uint16_t *bits = (uint16_t*) fb.bits;
        while (count > 3) {
            unsigned n = ptr[0];
            if (n > max)
                break;
            android_memset16(bits, ptr[1], n << 1);
            bits += n;
            max -= n;
            ptr += 2;
            count -= 4;
        }
    } else if (fb_bpp(&fb) == 32) {
        uint32_t *bits = fb.bits;
        while (count > 3) {
            unsigned n = ptr[0];
            if (n > max)
                break;
            uint32_t data32 = (0xff << 24) | (((ptr[1]>>11)&0x1f)<<19) | (((ptr[1]>>5)&0x3f)<<10) | (( ptr[1]&0x1f)<<3);
            android_memset32(bits, data32, n << 2);
            bits += n;
            max -= n;
            ptr += 2;
            count -= 4;
        }
    }

	 
    munmap(data, s.st_size);
    fb_update(&fb);
    fb_close(&fb);
    close(fd);
    unlink(fn);
    //write(fd_freescale, "0x10001", strlen("0x10001"));
    write(fd_blank, "0", strlen("0"));    
    close(fd_vaxis);
    close(fd_daxis);
    close(fd_faxis);
    close(fd_waxis);
    close(fd_freescale);
    close(fd_freescalemode);
    close(fd1_freescalemode);
    close(fd_dmode);
    close(fd_blank);
    close(fd_hpd_state);
    return 0;

fail_unmap_data:
    munmap(data, s.st_size);    
fail_close_file:
    close(fd);

fail_restore_text:
    memset(&fb, 0, sizeof(struct FB));    

    result = fb_open(&fb);
    ERROR("%s result is: %d\n", __FUNCTION__, result);

    if(result != 0)
    {
	vt_set_mode(0);
	result = -1;
    }
    else
    {
	if (fb_bpp(&fb) == 16) 
    	{
		unsigned short* p = (unsigned short*)fb.bits;
		unsigned int index = 0;

		for(index = 0; index <  fb_size(&fb)  / 2; index++)
		{
			*p = 0x0000;
			p++;
		}
    	} 
    	else if (fb_bpp(&fb) == 32)
    	{
    		unsigned int* p = (unsigned int*)fb.bits;
    		unsigned int index = 0;

    		for(index = 0; index <  fb_size(&fb)  / 4; index++)
    		{
    			*p = 0x00000000;
    			p++;
    		}
    	}

	result = 0;
    }	
    fb_update(&fb);
    fb_close(&fb);
    write(fd_freescale, "0x10001", strlen("0x10001"));
    write(fd_blank, "0", strlen("0"));    
    close(fd_vaxis);
    close(fd_daxis);
    close(fd_faxis);
    close(fd_waxis);
    close(fd_freescale);
    close(fd_freescalemode);
    close(fd_dmode);
    close(fd_blank);
    close(fd_hpd_state);
    return result;

#else  
  
  	struct FB fb;
    struct stat s;
    unsigned short *data, *ptr;
    unsigned count, max;
    int fd;

    if (vt_set_mode(1)) 
        return -1;

    fd = open(fn, O_RDONLY);
    if (fd < 0) {
        ERROR("cannot open '%s'\n", fn);
        goto fail_restore_text;
    }

    if (fstat(fd, &s) < 0) {
        goto fail_close_file;
    }

    data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
        goto fail_close_file;

    if (fb_open(&fb))
        goto fail_unmap_data;

	int offset=0;
	int linepos=0;
	int old_linepos=0;
	int logosourcewidth=0;
	if(strcmp(resolution,"1080p")==0||strcmp(resolution,"1080i")==0)
	{
		logosourcewidth	=1920;
	}
	else if(strcmp(resolution,"720p")==0)
	{
		logosourcewidth	=1280;
		
	}
	else if(strcmp(resolution,"480p")==0||strcmp(resolution,"480i")==0
			||strcmp(resolution,"576p")==0||strcmp(resolution,"576i")==0)
	{
		logosourcewidth	=720;
	}
	else
	{
		logosourcewidth	=1280;
	}
	offset=logosourcewidth-fb_width(&fb);

    max = fb_width(&fb) * fb_height(&fb);
    ptr = data;
    count = s.st_size;
    if (fb_bpp(&fb) == 16) {
        uint16_t *bits = (uint16_t*) fb.bits;
        while (count > 3) {
            unsigned n = ptr[0];
            if (n > max)
                break;
            //modify n by offset
					unsigned tmp=(n+linepos)/logosourcewidth;  // how many lines by this pix 
					linepos=(linepos+n)%logosourcewidth;       //new line postion
					n=n-tmp*offset+(old_linepos>fb_width(&fb)-1?old_linepos-fb_width(&fb)+1:0)-(linepos>fb_width(&fb)-1?linepos-fb_width(&fb)+1:0);

			       // how many pixs in framebuffer   
					if(n<=0)
					{
							//ERROR("ERR%d",n);
					}else
					{
						android_memset16(bits, ptr[1], n << 1);
					}
					bits += n;
					max -= n;
					ptr += 2;
					count -= 4;
					old_linepos = linepos;
        }
    } else if (fb_bpp(&fb) == 32) {
        uint32_t *bits = fb.bits;
        while (count > 3) 
        {
	          unsigned n = ptr[0];
	          if (n > max)
	                break;
						//modify n by offset
						unsigned tmp=(n+linepos)/logosourcewidth;  // how many lines by this pix 
						linepos=(linepos+n)%logosourcewidth;       //new line postion
						n=n-tmp*offset+(old_linepos>fb_width(&fb)-1?old_linepos-fb_width(&fb)+1:0)-(linepos>fb_width(&fb)-1?linepos-fb_width(&fb)+1:0);
						
		        uint32_t data32 = (0xff << 24) | (((ptr[1]>>11)&0x1f)<<19) | (((ptr[1]>>5)&0x3f)<<10) | (( ptr[1]&0x1f)<<3);
						if(n<=0)
						{
							//ERROR("ERR%d",n);
						}else
						{
							 android_memset32(bits, data32, n << 2);
						}
		        bits += n;
		        max -= n;
		        ptr += 2;
		        count -= 4;
		        old_linepos = linepos;
	      }
    }
    munmap(data, s.st_size);
    fb_update(&fb);
    fb_close(&fb);
    close(fd);
    unlink(fn);
    return 0;

fail_unmap_data:
    munmap(data, s.st_size);    
fail_close_file:
    close(fd);
fail_restore_text:
    vt_set_mode(0);
    return -1;		
#endif 
}
#endif
