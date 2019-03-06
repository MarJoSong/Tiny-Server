#General Generate Binary Makefile(aka'GGBM')

SRCFILES = $(wildcard *.c)

server:$(SRCFILES)
	gcc $(SRCFILES) -o $@

clean:
	-rm -f server

cc:
	-rm -f server
	-rm -f makefile
