.PHONY: all assets bootstrap-vendors initramfs initramfs-core initramfs-gui initramfs-aec initramfs-aec-arm64 initramfs-dev initramfs-dev-arm64 suvosd suvosctl suvos-gateway suvos-splash ui ui-check ui-fix aec chromium chromium-dev-gateway run runos run-qemu run-qemu-x86 run-arm64 run-parallels run-dev run-dev-qemu-x86 run-dev-arm64 run-console run-core run-graphics run-core-graphics run-gui run-gui-aec test test-core test-full test-dev test-gui-smoke test-gui-resolutions test-aec-smoke clean clean-layer-cache distclean

SUVOS_HOST_ARCH ?= $(shell uname -m)
SUVOS_HOST_OS ?= $(shell uname -s)
RUNOS_BACKEND ?= $(if $(filter arm64 aarch64,$(SUVOS_HOST_ARCH)),arm64,qemu)
SUVOS_QEMU_X86_MEMORY ?= 4096M
SUVOS_QEMU_X86_CPUS ?= 4
SUVOS_ARM64_MEMORY ?= 8192M
SUVOS_ARM64_CPUS ?= 8
SUVOS_AEC_TERMINAL_GPU ?= off
SUVOS_QEMU_DISPLAY ?= $(if $(filter Darwin,$(SUVOS_HOST_OS)),cocoa,gtk)
SUVOS_QEMU_AUDIO_BACKEND ?= $(if $(filter Darwin,$(SUVOS_HOST_OS)),coreaudio,pa)
SUVOS_GUI_SIZE ?= $(shell scripts/detect-gui-size.sh)
SUVOS_GUI_WIDTH ?= $(word 1,$(SUVOS_GUI_SIZE))
SUVOS_GUI_HEIGHT ?= $(word 2,$(SUVOS_GUI_SIZE))
SUVOS_GUI_VIDEO_DEVICE ?= virtio-vga,xres=$(SUVOS_GUI_WIDTH),yres=$(SUVOS_GUI_HEIGHT),edid=on
SUVOS_ARM64_GUI_VIDEO_DEVICE ?= virtio-gpu-pci,xres=$(SUVOS_GUI_WIDTH),yres=$(SUVOS_GUI_HEIGHT),edid=on
SUVOS_GUI_CONNECTOR ?= Virtual-1
SUVOS_GUI_KERNEL_VIDEO ?= video=$(SUVOS_GUI_CONNECTOR):$(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)-32
SUVOS_GUI_INPUT_DEVICES ?= -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 -device usb-mouse,bus=xhci.0
SUVOS_GUI_AUDIO_DEVICES ?= -audiodev $(SUVOS_QEMU_AUDIO_BACKEND),id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1

all: initramfs

assets:
	python3 tools/fetch_alpine_assets.py

bootstrap-vendors:
	scripts/bootstrap-vendors.sh

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

aec:
	scripts/build-aec.sh

chromium:
	scripts/build-chromium.sh

chromium-dev-gateway:
	scripts/run-chromium-dev-gateway.sh

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

initramfs-aec: ui aec
	SUVOS_WITH_RUNTIMES=0 SUVOS_WITH_GUI=1 SUVOS_WITH_AEC=1 scripts/build-initramfs.sh

initramfs-aec-arm64: ui
	SUVOS_ARCH=aarch64 SUVOS_WITH_RUNTIMES=0 SUVOS_WITH_GUI=1 SUVOS_WITH_AEC=1 scripts/build-initramfs.sh

initramfs-dev: ui
	SUVOS_WITH_RUNTIMES=0 SUVOS_WITH_GUI=1 SUVOS_WITH_AEC=1 SUVOS_WITH_DEVTOOLS=1 scripts/build-initramfs.sh

initramfs-dev-arm64: ui
	SUVOS_ARCH=aarch64 SUVOS_WITH_RUNTIMES=0 SUVOS_WITH_GUI=1 SUVOS_WITH_AEC=1 SUVOS_WITH_DEVTOOLS=1 scripts/build-initramfs.sh

run: runos

runos:
	$(MAKE) run-$(RUNOS_BACKEND)

run-qemu: run-qemu-x86

run-qemu-x86: initramfs-aec
	@echo "SuvOS GUI size: $(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)"
	SUVOS_ARCH=x86_64 SUVOS_MEMORY=$(SUVOS_QEMU_X86_MEMORY) SUVOS_CPUS=$(SUVOS_QEMU_X86_CPUS) SUVOS_DISPLAY="$(SUVOS_QEMU_DISPLAY)" SUVOS_VIDEO_DEVICE="$(SUVOS_GUI_VIDEO_DEVICE)" SUVOS_EXTRA_QEMU_ARGS="$(SUVOS_GUI_INPUT_DEVICES) $(SUVOS_GUI_AUDIO_DEVICES)" SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 $(SUVOS_GUI_KERNEL_VIDEO) suvos.graphics=1 suvos.gui=1 suvos.aec=1 suvos.render=qemu-tcg" scripts/run-suvos.sh

run-arm64: initramfs-aec-arm64
	@echo "SuvOS arm64/HVF GUI size: $(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)"
	SUVOS_ARCH=aarch64 SUVOS_MEMORY=$(SUVOS_ARM64_MEMORY) SUVOS_CPUS=$(SUVOS_ARM64_CPUS) SUVOS_DISPLAY="$(SUVOS_QEMU_DISPLAY)" SUVOS_VIDEO_DEVICE="$(SUVOS_ARM64_GUI_VIDEO_DEVICE)" SUVOS_EXTRA_QEMU_ARGS="$(SUVOS_GUI_INPUT_DEVICES) $(SUVOS_GUI_AUDIO_DEVICES)" SUVOS_APPEND="console=ttyAMA0 rdinit=/init quiet loglevel=3 panic=-1 $(SUVOS_GUI_KERNEL_VIDEO) suvos.graphics=1 suvos.gui=1 suvos.aec=1 suvos.render=qemu-hvf suvos.aec_terminal_gpu=$(SUVOS_AEC_TERMINAL_GPU)" scripts/run-suvos.sh

