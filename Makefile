CC = gcc
CFLAGS = -Wall -pedantic -o3
TARGET = main
SRCS = main.c

all: $(TARGET) help

$(TARGET):
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)
	@echo "Build successful!"

run: $(TARGET)
	./$(TARGET) output.txt 5

clean:
	rm -f $(TARGET)
	@echo "Cleaned up successfully!"

help:
	@echo "Usage: ./$(TARGET) <filename> <amount of processes>"
	@echo "Example Run: make run"
