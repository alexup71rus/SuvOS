.PHONY: all assets initramfs initramfs-core cpp suvosd run run-core test test-core test-full clean distclean

all: initramfs

assets:
	python3 tools/fetch_alpine_assets.py

cpp:
	scripts/build-cpp-demo.sh

suvosd:
	scripts/build-suvosd.sh

initramfs:
	scripts/build-initramfs.sh

initramfs-core:
	SUVOS_WITH_RUNTIMES=0 scripts/build-initramfs.sh

run: all
	scripts/run-suvos.sh

run-core: initramfs-core
	scripts/run-suvos.sh

test: test-core

test-core: initramfs-core
	SUVOS_TEST_PROFILE=core scripts/test-boot.sh

test-full: initramfs
	SUVOS_TEST_PROFILE=full scripts/test-boot.sh

clean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build/rootfs build/initramfs build/cpp

distclean:
	chmod -R u+w build/rootfs 2>/dev/null || true
	rm -rf build
