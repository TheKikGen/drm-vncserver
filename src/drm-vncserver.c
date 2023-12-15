/*
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This project is an adaptation of the original fbvncserver for the iPAQ
 * and Zaurus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h> /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"

#include "touch.h"
#include "mouse.h"
#include "keyboard.h"
#include "logging.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#define SERVER_NAME "The Kikgen Labs - MPC VNC Server"
#define TKGL_LOGO "\
__ __| |           |  /_) |     ___|             |           |\n\
  |   __ \\   _ \\  ' /  | |  / |      _ \\ __ \\   |      _` | __ \\   __|\n\
  |   | | |  __/  . \\  |   <  |   |  __/ |   |  |     (   | |   |\\__ \\\n\
 _|  _| |_|\\___| _|\\_\\_|_|\\_\\\\____|\\___|_|  _| _____|\\__,_|_.__/ ____/\n\
"


#define LOG_FPS

#define BITS_PER_SAMPLE 8
#define SAMPLES_PER_PIXEL 4
//#define COLOR_MASK  0x1f001f
#define COLOR_MASK (((1UL << BITS_PER_SAMPLE) << 1) - 1)
#define PIXEL_FB_TO_RFB(p, r_offset, g_offset, b_offset) \
    ((p >> r_offset) & COLOR_MASK) | (((p >> g_offset) & COLOR_MASK) << BITS_PER_SAMPLE) | (((p >> b_offset) & COLOR_MASK) << (2 * BITS_PER_SAMPLE))

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

// #define CHANNELS_PER_PIXEL 4

static char fb_device[256] = "/dev/fb0";
static char touch_device[256] = "";
static char kbd_device[256] = "";
static char mouse_device[256] = "";

static struct fb_var_screeninfo var_scrinfo;
static struct fb_fix_screeninfo fix_scrinfo;

static unsigned short int *RFB_Framebuffer;
static unsigned short int *FrameBuffer;

static int drmfd = -1;
static char drmFB_device[256] = "/dev/dri/card0";
static unsigned short int *drmFramebuffer_mmap = MAP_FAILED;

static int VNC_port = 5900;
static int VNC_rotate = -1;
static int Touch_rotate = -1;
static int Target_fps = 10;
static rfbScreenInfoPtr RFB_Server;
static size_t FrameBuffer_BytesPP;
static unsigned int FrameBuffer_BitsPerPixel;
static unsigned int FrameBuffer_Depth;
static unsigned int FrameBufferSize;
static unsigned int FrameBuffer_Xwidth;
static unsigned int FrameBuffer_Yheight;
int verbose = 0;

#define UNUSED(x) (void)(x)

/* No idea, just copied from fbvncserver as part of the frame differerencing
 * algorithm.  I will probably be later rewriting all of this. */
static struct varblock_t
{
    int min_i;
    int min_j;
    int max_i;
    int max_j;
    int r_offset;
    int g_offset;
    int b_offset;
    int rFrameBuffer_Xwidth;
    int rfb_maxy;
} varblock;

struct type_name {
    unsigned int type;
    const char *name;
};