run-dev: run-dev-$(RUNOS_BACKEND)

run-dev-qemu-x86: initramfs-dev
	@echo "SuvOS dev GUI size: $(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)"
	SUVOS_ARCH=x86_64 SUVOS_MEMORY=$(SUVOS_QEMU_X86_MEMORY) SUVOS_CPUS=$(SUVOS_QEMU_X86_CPUS) SUVOS_DISPLAY="$(SUVOS_QEMU_DISPLAY)" SUVOS_VIDEO_DEVICE="$(SUVOS_GUI_VIDEO_DEVICE)" SUVOS_EXTRA_QEMU_ARGS="$(SUVOS_GUI_INPUT_DEVICES) $(SUVOS_GUI_AUDIO_DEVICES)" SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 $(SUVOS_GUI_KERNEL_VIDEO) suvos.graphics=1 suvos.gui=1 suvos.aec=1 suvos.render=qemu-tcg" scripts/run-suvos.sh

run-dev-arm64: initramfs-dev-arm64
	@echo "SuvOS arm64/HVF dev GUI size: $(SUVOS_GUI_WIDTH)x$(SUVOS_GUI_HEIGHT)"
	SUVOS_ARCH=aarch64 SUVOS_MEMORY=$(SUVOS_ARM64_MEMORY) SUVOS_CPUS=$(SUVOS_ARM64_CPUS) SUVOS_DISPLAY="$(SUVOS_QEMU_DISPLAY)" SUVOS_VIDEO_DEVICE="$(SUVOS_ARM64_GUI_VIDEO_DEVICE)" SUVOS_EXTRA_QEMU_ARGS="$(SUVOS_GUI_INPUT_DEVICES) $(SUVOS_GUI_AUDIO_DEVICES)" SUVOS_APPEND="console=ttyAMA0 rdinit=/init quiet loglevel=3 panic=-1 $(SUVOS_GUI_KERNEL_VIDEO) suvos.graphics=1 suvos.gui=1 suvos.aec=1 suvos.render=qemu-hvf suvos.aec_terminal_gpu=$(SUVOS_AEC_TERMINAL_GPU)" scripts/run-suvos.sh

ifneq ($(filter arm64 aarch64,$(SUVOS_HOST_ARCH)),)
run-parallels: initramfs-aec-arm64
	SUVOS_ARCH=aarch64 scripts/run-suvos-parallels.sh
else
run-parallels: initramfs-aec
	scripts/run-suvos-parallels.sh
endif

run-console: all
	scripts/run-suvos.sh

run-core: initramfs-core
	scripts/run-suvos.sh

run-graphics: all
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

run-core-graphics: initramfs-core
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

run-gui: runos

run-gui-aec: runos

test: test-core

test-core: initramfs-core
	SUVOS_TEST_PROFILE=core scripts/test-boot.sh

test-full: initramfs
	SUVOS_TEST_PROFILE=full scripts/test-boot.sh

ifneq ($(filter arm64 aarch64,$(SUVOS_HOST_ARCH)),)
test-dev: initramfs-dev-arm64
	SUVOS_ARCH=aarch64 SUVOS_TEST_PROFILE=dev SUVOS_TEST_MEMORY=4096M SUVOS_TEST_CPUS=2 SUVOS_TEST_TIMEOUT=420 scripts/test-boot.sh

test-gui-smoke: initramfs-aec-arm64
	SUVOS_ARCH=aarch64 scripts/test-gui-smoke.sh

test-gui-resolutions: initramfs-aec-arm64
	SUVOS_ARCH=aarch64 SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768 scripts/test-gui-smoke.sh
	SUVOS_ARCH=aarch64 SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900 scripts/test-gui-smoke.sh

test-aec-smoke: initramfs-aec-arm64
	SUVOS_ARCH=aarch64 SUVOS_TEST_PROFILE=aec SUVOS_TEST_MEMORY=4096M SUVOS_TEST_CPUS=2 SUVOS_TEST_TIMEOUT=420 SUVOS_APPEND_EXTRA="suvos.aec=1" scripts/test-boot.sh
else
test-dev: initramfs-dev
	SUVOS_TEST_PROFILE=dev SUVOS_TEST_MEMORY=4096M SUVOS_TEST_CPUS=2 SUVOS_TEST_TIMEOUT=420 scripts/test-boot.sh

test-gui-smoke: initramfs-aec
	scripts/test-gui-smoke.sh

test-gui-resolutions: initramfs-aec
	SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768 scripts/test-gui-smoke.sh
	SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900 scripts/test-gui-smoke.sh

test-aec-smoke: initramfs-aec
	SUVOS_TEST_PROFILE=aec SUVOS_TEST_MEMORY=4096M SUVOS_TEST_CPUS=2 SUVOS_TEST_TIMEOUT=420 SUVOS_APPEND_EXTRA="suvos.aec=1" scripts/test-boot.sh
endif

clean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build/rootfs build/initramfs build/cpp build/suvosd build/suvosctl build/suvos-gateway build/suvos-splash build/ui build/test-boot*.log build/test-gui-smoke.log build/run-gui-smoke.log

clean-layer-cache:
	rm -rf build/cache/rootfs-layers build/cache/apk

distclean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build
