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
