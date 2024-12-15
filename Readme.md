# ViSIE (Video Still Image Extractor)

ViSIE is a work-in-progress tool for extracting high-quality still images from video files with metadata preservation. It features efficient HEIC/HEIF image compression and comprehensive metadata handling, particularly for GoPro and DJI videos.

## Features

- Extract still frames from video files as HEIC/HEIF images
- Lossless image compression (when supported by hardware)
- Preserves video metadata in extracted stills
- Special support for:
  - GoPro cameras (GPS coordinates, speed, altitude, GPSDOP)
    - tested with GoPro Hero 7
  - DJI drones (GPS coordinates, aperture, shutter speed, ISO, exposure bias, digital zoom)
    - tested with DJI Mini 2
  - Apple iPhone
- Frame-by-frame navigation with arrow keys
- Interactive timeline slider

## Security Warning

⚠️ Only process video files that you created yourself or trust completely. Processing untrusted media files can pose security risks.

## Requirements

### macOS
- Homebrew package manager
- vcpkg package manager

### Windows
- vcpkg package manager

## Dependencies

- Qt6 (or Qt5)
- libheif 1.19.5 or later
- exiv2
- FFmpeg

## Usage

1. Launch ViSIE
2. Open a video file (File -> Open)
3. Use the timeline slider or arrow keys to navigate to desired frame
4. Save the current frame (File -> Save or Ctrl+S)
5. Find save image in the Pictures folder

## Metadata Support

ViSIE preserves extensive metadata from the source video, with special handling for:

### GoPro Cameras
- GPS coordinates
- Speed and altitude
- GPS DOP (Dilution of Precision)
- GPS timestamp
- Device name and model

### DJI Drones
- GPS coordinates
- Aperture value
- Shutter speed
- ISO settings
- Exposure bias
- Digital zoom ratio
- Speed data

Additional metadata preserved for all sources:
- Color space information
- Creation timestamps
- Device information
- Orientation data

## Current Limitations

- Work in progress, expect bugs and incomplete features
- Saved files are currently named visie-000 through visie-999
- Some stability issues when handling external storage devices

## License

MIT