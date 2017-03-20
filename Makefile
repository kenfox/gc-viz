
#ALGO=NO_GC
#ALGO=REF_COUNT_GC
ALGO=MARK_SWEEP_GC
#ALGO=MARK_COMPACT_GC
#ALGO=COPY_GC

$(ALGO).gif: dkp.exe
	./dkp.exe data/dkp.log-big > frames.js
	rm -f $(ALGO).gif
	convert -loop 1 -delay 3 *.xpm $(ALGO).gif
	mkdir raw -p
	mv -f *.xpm raw

dkp.exe: Makefile dkp.cc
	g++ -D$(ALGO)=1 -o dkp.exe dkp.cc
