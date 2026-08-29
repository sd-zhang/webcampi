# Webcam Pi

An embedded Linux image that turns a Raspberry Pi Zero (v1.3, BCM2835/ARMv6) and camera module into a plug-and-play USB webcam with **hardware-accelerated MJPEG encoding**.

## Table of Contents

1. [Required Hardware](#required-hardware)
2. [Supported Camera Sensors](#supported-camera-sensors)
3. [Features](#features)
4. [Installation](#installation)
5. [Setup](#setup)
6. [Usage](#usage)
7. [Configuration](#configuration)
8. [Building](#building)
9. [Credits](#credits)

## Required Hardware

| Part                    | Description                        |
| ----------------------- | ---------------------------------- |
| Raspberry Pi Zero (v1.3)| Main compute module (BCM2835/ARMv6)|
| Supported camera sensor | CSI-2 camera module                |
| microSD card (≥100 MB)  | System storage        |
| Micro-USB cable         | USB-OTG data & power  |
| Case (optional)         | Protection & mounting |

## Supported Camera Sensors

- OV5647\*
- IMX219\*
- IMX708
- IMX477\*
- IMX500\*
- IMX296\*
- IMX290\*
- IMX519\*

**\*: Untested** – only the IMX708 has been verified to work. Other sensors are included in the kernel and supported by libcamera, but your mileage may vary.

## Features

- **Hardware MJPEG encoding:** This image specifically targets the original single-core **Pi Zero** (ARM1176 / ARMv6), which is far too slow to compress 720p/1080p video in software. Instead, camera frames are JPEG-compressed by the Pi's **VideoCore hardware encoder** (the `bcm2835-codec` block on `/dev/video11`) and streamed as MJPEG. The frames are handed to the encoder **zero-copy over DMA-BUF**, straight from the camera ISP, so the ARM CPU never touches pixel data and stays nearly idle while streaming. A libjpeg software path exists only as a fallback if no hardware encoder is found.
- **Runtime configuration:** Resolution, field of view, JPEG quality and the status LED are read at boot from a plain-text file on the SD card's boot partition — editable from any Mac/PC, no rebuild or shell required. See [Configuration](#configuration).
- **Fast boot:** Less than 6 seconds to go from plugged in to functional.
- **Running status:** [GPIO 23](https://pinout.xyz/pinout/pin16_gpio23) goes `HIGH` whenever uvc-gadget is active (also drives the optional logo LED; see `logo_light`).
- **Streaming status:** [GPIO 24](https://pinout.xyz/pinout/pin18_gpio24) goes `HIGH` whenever video is being streamed to host.
- **Pause/resume:** [GPIO 26](https://pinout.xyz/pinout/pin37_gpio26) freezes video on `HIGH` while keeping the stream alive.

## Installation

1. Download the latest image from the [Releases](https://github.com/sd-zhang/webcampi/releases) page.
2. Insert your microSD card into your host computer.
3. Flash the image:
	- Linux/macOS:
   ```bash
   # Replace /dev/sdX with your card device
   sudo dd if=webcampi-<version>.img of=/dev/sdX
   ```
	- Windows:
   ```
   Use a tool like Balena Etcher or Raspberry Pi Imager
   ```

**Alternatively**, if you already have a Linux image on a Pi that supports OTG you can skip Webcam Pi and just follow the generic UVC-gadget setup guide: [uvc-gadget#installation](https://github.com/elcalzado/uvc-gadget#Installation)

## Setup

1. Insert the flashed microSD card into the Pi.
2. Connect the camera to the Pi’s camera port and secure the ribbon cable.
3. Plug a Micro-USB cable into the “USB” OTG port of the Pi and the other end into your host USB port.
4. On your host machine, a new video device should appear.

## Usage

Once the gadget is running, any standard UVC-compatible tool can access it:

```
Any webcam-enabled app: Zoom, OBS, Discord, etc. just pick the Webcam Pi as your camera.
```
...or...
```bash
# List video devices
v4l2‐ctl --list-devices

# Stream with VLC
vlc v4l2:///dev/video0

# Record with ffmpeg
ffmpeg -f v4l2 -i /dev/video0 -t 00:00:10 output.mkv
```

## Configuration

The camera reads its settings at boot from **`isight.json` on the FAT boot partition** — the same volume your host mounts when you plug the SD card into a Mac or PC. Edit the file, save, eject, and reboot the Pi; there is no web UI or login required. The file is created with defaults on first boot, and if it is ever missing or invalid the camera falls back to safe built-in values and still boots.

Default `isight.json`:

```json
{
  "resolutions": ["1280x720", "1920x1080"],
  "fov": 75,
  "logo_light": "activity",
  "quality": 72
}
```

| Key           | Values                       | Default                        | Description |
| ------------- | ---------------------------- | ------------------------------ | ----------- |
| `resolutions` | list of `"WxH"`              | `["1280x720","1920x1080"]`     | The MJPEG modes advertised to the host over USB. The host can only select from this list; the first entry is the default mode. |
| `fov`         | integer degrees              | `75`                           | Field of view. The full sensor is ~75°; a smaller value crops the centre via the ISP for an optical-style zoom at no CPU cost. `75` or higher keeps the full frame. |
| `quality`     | `1`–`100`                    | `72`                           | JPEG quality handed to the hardware encoder. Higher means better image quality and larger frames. |
| `logo_light`  | `off` / `on` / `activity`    | `activity`                     | Optional rear logo LED on GPIO 23. `activity` mirrors the streaming indicator (GPIO 24); `on`/`off` hold it steady. |

## Building

### Prerequisites

- Linux host (WSL is fine)
- Git and buildroot dependencies

### Clone

```bash
git clone --recursive https://github.com/sd-zhang/webcampi.git
cd webcampi
```

### Build

```bash
# This will configure Buildroot and compile the kernel, rootfs, and image
./build.sh
```

When complete, the final SD card image will be in:

```
buildroot/output/images/sdcard.img
```

Flash it as described in [Installation](#installation).

## Credits

- [uvc-gadget](https://github.com/elcalzado/uvc-gadget): Fork of the main uvc-gadget with added GPIO functionality.
- [showmewebcam](https://github.com/showmewebcam/showmewebcam): Inspiration for this project and learning resource.
- [Buildroot](https://gitlab.com/buildroot.org/buildroot)
