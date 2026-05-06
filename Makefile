CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2
TARGET = myinit
SOURCES = myinit.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
	rm -f /tmp/myinit.log

test: $(TARGET)
	./runme.sh

run: $(TARGET)
	./$(TARGET) /tmp/myinit_test/config.txt

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

help:
	@echo "Available targets:"
	@echo "  make        - Build the program"
	@echo "  make clean  - Remove compiled files"
	@echo "  make test   - Run the test script"
	@echo "  make run    - Run myinit with default config"
	@echo "  make install- Install to /usr/local/bin"