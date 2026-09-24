IMAGE ?= stm32-build
build:
	docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $(shell id -u):$(shell id -g) $(IMAGE) \
		make -C firmware all

flash:
	st-flash write firmware/build/firmware.bin 0x08000000
	
monitor:
	picocom -b 115200 --imap lfcrlf /dev/ttyACM0