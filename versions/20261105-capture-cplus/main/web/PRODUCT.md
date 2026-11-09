# RM P4 camera viewer

## Platform
Mobile web on REDMAGIC NX729J, Android 15. Local Wi-Fi only.

## User and task
The robot operator watches whether the claw holds an object. Existing on-board AprilTag recognition and GPIO outputs have priority.

## Scope
One USB camera. This candidate preserves 640x480 YUY2 at 10 fps and adds a 320x240 grayscale preview. The viewer must mark stale images and must not control recognition or lights.

## Stack
The user asked to proceed with the prior local-browser proposal. Plain offline HTML, CSS and JavaScript embedded in the ESP-IDF application. No remote fonts, images, account or cloud service. Camera hardware data are required to fill the image surface; no synthetic image is represented as live.

## Execution constraint
The user explicitly requested no runtime or visual tests. The board is unplugged and the current instruction is to finish code and compilation, then stop before flashing.
