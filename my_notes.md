
# software info

ffmpeg -codecs
ffmpeg -formats
ffmpeg -h encoder=h264_nvenc

mpv --vo=help
mpv --hwdec=help

# file info

ffplay -vf showinfo test.mkv
ffprobe -show_packets test.mkv

# select from multiple video streams
ffplay -vst v:1 test.mkv 
mpv --vid=1 test.mkv

# play file with multiple video streams

mpv with hwdec=no because some hwdec is not compatible with the stack filters.
        
        mpv --hwdec=no --lavfi-complex '[vid1][vid2]vstack[vo]' test.mkv 

combine into one new mkv file (ffplay don't support such filter).

        ffmpeg -i test.mkv -filter_complex "[0:0][0:1]vstack" output.mkv


# mpv config
create ~/.config/mpv/mpv.conf: 
    
        hwdec=cuda
        hwdec-codecs=all


ref:
https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples

http://dranger.com/ffmpeg/ffmpeg.html

https://github.com/feixiao/ffmpeg-tutorial

https://github.com/leixiaohua1020/simplest_ffmpeg_video_encoder  

TODO:

https://github.com/jocover/jetson-ffmpeg    

http://www.ipb.uni-bonn.de/data-software/depth-streaming-using-h-264/    

