# WSHOT

Minimal screenshot utility for wayland with C.

You can also check out [“How to build a simple screenshot tool for Wayland.”](https://0l3d.github.io/wshot/) 

## FEATURES
- Region selection with slurp. `wshot -r "$(slurp)"`
- Hide cursor when screenshot. `wshot -c`
- Output file name selection. `wshot -o <filename>`
- Delay before taking the screenshot. `wshot -w 5 # 5 seconds`  
- Usage with wl-copy: region `wshot -s -r "$(slurp)" | wl-copy` or fullscreen `wshot -s | wl-copy`  
You might be thinking, 'These aren't features!'

Yes, these really aren't features — because for a screenshot program, these are simply all I need.

## Usage
```bash
git clone https://github.com/0l3d/wshot.git
make
./wshot -h
```

## LICENSE
This project is licensed under the **GPL-3.0 License**.

## Author 
Created by **0l3d**

# REFERANCES
- Wayland Docs: *https://wayland.freedesktop.org/docs/html/*
- Archlinux Wiki (Screen Capture): *https://wiki.archlinux.org/title/Screen_capture*
- Wayland Protocols (wlr-screencopy-unstable): *https://hoyon.github.io/wayland-protocol-docs/protocols/wlr_screencopy_unstable_v1.html*
- FreeDesktop (wlr-screencopy): *https://wlroots-e7fb81.pages.freedesktop.org/wlr/types/wlr_screencopy_v1.h.html*
