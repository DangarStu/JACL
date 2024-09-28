# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2

# Directories
CGIHTML_DIR = cgihtml-1.69
GLKTERM_DIR = glkterm
GLKTERMW_DIR = glktermw
CHEAPGLK_DIR = CheapGlk

# Source files for each target
COMMON_SRC = findroute.c interpreter.c loader.c logging.c parser.c display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c

# Targets
all: jacl cgijacl fcgijacl bjorb cjacl

# Bjorb target
bjorb: bjorb.c
	$(CC) $(CFLAGS) -o bjorb bjorb.c

# Jacl target
jacl: libglkterm.a
	$(CC) $(CFLAGS) -DGLK jacl.c glk_startup.c $(COMMON_SRC) glk_saver.c -I$(GLKTERM_DIR) -L$(GLKTERM_DIR) -lglkterm -lm -lncurses -o jacl

# Cjacl target
cjacl: libcheapglk.a
	$(CC) $(CFLAGS) -DGLK jacl.c glk_startup.c $(COMMON_SRC) glk_saver.c -I$(CHEAPGLK_DIR) -L$(CHEAPGLK_DIR) -lcheapglk -lm -o cjacl

# Cgijacl target
cgijacl: libcgihtml.a webjacl.o
	$(CC) $(CFLAGS) -DWEBJACL cgijacl.c $(COMMON_SRC) saver.c webjacl.o -I$(CGIHTML_DIR) -Iwebjacl -L$(CGIHTML_DIR) -lcgihtml -lm -o cgijacl

# Fcgijacl target
fcgijacl: libcgihtml.a
	$(CC) $(CFLAGS) -DFCGIJACL cgijacl.c $(COMMON_SRC) saver.c -I$(CGIHTML_DIR) -L$(CGIHTML_DIR) -lcgihtml -lfcgi -lm -I/opt/homebrew/include -L/opt/homebrew/lib -o fcgijacl

# webjacl.o target
webjacl.o: webjacl.c webjacl.h language.h
	$(CC) $(CFLAGS) -c webjacl.c -o webjacl.o

# External libraries targets
libcheapglk.a:
	cd $(CHEAPGLK_DIR) && make libcheapglk.a

libglktermw.a:
	cd $(GLKTERMW_DIR) && make libglktermw.a

libglkterm.a:
	cd $(GLKTERM_DIR) && make libglkterm.a

libcgihtml.a:
	cd $(CGIHTML_DIR) && make libcgihtml.a

# Install binaries to the bin directory
install: all
	mv jacl ../bin
	mv bjorb ../bin
	mv cgijacl ../bin
	mv fcgijacl ../bin
	mv cjacl ../bin

# Clean up
clean:
	rm -f *.o
	rm -f cgijacl fcgijacl bjorb jacl cjacl
	cd $(CGIHTML_DIR) && make clean
	cd $(GLKTERM_DIR) && make clean
	cd $(GLKTERMW_DIR) && make clean
	cd $(CHEAPGLK_DIR) && make clean

