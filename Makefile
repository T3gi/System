IMAGE ?= stm32-build
DOCKER ?= docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $(shell id -u):$(shell id -g) $(IMAGE)
UART_PORT ?= /dev/ttyACM0
UART_BAUD ?= 115200

build:
	$(DOCKER) make -C firmware all

# Host unit tests (tests/README.md), no board needed.
test:
	$(DOCKER) make -C tests test

coverage:
	$(DOCKER) make -C tests coverage

flash:
	st-flash write firmware/build/firmware.bin 0x08000000
	
monitor:
	picocom -b 115200 --imap lfcrlf /dev/ttyACM0

