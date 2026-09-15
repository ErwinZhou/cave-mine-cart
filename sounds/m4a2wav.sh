for f in player/*.m4a; do
  ffmpeg -nostdin -n -i "$f" \
    -map 0:a:0 -ar 48000 -ac 1 -c:a pcm_f32le \
    "${f%.m4a}.wav"
done
