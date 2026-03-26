CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lssl -lcrypto
DEPREC = -Wno-deprecated-declarations

TARGET = proxy
SRCS = proxy.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS) $(DEPREC)
# 	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

debug:
	$(MAKE) CFLAGS="$(CFLAGS) -g -DDEBUG"

clean:
	rm -f $(TARGET)


