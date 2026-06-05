.PHONY: all assets initramfs cpp suvosd run test clean distclean

all: initramfs

assets:
	python3 tools/fetch_alpine_assets.py

cpp:
	scripts/build-cpp-demo.sh

suvosd:
	scripts/build-suvosd.sh

initramfs:
	scripts/build-initramfs.sh

run: all
	scripts/run-suvos.sh

test: all
	scripts/test-boot.sh

clean:
	rm -rf build/rootfs build/initramfs build/cpp

distclean:
	rm -rf build
