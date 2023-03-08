all:
	arduino-cli compile --build-path build --build-cache-path build/cache --upload -v -p /dev/ttyUSB0