static const struct type_name connector_type_names[] = {
    { DRM_MODE_CONNECTOR_Unknown, "unknown" },
    { DRM_MODE_CONNECTOR_VGA, "VGA" },
    { DRM_MODE_CONNECTOR_DVII, "DVI-I" },
    { DRM_MODE_CONNECTOR_DVID, "DVI-D" },
    { DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
    { DRM_MODE_CONNECTOR_Composite, "composite" },
    { DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
    { DRM_MODE_CONNECTOR_LVDS, "LVDS" },
    { DRM_MODE_CONNECTOR_Component, "component" },
    { DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
    { DRM_MODE_CONNECTOR_DisplayPort, "DP" },
    { DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
    { DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
    { DRM_MODE_CONNECTOR_TV, "TV" },
    { DRM_MODE_CONNECTOR_eDP, "eDP" },
    { DRM_MODE_CONNECTOR_VIRTUAL, "Virtual" },
    { DRM_MODE_CONNECTOR_DSI, "DSI" },
    { DRM_MODE_CONNECTOR_DPI, "DPI" },
};

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

const char *connector_type_name(unsigned int type)
{
    if (type < ARRAY_SIZE(connector_type_names) && type >= 0) {
        return connector_type_names[type].name;
    }

    return "INVALID";
}

///////////////////////////////////////////////////////////////////////////////
// Log management
///////////////////////////////////////////////////////////////////////////////
enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

#define tklog_trace(...) tklog(LOG_TRACE,  __VA_ARGS__)
#define tklog_debug(...) // tklog(LOG_DEBUG,  __VA_ARGS__)
#define tklog_info(...)  tklog(LOG_INFO,   __VA_ARGS__)
#define tklog_warn(...)  tklog(LOG_WARN,   __VA_ARGS__)
#define tklog_error(...) tklog(LOG_ERROR,  __VA_ARGS__)
#define tklog_fatal(...) tklog(LOG_FATAL,  __VA_ARGS__)

static const char *tklog_level_strings[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "***ERROR", "***FATAL"
};

static void tklog(int level, const char *fmt, ...) {

  va_list ap;

  //time_t timestamp = time( NULL );
  //struct tm * now = localtime( & timestamp );

  //char buftime[16];
  //buftime[strftime(buftime, sizeof(buftime), "%H:%M:%S", now)] = '\0';

  if ( level >= LOG_ERROR ) fprintf(stderr, "[tkgl %-8s]  ",tklog_level_strings[level]);
  else  fprintf(stdout, "[tkgl %-8s]  ",tklog_level_strings[level]);
 
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);

  fflush(stdout);

}

///////////////////////////////////////////////////////////////////////////////
// DRM Framebuffer initialization
///////////////////////////////////////////////////////////////////////////////
static void init_drmFB(void)
{
    drmModeFB           *drmFB;
    drmModeRes          *drmRes;
    drmModeCrtc         *drmCrtc;    
    drmModeConnector    *drmConnector = NULL;
    drmModeEncoder      *drmEncoder = NULL;
    drmModeModeInfoPtr  drmResolution = 0;

    // Open the DRM device
    drmfd = open(drmFB_device, O_RDWR | O_CLOEXEC);
    if (drmfd < 0)
    {
        tklog_fatal("Unable to open DRM device %s.\n",drmFB_device);
        exit(EXIT_FAILURE);
    }

    tklog_info("DRM device %s sucessfully opened.\n",drmFB_device);

    // retrieve device resources
    drmRes = drmModeGetResources(drmfd);
    if (!drmRes)
    {
        tklog_fatal("Unable to retrieve DRM resources (%d).\n", errno);
        exit(EXIT_FAILURE);
    }

    if ( drmRes->count_connectors < 1 ) {
        tklog_fatal("No connector found for that device.\n");
        exit(EXIT_FAILURE);   
    }
    tklog_info("DRM device has %d connectors.\n", drmRes->count_connectors);
   
    // We use the first connector because this vncserver is for MPC devices. No more connectors...
    drmConnector = drmModeGetConnectorCurrent(drmfd, drmRes->connectors[0]);
    if (!drmConnector) {
        tklog_fatal("Unable to get DRM device connector[0].\n");
        exit(EXIT_FAILURE);
    }
    tklog_info("DRM Device Name: %s-%u\n", connector_type_name(drmConnector->connector_type), drmConnector->connector_type_id);
    tklog_info("Encoder        : %d\n", drmConnector->encoder_id);

    if (drmConnector->count_modes <= 0) {
        tklog_fatal("No modes found for this connector.\n");
        exit(EXIT_FAILURE);
    }
        
    // Get resolution (we do not check preferred because there is only one mode)
    drmResolution = &drmConnector->modes[0];
    tklog_info("Resolution : %ux%u@%u\n", drmResolution->hdisplay, drmResolution->vdisplay, drmResolution->vrefresh);
    tklog_info("(ht: %u hs: %u he: %u hskew: %u, vt: %u  vs: %u ve: %u vscan: %u, flags: 0x%X %s)\n",
                drmResolution->htotal, drmResolution->hsync_start, drmResolution->hsync_end, drmResolution->hskew,
                drmResolution->vtotal, drmResolution->vsync_start, drmResolution->vsync_end, drmResolution->vscan,
                drmResolution->flags, drmResolution->type & DRM_MODE_TYPE_PREFERRED ? "<P>":"");


    drmEncoder = drmModeGetEncoder(drmfd, drmConnector->encoder_id);
    if (!drmEncoder) {
        tklog_fatal("Unable to drmModeGetEncoder (%d).\n", errno);
        exit(EXIT_FAILURE);
    }

    drmCrtc = drmModeGetCrtc(drmfd,drmEncoder->crtc_id);
    if (!drmCrtc) {
        tklog_fatal("Unable to drmModeGetCrtc (%d).\n", errno);
        exit(EXIT_FAILURE);           
    }

    tklog_info("Connector %d is connected to encoder %d CRTC %d.\n",drmConnector->connector_id,drmConnector->encoder_id, drmCrtc->crtc_id);
    
      /* check framebuffer id */
    drmFB = drmModeGetFB(drmfd, drmCrtc->buffer_id);
    if (drmFB == NULL) {
        tklog_fatal("Unable to get framebuffer for specified CRTC.\n");
        exit(EXIT_FAILURE);
    }

    tklog_info("Got framebuffer at CRTC: %d.\n", drmCrtc->crtc_id);
    tklog_info("FB depth is %u pitch %u width %u height %u bpp %u.\n", drmFB->depth, drmFB->pitch,drmFB->width,drmFB->height,drmFB->bpp);

    /* Now this is how we dump the framebuffer */
    /* structure to retrieve FB later */
    struct drm_mode_map_dumb dumb_map;

    memset(&dumb_map, 0, sizeof(dumb_map));
    dumb_map.handle = drmFB->handle;    
    dumb_map.offset = 0;

    if ( drmIoctl(drmfd, DRM_IOCTL_MODE_MAP_DUMB, &dumb_map) != 0 ) {
        tklog_fatal("DRM_IOCTL_MODE_MAP_DUMB failed (err=%d)\n", errno);
        exit(EXIT_FAILURE);
    }

    // Recompute with drm infos..should be the same as fb0
    FrameBufferSize          = drmFB->pitch * drmFB->height;
    FrameBuffer_BitsPerPixel = drmFB->bpp;
    FrameBuffer_BytesPP      = FrameBuffer_BitsPerPixel / 8;
    FrameBuffer_Depth        = drmFB->depth;


    drmFramebuffer_mmap = mmap(0, FrameBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, drmfd, dumb_map.offset);
    if (drmFramebuffer_mmap == MAP_FAILED) {
        tklog_fatal("DRM frame buffer mmap failed (err=%d)\n", errno);
        exit(EXIT_FAILURE);
    }
    tklog_info("DRM frame buffer map of %u bytes allocated at %p.\n",FrameBufferSize,drmFramebuffer_mmap);

    drmModeFreeEncoder(drmEncoder);
    drmModeFreeCrtc(drmCrtc);
    drmModeFreeConnector(drmConnector);
    drmModeFreeResources(drmRes);
}

///////////////////////////////////////////////////////////////////////////////
// DRM framebuffer cleaning
///////////////////////////////////////////////////////////////////////////////
static void cleanup_drmFB(void)
{
    close(drmfd);
}

///////////////////////////////////////////////////////////////////////////////
// "old" Framebuffer initialization
///////////////////////////////////////////////////////////////////////////////
static void init_fb(void)
{

    int fbfd = -1;

    if ((fbfd = open(fb_device, O_RDONLY)) == -1) {
        tklog_fatal("cannot open fb device %s\n", fb_device);
        exit(EXIT_FAILURE);
    }
    tklog_info("FB device %s successfully opened.\n");


    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &var_scrinfo) != 0) {
        tklog_fatal("ioctl error (FBIOGET_VSCREENINFO)\n");
        exit(EXIT_FAILURE);
    }

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fix_scrinfo) != 0) {
        tklog_fatal("ioctl error (FBIOGET_FSCREENINFO)\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Get actual resolution of the framebufffer, which is not always the same as the screen resolution.
     * This prevents the screen from 'smearing' on 1366 x 768 displays
     */

    FrameBuffer_Xwidth       = fix_scrinfo.line_length / (var_scrinfo.bits_per_pixel / 8.0);
    FrameBuffer_Yheight      = var_scrinfo.yres;
    FrameBuffer_BytesPP      = var_scrinfo.bits_per_pixel / 8;
    FrameBuffer_BitsPerPixel = var_scrinfo.bits_per_pixel;
    FrameBufferSize          = FrameBuffer_Xwidth * FrameBuffer_Yheight * FrameBuffer_BytesPP;
    

    tklog_info(" fb xres=%d, yres=%d, xresv=%d, yresv=%d, xoffs=%d, yoffs=%d, bpp=%d\n",
               (int)FrameBuffer_Xwidth, (int)FrameBuffer_Yheight,
               (int)var_scrinfo.xres_virtual, (int)var_scrinfo.yres_virtual,
               (int)var_scrinfo.xoffset, (int)var_scrinfo.yoffset,
               (int)var_scrinfo.bits_per_pixel);
    
    tklog_info("  offset:length red=%d:%d green=%d:%d blue=%d:%d \n",
               (int)var_scrinfo.red.offset, (int)var_scrinfo.red.length,
               (int)var_scrinfo.green.offset, (int)var_scrinfo.green.length,
               (int)var_scrinfo.blue.offset, (int)var_scrinfo.blue.length);
    
    tklog_info("  frame buffer size : %d bytes\n",FrameBufferSize);
 
    close(fbfd); 
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    int scancode;

    tklog_debug("Got keysym: %04x (down=%d)\n", (unsigned int)key, (int)down);

    if ((scancode = keysym2scancode(key, cl)))
    {
        injectKeyEvent(scancode, down);
    }
}

static void ptrevent_touch(int buttonMask, int x, int y, rfbClientPtr cl)
{
    UNUSED(cl);
    /* Indicates either pointer movement or a pointer button press or release. The pointer is
now at (x-position, y-position), and the current state of buttons 1 to 8 are represented
by bits 0 to 7 of button-mask respectively, 0 meaning up, 1 meaning down (pressed).
On a conventional mouse, buttons 1, 2 and 3 correspond to the left, middle and right
buttons on the mouse. On a wheel mouse, each step of the wheel upwards is represented
by a press and release of button 4, and each step downwards is represented by
a press and release of button 5.
  From: http://www.vislab.usyd.edu.au/blogs/index.php/2009/05/22/an-headerless-indexed-protocol-for-input-1?blog=61 */

    tklog_debug("Got ptrevent: %04x (x=%d, y=%d)\n", buttonMask, x, y);
    // Simulate left mouse event as touch event
    static int pressed = 0;
    if (buttonMask & 1)
    {
        if (pressed == 1)
        {
            injectTouchEvent(MouseDrag, x, y, &var_scrinfo);
        }
        else
        {
            pressed = 1;
            injectTouchEvent(MousePress, x, y, &var_scrinfo);
        }
    }
    if (buttonMask == 0)
    {
        if (pressed == 1)
        {
            pressed = 0;
            injectTouchEvent(MouseRelease, x, y, &var_scrinfo);
        }
    }
}

static void ptrevent_mouse(int buttonMask, int x, int y, rfbClientPtr cl)
{
    UNUSED(cl);
    /* Indicates either pointer movement or a pointer button press or release. The pointer is
now at (x-position, y-position), and the current state of buttons 1 to 8 are represented
by bits 0 to 7 of button-mask respectively, 0 meaning up, 1 meaning down (pressed).
On a conventional mouse, buttons 1, 2 and 3 correspond to the left, middle and right
buttons on the mouse. On a wheel mouse, each step of the wheel upwards is represented
by a press and release of button 4, and each step downwards is represented by
a press and release of button 5.
  From: http://www.vislab.usyd.edu.au/blogs/index.php/2009/05/22/an-headerless-indexed-protocol-for-input-1?blog=61 */

    tklog_debug("Got mouse: %04x (x=%d, y=%d)\n", buttonMask, x, y);
    // Simulate left mouse event as touch event
    injectMouseEvent(&var_scrinfo, buttonMask, x, y);
}

///////////////////////////////////////////////////////////////////////////////
// VNC SERVER INITIALIZATION
///////////////////////////////////////////////////////////////////////////////
static void init_fb_server(int argc, char **argv, rfbBool enable_touch, rfbBool enable_mouse)
{
    tklog_info("Initializing VNC server...\n");

    // Allocate the VNC server buffer to be managed (not manipulated) by libvncserver.
    RFB_Framebuffer = malloc(FrameBufferSize);
    assert(RFB_Framebuffer != NULL);
    memset(RFB_Framebuffer, 0, FrameBufferSize);

    // Allocate the comparison buffer for detecting drawing updates from frame to frame.
    FrameBuffer = malloc(FrameBufferSize);
    assert(FrameBuffer != NULL);

    RFB_Server = rfbGetScreen(&argc, argv, FrameBuffer_Xwidth, FrameBuffer_Yheight, BITS_PER_SAMPLE, SAMPLES_PER_PIXEL, FrameBuffer_BytesPP);
    
    assert(RFB_Server != NULL);

    RFB_Server->desktopName  = SERVER_NAME;
    RFB_Server->frameBuffer  = (char *)RFB_Framebuffer;
    RFB_Server->alwaysShared = TRUE;
    RFB_Server->httpDir      = NULL;
    RFB_Server->port         = VNC_port;

    RFB_Server->kbdAddEvent = keyevent;
    if (enable_touch) RFB_Server->ptrAddEvent = ptrevent_touch;
    if (enable_mouse) RFB_Server->ptrAddEvent = ptrevent_mouse; 
    
    // Set PixelFormat for server
    RFB_Server->serverFormat.bitsPerPixel = FrameBuffer_BitsPerPixel ;
    RFB_Server->serverFormat.depth        = FrameBuffer_Depth ;
    RFB_Server->serverFormat.bigEndian    = 0 ;
    RFB_Server->serverFormat.trueColour   = 1 ;
    RFB_Server->serverFormat.redMax       = 0x00FF ;
    RFB_Server->serverFormat.greenMax     = 0x00FF ;
    RFB_Server->serverFormat.blueMax      = 0x00FF ; 
    RFB_Server->serverFormat.blueMax      = 0x00FF ;
 
    RFB_Server->serverFormat.redShift     = var_scrinfo.red.offset ;
    RFB_Server->serverFormat.greenShift   = var_scrinfo.green.offset ;
    RFB_Server->serverFormat.blueShift    = var_scrinfo.blue.offset ;  
    
    // Rotation adjustments
    switch (VNC_rotate) {
        case 0:
        case 180:
            RFB_Server->width = FrameBuffer_Xwidth;
            RFB_Server->height = FrameBuffer_Yheight;
            RFB_Server->paddedWidthInBytes = FrameBuffer_Xwidth * FrameBuffer_BytesPP;
            break;

        case 90:
        case 270:
            RFB_Server->width = FrameBuffer_Yheight;
            RFB_Server->height = FrameBuffer_Xwidth;
            RFB_Server->paddedWidthInBytes = FrameBuffer_Yheight * FrameBuffer_BytesPP;
            break;

        default:
            tklog_fatal("%d is an invalid rotation value. 0, 90, 180, 270 are correct values\n",VNC_rotate);
            exit(EXIT_FAILURE);
    }
    
    rfbInitServer(RFB_Server);

    // Mark as dirty since we haven't sent any updates at all yet.
    rfbMarkRectAsModified(RFB_Server, 0, 0, FrameBuffer_Xwidth, FrameBuffer_Yheight);

    /* No idea. */
    varblock.r_offset = var_scrinfo.red.offset + var_scrinfo.red.length - BITS_PER_SAMPLE;
    varblock.g_offset = var_scrinfo.green.offset + var_scrinfo.green.length - BITS_PER_SAMPLE;
    varblock.b_offset = var_scrinfo.blue.offset + var_scrinfo.blue.length - BITS_PER_SAMPLE;
    varblock.rFrameBuffer_Xwidth = FrameBuffer_Yheight;
    varblock.rfb_maxy = FrameBuffer_Xwidth - 1;
}

// sec
#define LOG_TIME 5

int timeToLogFPS()
{
    static struct timeval now = {0, 0}, then = {0, 0};
    double elapsed, dnow, dthen;
    gettimeofday(&now, NULL);
    dnow = now.tv_sec + (now.tv_usec / 1000000.0);
    dthen = then.tv_sec + (then.tv_usec / 1000000.0);
    elapsed = dnow - dthen;
    if (elapsed > LOG_TIME)
        memcpy((char *)&then, (char *)&now, sizeof(struct timeval));
    return elapsed > LOG_TIME;
}


///////////////////////////////////////////////////////////////////////////////
// VNC UPDATE SCREEN - 32 bits per pixel
///////////////////////////////////////////////////////////////////////////////

static void update_screen32(void)
{
#ifdef LOG_FPS
    if (verbose)
    {
        static int frames = 0;
        frames++;
        if (timeToLogFPS())
        {
            double fps = frames / LOG_TIME;
            tklog_info("  fps: %f\n", fps);
            frames = 0;
        }
    }
#endif

    varblock.min_i = varblock.min_j = 9999;
    varblock.max_i = varblock.max_j = -1;

    uint32_t *f = (uint32_t *)drmFramebuffer_mmap;  // -> framebuffer
    uint32_t *c = (uint32_t *)FrameBuffer;          // -> compare framebuffer
    uint32_t *r = (uint32_t *)RFB_Framebuffer;      // -> remote framebuffer
 

    // Something changed in the Framebuffer ?    
    if ( memcmp(drmFramebuffer_mmap, FrameBuffer, FrameBufferSize) != 0)
    {

        // memcpy(FrameBuffer,drmFramebuffer_mmap,FrameBufferSize);
        // memcpy(RFB_Framebuffer,drmFramebuffer_mmap,FrameBufferSize);
        // rfbMarkRectAsModified(RFB_Server, 0, 0, FrameBuffer_Xwidth, FrameBuffer_Yheight);
        // return;

        uint16_t y;
        for (y = 0; y < FrameBuffer_Yheight; y++)
        {
            /* Compare every pixels at a time */
            uint16_t x;
            for (x = 0; x < FrameBuffer_Xwidth; x++)
            {
                if ( *f != *c)
                {
                    uint16_t x2, y2;

                    *c = *f; // Update compare FB

                    switch (VNC_rotate)
                    {
                    case 0:
                        x2 = x;
                        y2 = y;
                        break;

                    case 90:
                        x2 = FrameBuffer_Yheight - 1 - y;
                        y2 = x;
                        break;

                    case 180:
                        x2 = FrameBuffer_Xwidth - 1 - x;
                        y2 = FrameBuffer_Yheight - 1 - y;
                        break;

                    case 270:
                        x2 = y;
                        y2 = FrameBuffer_Xwidth - 1 - x;
                        break;
                    default:
                        tklog_fatal("%d rotation is invalid. 0, 90, 180, 270 are correct values\n");
                        exit(EXIT_FAILURE);
                    }

                    // Update remote FB
                    r[y2 * RFB_Server->width + x2] = *f ;
                    //PIXEL_FB_TO_RFB(*f, varblock.r_offset, varblock.g_offset, varblock.b_offset);

                    if (x2 < varblock.min_i)
                        varblock.min_i = x2;
                    else
                    {
                        if (x2 > varblock.max_i)
                            varblock.max_i = x2;

                        if (y2 > varblock.max_j)
                            varblock.max_j = y2;
                        else if (y2 < varblock.min_j)
                            varblock.min_j = y2;
                    }
                }

                f++;
                c++;
            }
        }

    } 
 
    if (varblock.min_i < 9999)
    {
        if (varblock.max_i < 0)
            varblock.max_i = varblock.min_i;

        if (varblock.max_j < 0)
            varblock.max_j = varblock.min_j;

        tklog_debug("Dirty page: %dx%d+%d+%d...\n",
                    (varblock.max_i + 2) - varblock.min_i, (varblock.max_j + 1) - varblock.min_j,
                    varblock.min_i, varblock.min_j);

        rfbMarkRectAsModified(RFB_Server, varblock.min_i, varblock.min_j,
                              varblock.max_i + 2, varblock.max_j + 1);
    }
}

/*****************************************************************************/

void print_usage(char **argv)
{
    fprintf(stdout,"%s [-f device] [-p port] [-t touchscreen] [-m touchscreen] [-k keyboard] [-r rotation] [-R touchscreen rotation] [-F FPS] [-v] [-h]\n"
               "-p port: VNC port, default is 5900\n"
               "-f device: drm device node, default is %s\n"
               "-k device: keyboard device node (example: /dev/input/event0)\n"
               "-t device: touchscreen device node (example:/dev/input/event2)\n"
               "-m device: mouse device node (example:/dev/input/event2)\n"
               "-r degrees: framebuffer rotation, default is 0\n"
               "-R degrees: touchscreen rotation, default is same as framebuffer rotation\n"
               "-F FPS: Maximum target FPS, default is 10.  0 means unlimited FPS.\n"
               "-v: verbose\n"
               "-h: print this help\n\n",
               *argv,drmFB_device);
}

int main(int argc, char **argv)
{

    fprintf(stdout,"\n%s",TKGL_LOGO);
    fprintf(stdout,"----------------------------------------------------------------------\n");
    fprintf(stdout,"TKGL VNC SERVER FOR DRM DEVICES - V1.0\n");
    fprintf(stdout,"(c) The KikGen Labs.\n\n");

    if (argc > 1)
    {
        int i = 1;
        while (i < argc)
        {
            if (*argv[i] == '-')
            {
                switch (*(argv[i] + 1))
                {
                case 'h':
                    print_usage(argv);
                    exit(0);
                    break;
                case 'f':
                    i++;
                    if (argv[i])
                        strcpy(drmFB_device, argv[i]);
                    break;
                case 't':
                    i++;
                    if (argv[i])
                        strcpy(touch_device, argv[i]);
                    break;
                case 'm':
                    i++;
                    if (argv[i])
                        strcpy(mouse_device, argv[i]);
                    break;                    
                case 'k':
                    i++;
                    strcpy(kbd_device, argv[i]);
                    break;
                case 'p':
                    i++;
                    if (argv[i])
                        VNC_port = atoi(argv[i]);
                    break;
                case 'r':
                    i++;
                    if (argv[i])
                        VNC_rotate = atoi(argv[i]);
                    break;
                case 'R':
                    i++;
                    if (argv[i])
                        Touch_rotate = atoi(argv[i]);
                    break;
               case 'F':
                    i++;
                    if (argv[i])
                        Target_fps = atoi(argv[i]);
                    break;
                case 'v':
                    verbose = 1;
                    break;
                }
            }
            i++;
        }
    }

  	tklog_info("VNCSERVER STARTING...\n");

    init_fb();
    init_drmFB();

    if (FrameBuffer_BitsPerPixel != 32) {
        tklog_fatal("Only 32 bits framebuffer is supported. Current Bits Per Pixel is %d.\n",FrameBuffer_BitsPerPixel);
        exit(EXIT_FAILURE);
    }

   
    if ( FrameBuffer_Xwidth < FrameBuffer_Yheight  && VNC_rotate < 0 ) {
        tklog_info("Display auto rotation activated (90°)\n");
        VNC_rotate = 90;
    } 

    if (VNC_rotate < 0 )  VNC_rotate = 0 ;
    if (Touch_rotate < 0) Touch_rotate = VNC_rotate;   
    
    if (strlen(kbd_device) > 0) {
        int ret = init_kbd(kbd_device);
        if (!ret) tklog_error("Keyboard device %s not available.\n", kbd_device);
    }
    else tklog_warn("No keyboard device. You may use -k command line option\n");

    rfbBool enable_touch = FALSE;
    rfbBool enable_mouse = FALSE;
    if(strlen(touch_device) > 0 && strlen(mouse_device) > 0) {
        tklog_fatal("It is not possible to use both mouse and touch device.\n");
        exit(EXIT_FAILURE);
    }
    else if (strlen(touch_device) > 0) {
        // init touch only if there is a touch device defined
        int ret = init_touch(touch_device, Touch_rotate);
        enable_touch = (ret > 0);
    }
    else if(strlen(mouse_device) > 0) {
        // init mouse only if there is a mouse device defined
        int ret = init_mouse(mouse_device, Touch_rotate);
        enable_mouse = (ret > 0);        
    }
    else {
        tklog_warn("No touch or mouse device. You may use -t command line option.\n");
    }

    tklog_info("VNC (TKGL) server initialized with the following parameters :\n");
    tklog_info("  width,height       : %d,%d\n", (int)FrameBuffer_Xwidth,(int)FrameBuffer_Yheight);
    tklog_info("  bpp                : %d\n", (int)var_scrinfo.bits_per_pixel);
    tklog_info("  port               : %d\n", (int)VNC_port);
    tklog_info("  rotate             : %d\n", (int)VNC_rotate);
    tklog_info("  mouse/touch rotate : %d\n", (int)Touch_rotate);
    tklog_info("  target FPS         : %d\n", (int)Target_fps);

    init_fb_server(argc, argv, enable_touch, enable_mouse);
    
 
    /* Implement our own event loop to detect changes in the framebuffer. */
    while (1)
    {
        rfbRunEventLoop(RFB_Server, 40 * 1000, TRUE);
        while (rfbIsActive(RFB_Server))
        {
            if (RFB_Server->clientHead != NULL)
                update_screen32();
            else {
                if (Target_fps > 0) usleep(1000 * 1000 / Target_fps);
                else usleep(10 * 1000);     
            }    
        }
    }
 
    tklog_info("Cleaning up things...\n");
    cleanup_drmFB();
    cleanup_kbd();
    cleanup_touch();
}
