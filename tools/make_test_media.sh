#!/usr/bin/env bash
# Generates the synthetic media set for htdemucs_format_matrix_check: a 30 s
# "music-like" stereo signal (bass, chord, tremolo lead, noise) in every
# container/codec the file chooser accepts, unusual sample formats, and the
# files that must be rejected. File names encode the expectation the check
# applies (see tests/format_matrix_check.cpp).
#
# Usage: tools/make_test_media.sh <output-dir> [ffmpeg.exe]
# The encoders (libx264, libvpx, libmp3lame, libvorbis, libopus, wmv2, aac,
# mpeg1video, mp2) need a full ffmpeg build; the LGPL build that ships with
# the app can decode all of them but not encode H.264/VP8/MP3.
set -euo pipefail
out=${1:?output directory}
ff=${2:-ffmpeg}
mkdir -p "$out"
cd "$out"

src="aevalsrc=exprs='0.35*sin(2*PI*55*t)*(0.6+0.4*sin(2*PI*1*t))+0.18*sin(2*PI*220*t)+0.12*sin(2*PI*330*t)*abs(sin(2*PI*2*t))+0.05*(random(0)-0.5)|0.35*sin(2*PI*55*t)*(0.6+0.4*sin(2*PI*1*t))+0.18*sin(2*PI*277*t)+0.12*sin(2*PI*440*t)*abs(sin(2*PI*3*t))+0.05*(random(1)-0.5)':s=44100:d=30"
"$ff" -hide_banner -loglevel error -y -f lavfi -i "$src" -c:a pcm_s16le src_44k_s16.wav

gen() { "$ff" -hide_banner -loglevel error -y -i src_44k_s16.wav "$@"; }
video="testsrc2=size=320x240:rate=25:duration=30"
vgen() { local o=$1; shift; "$ff" -hide_banner -loglevel error -y -f lavfi -i "$video" -i src_44k_s16.wav -shortest "$@" "$o"; }

# audio containers the chooser accepts, plus unusual PCM layouts
gen -c:a pcm_s24le -ar 48000 audio_48k_24bit.wav
gen -c:a pcm_f32le -ar 96000 -ac 1 audio_96k_f32_mono.wav
gen -c:a pcm_u8 -ar 22050 audio_22k_u8.wav
gen -c:a flac audio.flac
gen -c:a pcm_s16be audio.aif
gen -c:a pcm_s16be audio.aiff
gen -c:a libmp3lame -b:a 128k audio.mp3
gen -c:a libvorbis -q:a 4 audio.ogg
gen -c:a libopus audio_opus.ogg
gen -c:a aac -b:a 128k audio.m4a
gen -c:a aac -ac 1 -ar 48000 audio_mono_48k.m4a

# video containers
vgen video.mp4  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k
vgen video.mov  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k
vgen video.mkv  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k
vgen video.m4v  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k
vgen video.avi  -c:v mpeg4 -q:v 5 -c:a libmp3lame -b:a 128k
vgen video.webm -c:v libvpx -b:v 500k -c:a libvorbis
vgen video.wmv  -c:v wmv2 -b:v 500k -c:a wmav2 -b:a 128k
vgen video.mpeg -c:v mpeg1video -b:v 1000k -c:a mp2 -b:a 192k
vgen video_51.mp4 -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -ac 6
"$ff" -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=1920x1080:rate=60:duration=30" \
    -i src_44k_s16.wav -shortest -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k video_1080p60.mp4

# edge cases
vgen video_no_audio.mp4 -an -c:v libx264 -preset veryfast -pix_fmt yuv420p
gen -t 0.5 audio_half_second.wav
gen -af "volume=0" audio_silent.wav
"$ff" -hide_banner -loglevel error -y -f lavfi -i "anullsrc=r=44100:cl=stereo" -t 0 -c:a pcm_s16le audio_empty.wav
printf 'not a media file' > bogus.wav
head -c 100000 audio.mp3 > audio_truncated.mp3
mkdir -p "中文 路徑 ünï"
cp src_44k_s16.wav "中文 路徑 ünï/測試 音檔 #1.wav"

ls -1 | sort
