.PHONY: all assets initramfs initramfs-core initramfs-gui cpp suvosd suvosctl suvos-gateway suvos-splash ui ui-check ui-fix run run-core run-graphics run-core-graphics run-gui test test-core test-full test-gui-smoke test-gui-resolutions clean clean-layer-cache distclean

SUVOS_GUI_SIZE ?= $(shell scripts/detect-gui-size.sh)
SUVOS_GUI_WIDTH ?= $(word 1,$(SUVOS_GUI_SIZE))
SUVOS_GUI_HEIGHT ?= $(word 2,$(SUVOS_GUI_SIZE))
SUVOS_GUI_VIDEO_DEVICE ?= virtio-vga,xres=$(SUVOS_GUI_WIDTH),yres=$(SUVOS_GUI_HEIGHT),edid=on
SUVOS_GUI_INPUT_DEVICES ?= -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 -device usb-mouse,bus=xhci.0
SUVOS_GUI_AUDIO_DEVICES ?= -audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1

all: initramfs

assets:
	python3 tools/fetch_alpine_assets.py

cpp:
	scripts/build-cpp-demo.sh

suvosd:
	scripts/build-suvosd.sh

suvosctl:
	scripts/build-suvosctl.sh

suvos-gateway:
	scripts/build-suvos-gateway.sh

suvos-splash:
	scripts/build-suvos-splash.sh

ui:
	scripts/build-ui.sh

ui-check:
	npm run ui:check

ui-fix:
	npm run ui:fix

initramfs: ui
	scripts/build-initramfs.sh

initramfs-core: ui
	SUVOS_WITH_RUNTIMES=0 scripts/build-initramfs.sh

initramfs-gui: ui
	SUVOS_WITH_RUNTIMES=1 SUVOS_WITH_GUI=1 scripts/build-initramfs.sh

run: all
	scripts/run-suvos.sh

run-core: initramfs-core
	scripts/run-suvos.sh

run-graphics: all
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

run-core-graphics: initramfs-core
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

run-gui: initramfs-gui
	@echo "SuvOS GUI size: $(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)"
	SUVOS_MEMORY=3072M SUVOS_CPUS=4 SUVOS_DISPLAY=cocoa SUVOS_VIDEO_DEVICE="$(SUVOS_GUI_VIDEO_DEVICE)" SUVOS_EXTRA_QEMU_ARGS="$(SUVOS_GUI_INPUT_DEVICES) $(SUVOS_GUI_AUDIO_DEVICES)" SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 suvos.graphics=1 suvos.gui=1 suvos.render=qemu-tcg" scripts/run-suvos.sh

test: test-core

test-core: initramfs-core
	SUVOS_TEST_PROFILE=core scripts/test-boot.sh

test-full: initramfs
	SUVOS_TEST_PROFILE=full scripts/test-boot.sh

test-gui-smoke: initramfs-gui
	scripts/test-gui-smoke.sh

test-gui-resolutions: initramfs-gui
	SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768 scripts/test-gui-smoke.sh
	SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900 scripts/test-gui-smoke.sh

clean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build/rootfs build/initramfs build/cpp build/suvosd build/suvosctl build/suvos-gateway build/suvos-splash build/ui build/test-boot*.log build/test-gui-smoke.log build/run-gui-smoke.log

clean-layer-cache:
	rm -rf build/cache/rootfs-layers build/cache/apk

distclean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build
