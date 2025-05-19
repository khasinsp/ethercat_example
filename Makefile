CC = gcc
CFLAGS = -Wall -O2 -I./include -I/opt/etherlab/include
LDFLAGS = -L/opt/etherlab/lib -lethercat -lpthread
TARGET = ./devel/ft_sensor

all: $(TARGET)

$(TARGET): ./src/ft_sensor.c
	$(CC) $(CFLAGS) -o $(TARGET) ./src/ft_sensor.c $(LDFLAGS)

clean: rm -f $(TARGET)