SKETCH := led-matrix-audio-visualizer
# ELEGOO ESP-WROOM-32 dev board = generic "ESP32 Dev Module", 4 MB flash, no PSRAM
FQBN   ?= esp32:esp32:esp32:UploadSpeed=921600,CPUFreq=240,FlashSize=4M,PSRAM=disabled
PORT   ?= /dev/ttyUSB0
BAUD   ?= 115200
BUILD  := build

CORE := esp32:esp32@3.3.12
LIBS := \
	"ESP32 HUB75 LED MATRIX PANEL DMA Display@3.1.0" \
	"Adafruit GFX Library@1.13.0" \
	"arduinoFFT@2.0.4"

.PHONY: setup build upload monitor flash clean

setup:
	arduino-cli core update-index
	arduino-cli core install $(CORE)
	arduino-cli lib install $(LIBS)

build:
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD) $(SKETCH)

upload: build
	arduino-cli upload --fqbn $(FQBN) --port $(PORT) --input-dir $(BUILD) $(SKETCH)

monitor:
	arduino-cli monitor --port $(PORT) --config baudrate=$(BAUD)

flash: upload monitor

clean:
	rm -rf $(BUILD)
