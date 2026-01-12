CC ?= ${CROSS_COMPILE}gcc
CCFLAGS ?= -g -Wall -Werror -DUSE_AESD_CHAR_DEVICE
LDFLAGS ?= -lpthread -lrt
SRC_DIR ?= src
OBJ_DIR ?= obj
TARGET ?= aesdsocket

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

all: $(TARGET)

# Linking object files to create the final executable
$(TARGET): $(OBJS)
	mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

# Compiling source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean