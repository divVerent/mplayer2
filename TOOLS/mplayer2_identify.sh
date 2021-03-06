#!/bin/sh

# file identification script
#
# manual usage:
#   mplayer2_identify.sh foo.mkv
#
# sh/dash/ksh/bash usage:
#   . mplayer2_identify.sh FOO_ foo.mkv
# will fill properties into variables like FOO_length
#
# zsh usage:
#   mplayer2_identify() { emulate -L sh; . mplayer2_identify.sh "$@"; }
#   mplayer2_identify FOO_ foo.mkv
# will fill properties into variables like FOO_length
#
# When multiple files were specified, their info will be put into FOO_* for the
# first file, FOO_1_* for the second file, FOO_2_* for the third file, etc.

case "$0" in
    mplayer2_identify.sh|*/mplayer2_identify.sh)
        # we are NOT being sourced
        case "$1" in
            '')
                ;;
            *)
                set -- '' "$@"
                ;;
        esac
        ;;
esac

if [ $# -lt 2 ]; then
    echo >&2 "Usage 1 (for humans only): $0 filename.mkv"
    echo >&2 "will print all property values."
    echo >&2 "Note that this output really shouldn't be parsed, as the"
    echo >&2 "format is subject to change."
    echo >&2
    echo >&2 "Usage 2 (for use by scripts): see top of this file"
    echo >&2
    echo >&2 "NOTE: for mkv with ordered chapters, this may"
    echo >&2 "not always identify the specified file, but the"
    echo >&2 "file providing the first chapter. Specify"
    echo >&2 "--no-ordered-chapters to prevent this."
    exit 1
fi

__midentify__LF="
"

__midentify__nextprefix=$1
shift

if [ -n "$__midentify__nextprefix" ]; then
    # in case of error, we always want this unset
    eval unset $__midentify__nextprefix'path'
fi

__midentify__allprops="
    filename
    path
    stream_start
    stream_end
    stream_length

    demuxer

    switch_program
    length
    chapters
    editions
    titles

    switch_audio
    audio_bitrate
    audio_codec
    audio_format
    channels
    samplerate

    switch_video
    angle
    video_bitrate
    video_codec
    video_format
    aspect
    fps
    width
    height

    sub
"
# TODO add metadata support once mplayer can do it

__midentify__propstr="X-MIDENTIFY-START:\\n"
for __midentify__key in $__midentify__allprops; do
    __midentify__propstr=$__midentify__propstr"X-MIDENTIFY: $__midentify__key \${=$__midentify__key}\\n"
    eval unset $__midentify__nextprefix$__midentify__key
done

__midentify__output=`mplayer --playing-msg="$__midentify__propstr" --vo=null --ao=null --frames=0 "$@"`
__midentify__fileindex=0
__midentify__prefix=
while :; do
    case "$__midentify__output" in
        '')
            break
            ;;
        *$__midentify__LF*)
            __midentify__line=${__midentify__output%%$__midentify__LF*}
            __midentify__output=${__midentify__output#*$__midentify__LF}
            ;;
        *)
            __midentify__line=$__midentify__output
            __midentify__output=
            ;;
    esac
    case "$__midentify__line" in
        X-MIDENTIFY-START:)
            if [ -n "$__midentify__nextprefix" ]; then
                __midentify__prefix=$__midentify__nextprefix
                if [ $__midentify__fileindex -gt 0 ]; then
                    __midentify__nextprefix=${__midentify__prefix%$__midentify__fileindex\_}
                fi
                __midentify__fileindex=$(($__midentify__fileindex+1))
                __midentify__nextprefix=$__midentify__nextprefix$__midentify__fileindex\_
                for __midentify__key in $__midentify__allprops; do
                    eval unset $__midentify__nextprefix$__midentify__key
                done
            else
                if [ $__midentify__fileindex -gt 0 ]; then
                    echo
                fi
                __midentify__fileindex=$(($__midentify__fileindex+1))
            fi
            ;;
        X-MIDENTIFY:\ *)
            __midentify__key=${__midentify__line#X-MIDENTIFY:\ }
            __midentify__value=${__midentify__key#* }
            __midentify__key=${__midentify__key%% *}
            if [ -n "$__midentify__nextprefix" ]; then
                if [ -z "$__midentify__prefix" ]; then
                    echo >&2 "Got X-MIDENTIFY: without X-MIDENTIFY-START:"
                elif [ -n "$__midentify__value" ]; then
                    eval $__midentify__prefix$__midentify__key=\$__midentify__value
                fi
            else
                if [ -n "$__midentify__value" ]; then
                    echo "$__midentify__key=$__midentify__value"
                fi
            fi
            ;;
    esac
done

unset __midentify__fileindex
unset __midentify__allprops
unset __midentify__key
unset __midentify__LF
unset __midentify__line
unset __midentify__output
unset __midentify__nextprefix
unset __midentify__prefix
unset __midentify__propstr
unset __midentify__value
