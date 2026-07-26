# Makefile wrapper for idf.py — convenience targets
# Usage: make build | make flash | make monitor | make clean

IDF_PATH ?= $(HOME)/esp/esp-idf
export IDF_PATH

PORT   ?= /dev/ttyACM0
BAUD   ?= 921600
TARGET ?= esp32s3

.PHONY: build flash monitor clean menuconfig set-target flash-monitor erase

build:
	idf.py build

flash:
	idf.py -p $(PORT) -b $(BAUD) flash

monitor:
	idf.py -p $(PORT) monitor

flash-monitor: flash
	idf.py -p $(PORT) monitor

clean:
	idf.py clean

fullclean:
	idf.py fullclean

menuconfig:
	idf.py menuconfig

set-target:
	idf.py set-target $(TARGET)

erase:
	idf.py -p $(PORT) erase-flash
