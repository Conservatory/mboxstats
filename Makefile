VERSION=3.1

DEBUG=-g # -pg

CXXFLAGS+=-Wall -O2 -DVERSION=\"$(VERSION)\" $(DEBUG)
CFLAGS+=-Wall -O2 -DVERSION=\"$(VERSION)\" $(DEBUG)
LDFLAGS+=-lm -lstdc++ $(DEBUG)

OBJS=array.o funcs.o main.o io.o mem.o val.o br.o

all: mboxstats

mboxstats: $(OBJS)
	$(CC) -Wall -W $(OBJS) $(LDFLAGS) -o mboxstats

install: mboxstats
	cp mboxstats /usr/local/bin
	#
	# you might want to run 'make thanks' now :-)

clean:
	rm -f $(OBJS) mboxstats core

package: clean
	mkdir mboxstats-$(VERSION)
	cp *.c* *.h Makefile license.txt mboxstats-$(VERSION)
	tar czf mboxstats-$(VERSION).tgz mboxstats-$(VERSION)
	rm -rf mboxstats-$(VERSION)

thanks:
	echo Automatic thank you e-mail for mboxstats $(VERSION) on a `uname -a` | mail -s "mboxstats $(VERSION)" folkert@vanheusden.com
	echo
	echo Oh, blatant plug: http://keetweej.vanheusden.com/wishlist.html
