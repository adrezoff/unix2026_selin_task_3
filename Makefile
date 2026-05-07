CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = myinit
SRC = myinit.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
	rm -f *.cfg
	rm -f in out1 out2 out3 out_single