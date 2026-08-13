CC ?= gcc

CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Werror -fPIC -fvisibility=hidden

LDFLAGS += -shared
LDLIBS += -ldl

TARGET := liblinuwux_runtime.so

SOURCES := \
	src/runtime.c \
	src/signals.c \
	src/cpuid.c \
	src/syscall.c \
	src/kuser.c \
	src/time.c \
	src/registry.c \
	src/sud.c \
	src/prctl.c

ASM_SOURCES := \
	src/prctl_entry.S

OBJECTS := $(SOURCES:.c=.o) $(ASM_SOURCES:.S=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

clean:
	rm -f $(OBJECTS) $(TARGET)
