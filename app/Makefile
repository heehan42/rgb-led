TARGET := app
SRC := $(shell pwd)
BUILD_DIR := $(SRC)/build

.PHONY: all clean

all:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) main.c -o $(BUILD_DIR)/$(TARGET) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
