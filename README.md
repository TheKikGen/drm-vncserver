# drm-vncserver

VNC server for Linux DRM framebuffer devices.

This project allows remote access to embedded Linux systems with a direct frame buffer rendering, notably Akai MPC devices.

Working configurations: 32 bits/pixel with 90, 180, 270 degress rotation. 
Other resolutions are not implemented.

The code is based on a LibVNC example for Android:
https://github.com/LibVNC/libvncserver/blob/master/examples/androidvncserver.c 
and was forked from the following project https://github.com/ponty/framebuffer-vncserver 

### build

Dependency:

apt-get install libvncserver-dev libdrm-dev

Use the "make" command to build the binary in the bin/ directory.

### command-line help 

	./drm-vncserver [-f device] [-p port] [-t touchscreen] [-k keyboard] [-r rotation] [-R touchscreen rotation] [-F FPS] [-v] [-h]
	-p port: VNC port, default is 5900
	-f device: framebuffer device node, default is /dev/fb0
	-k device: keyboard device node (example: /dev/input/event0)
	-t device: touchscreen device node (example:/dev/input/event2)
	-r degrees: framebuffer rotation, default : 0 or 90 if width < height. 
	-R degrees: touchscreen rotation, default is same as framebuffer rotation
	-F FPS: Maximum target FPS, default is 10
	-v: verbose
	-h: print this help

### example with a MPC Force 

	root@force:~/drm-vncserver# bin/drm-vncserver -t /dev/input/event1 -k /dev/input/event2 -r 90
	
	__ __| |           |  /_) |     ___|             |           |
	  |   __ \   _ \  ' /  | |  / |      _ \ __ \   |      _` | __ \   __|
	  |   | | |  __/  . \  |   <  |   |  __/ |   |  |     (   | |   |\__ \
	 _|  _| |_|\___| _|\_\_|_|\_\\____|\___|_|  _| _____|\__,_|_.__/ ____/
	----------------------------------------------------------------------
	TKGL VNC SERVER FOR DRM DEVICES - V1.0
	(c) The KikGen Labs.
	
	[tkgl INFO    ]  VNCSERVER STARTING...
	[tkgl INFO    ]  FB device /dev/fb0 successfully opened.
	[tkgl INFO    ]   fb xres=800, yres=1280, xresv=800, yresv=3840, xoffs=0, yoffs=0, bpp=32
	[tkgl INFO    ]    offset:length red=16:8 green=8:8 blue=0:8
	[tkgl INFO    ]    frame buffer size : 4096000 bytes
	[tkgl INFO    ]  DRM device /dev/dri/card0 sucessfully opened.
	[tkgl INFO    ]  DRM device has 1 connectors.
	[tkgl INFO    ]  DRM Device Name: DSI-1
	[tkgl INFO    ]  Encoder        : 49
	[tkgl INFO    ]  Resolution : 800x1280@59
	[tkgl INFO    ]  (ht: 970 hs: 876 he: 894 hskew: 0, vt: 1290  vs: 1284 ve: 1286 vscan: 0, flags: 0x0 )
	[tkgl INFO    ]  Connector 50 is connected to encoder 49 CRTC 44.
	[tkgl INFO    ]  Got framebuffer at CRTC: 44.
	[tkgl INFO    ]  FB depth is 24 pitch 3200 width 800 height 1280 bpp 32.
	[tkgl INFO    ]  DRM frame buffer map of 4096000 bytes allocated at 0xb6266000.
	Initializing keyboard device /dev/input/event2 ...
	cannot open kbd device /dev/input/event2
	[tkgl ***ERROR]  Keyboard device /dev/input/event2 not available.
	Initializing touch device /dev/input/event1 ...
	  x:(0 2048)  y:(0 2048)
	[tkgl INFO    ]  VNC (TKGL) server initialized with the following parameters :
	[tkgl INFO    ]    width,height       : 800,1280
	[tkgl INFO    ]    bpp                : 32
	[tkgl INFO    ]    port               : 5900
	[tkgl INFO    ]    rotate             : 90
	[tkgl INFO    ]    mouse/touch rotate : 90
	[tkgl INFO    ]    target FPS         : 10
	[tkgl INFO    ]  Initializing VNC server...
	13/12/2023 23:57:11 Listening for VNC connections on TCP port 5900
	13/12/2023 23:57:11 Listening for VNC connections on TCP6 port 5900
