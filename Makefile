CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic

TARGET := welcome-home
OBJ := welcome-home.o xdg.o data.o terminal.o

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f tests/XDG_DATA_HOME/welcome-home/.data
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	rm -f tests/XDG_DATA_HOME/welcome-home/.data
	env -i XDG_CONFIG_DIRS=$(shell pwd)/tests/XDG_CONFIG_DIRS XDG_DATA_HOME=$(shell pwd)/tests/XDG_DATA_HOME ./welcome-home -a -d
	rm -f tests/XDG_DATA_HOME/welcome-home/.data
	env -i XDG_CONFIG_HOME=$(shell pwd)/tests/XDG_CONFIG_HOME_EMPTY XDG_CONFIG_DIRS=$(shell pwd)/tests/XDG_CONFIG_DIRS XDG_DATA_HOME=$(shell pwd)/tests/XDG_DATA_HOME ./welcome-home -a -d
	rm -f tests/XDG_DATA_HOME/welcome-home/.data
	env -i XDG_CONFIG_HOME=$(shell pwd)/tests/XDG_CONFIG_HOME XDG_CONFIG_DIRS=$(shell pwd)/tests/XDG_CONFIG_DIRS XDG_DATA_HOME=$(shell pwd)/tests/XDG_DATA_HOME ./welcome-home -a -d
