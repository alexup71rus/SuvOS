.PHONY: all assets initramfs initramfs-core cpp suvosd suvosctl suvos-gateway suvos-splash ui ui-check ui-fix run run-core run-graphics run-core-graphics test test-core test-full clean distclean

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

initramfs:
	scripts/build-initramfs.sh

initramfs-core:
	SUVOS_WITH_RUNTIMES=0 scripts/build-initramfs.sh

run: all
	scripts/run-suvos.sh

run-core: initramfs-core
	scripts/run-suvos.sh

run-graphics: all
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

run-core-graphics: initramfs-core
	SUVOS_DISPLAY=cocoa SUVOS_VGA=std SUVOS_APPEND="console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 vga=791 suvos.graphics=1" scripts/run-suvos.sh

test: test-core

test-core: initramfs-core
	SUVOS_TEST_PROFILE=core scripts/test-boot.sh

test-full: initramfs
	SUVOS_TEST_PROFILE=full scripts/test-boot.sh

clean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build/rootfs build/initramfs build/cpp build/suvosd build/suvosctl build/suvos-gateway build/suvos-splash build/ui build/test-boot*.log

distclean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build
