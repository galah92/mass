CC = gcc
CFLAGS = -std=c11 -O3 -Wall -Wextra -fopenmp
TARGET = parsort
SRC = parsort.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lm

clean:
	rm -f $(TARGET) output.txt

test: $(TARGET)
	./$(TARGET) 4 input.txt > output.txt
	tail -n +2 output.txt | diff - sorted.txt
	@echo "Test passed!"

zip:
	zip submission.zip $(SRC) Makefile build

.PHONY: all clean test pack
