.PHONY: all

all: compile_w_ssl

compile_w_ssl:
	gcc -ffreestanding \
	web.c src/*.c \
	/usr/lib/libclibp.a \
	-lssl -lcrypto


lib:
	lbg -c --output a.out \
    src/webserver.c \
    src/parser.c \
    src/router.c \
    src/utils.c
	ar rcs libweb.a *.o
	cp libweb.a /usr/lib
	rm *.o

# Bloatware
NOSSL:
	lbg --output a.out \
	tests/web.c \
	src/webserver.c \
	src/parser.c \
	src/router.c \
	src/utils.c 
