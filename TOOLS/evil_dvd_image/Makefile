all: evil.iso

../vf_dlopen/telecine.so: ../vf_dlopen/Makefile
	make -C ../vf_dlopen

../vf_dlopen/test1.so: ../vf_dlopen/Makefile
	make -C ../vf_dlopen

clean:
	$(RM) -r evil
	$(RM) audio.wav evil.iso *.vob

audio.wav:
	sox -n $@ synth 42 sin 440-880

vobs:
	sh generate.sh

evil/VIDEO_TS/VTS_01_1.VOB: vobs
	$(RM) -r evil
	dvdauthor -x evil.xml -o evil

evil.iso: evil/VIDEO_TS/VTS_01_1.VOB
	genisoimage -dvd-video -o evil.iso evil/
