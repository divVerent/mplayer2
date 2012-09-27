#!/bin/sh

trap 'killall mplayer; exit' INT

sox -n audio.wav synth 42 sin 440-880
for frametype in telecine progressive interlaced; do
	for size in full normal half quarter; do
		for norm in pal ntsc; do
			for aspect in 16_9 4_3; do
				f=$norm-$size-$aspect-$frametype.vob

				[ -f "$f" ] && continue

				case "$f" in
					pal-*-telecine.vob)
						# this combination does not exist
						continue
						;;
				esac

				set -- mplayer --endpos=42 --vf=dlopen=../vf_dlopen/test1.so,scale --demuxer=rawvideo /dev/zero --audiofile=audio.wav --ovcopts=qscale=5 --o="$f"

				case "$f" in
					pal-*)
						fps1=25
						fps2=50
						fps3=50
						;;
					ntsc-*-progressive.vob)
						fps1=24000/1001
						fps2=24000/1001
						fps3=48000/1001
						;;
					ntsc-*)
						fps1=24000/1001
						fps2=30000/1001
						fps3=60000/1001
						;;
				esac

				case "$f" in
					pal-full-*)
						w=720
						h=576
						;;
					pal-normal-*)
						w=704
						h=576
						;;
					pal-half-*)
						w=352
						h=576
						;;
					pal-quarter-*)
						w=352
						h=288
						;;
					ntsc-full-*)
						w=720
						h=480
						;;
					ntsc-normal-*)
						w=704
						h=480
						;;
					ntsc-half-*)
						w=352
						h=480
						;;
					ntsc-quarter-*)
						w=352
						h=240
						;;
				esac

				case "$f" in
					*-full-4_3-*)
						set -- "$@" --vf-pre=dsize=15/11
						;;
					*-4_3-*)
						set -- "$@" --vf-pre=dsize=4/3
						;;
					*-full-16_9-*)
						set -- "$@" --vf-pre=dsize=20/11
						;;
					*-16_9-*)
						set -- "$@" --vf-pre=dsize=16/9
						;;
				esac

				case "$f" in
					*-progressive.vob)
						fpsi=$fps1
						fpso=$fps1
						;;
					*-interlaced.vob)
						fpsi=$fps3
						fpso=$fps2
						set -- "$@" --vf-add=dlopen=../vf_dlopen/telecine.so:t:11
						;;
					*-telecine.vob)
						fpsi=$fps1
						fpso=$fps2
						set -- "$@" --vf-add=dlopen=../vf_dlopen/telecine.so:t:2332
						;;
				esac

				set -- "$@" --rawvideo=w=$w:h=$h:fps=$fpsi:format=rgb24 --profile=enc-to-dvd$norm --ofps=$fpso

				"$@" &
			done
		done
		wait
	done
done
