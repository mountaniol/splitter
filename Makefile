ALL_O=$(subst .c,.o,$(shell ls *.c ))
T=splitter

GCC=gcc
SPLIT_O=split.o
HTTP_O=html.o
HTTP_T=html.da
INCLUDE=
CFLAGS=-g3 -Wall -D_FILE_OFFSET_BITS=64 

all: $(SPLIT_O)
	gcc $(CFLAGS) $(SPLIT_O) -o $(T)

http: $(HTTP_O)
	gcc $(CFLAGS) `curl-config --cflags` $(HTTP_O) -o $(HTTP_T) `curl-config --libs`

clean:
	rm -f $(ALL_O) $(T)
	
# Building rules
%.o:%.c
	@echo building $@...
	$(GCC) -g $(INCLUDE) $(CFLAGS) -c -o $@ $<
	@echo $@ done.
