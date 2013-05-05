all: rip-flac flac-mp3

%: %.c
	gcc -Wall -g $(shell pkg-config --cflags --libs gstreamer-0.10 libmusicbrainz3) $< -o $@


install:
	cp rip-flac flac-mp3 /home/jhl/bin/
