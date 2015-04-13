OBJS=raspi2png.o
BIN=raspi2png

CFLAGS+=-Wall -g -O3 $(STAGING_DIR)/usr/bin/libpng-config --cflags
LDFLAGS+=-L/opt/vc/lib/ -lbcm_host `$(STAGING_DIR)/usr/bin/libpng-config --ldflags` -lm

INCLUDES+=-I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux

all: $(BIN)

%.o: %.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

$(BIN): $(OBJS)
	$(CC) -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

clean:
	@rm -f $(OBJS)
	@rm -f $(BIN)
