

ffmpeg -codecs
ffmpeg -formats
ffmpeg -h encoder=h264_nvenc


ffplay -vf showinfo test.mp4
ffprobe -show_packets test.avi


mpv --vo=help
mpv --hwdec=help

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

