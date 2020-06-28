ref: https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu


git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
cd nv-codec-headers
make
sudo make install


PATH="/media/data/ffmpeg/bin:/usr/local/cuda/bin:$PATH" \
PKG_CONFIG_PATH="/media/data/ffmpeg/ffmpeg_build/lib/pkgconfig" ./configure \
  --prefix="/media/data/ffmpeg/ffmpeg_build" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I/media/data/ffmpeg/ffmpeg_build/include" \
  --extra-ldflags="-L/media/data/ffmpeg/ffmpeg_build/lib" \
  --extra-libs="-lpthread -lm" \
  --bindir="/media/data/ffmpeg/bin" \
  --enable-gpl \
  --enable-libass \
  --enable-libfdk-aac \
  --enable-libfreetype \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-libvorbis \
  --enable-libvpx \
  --enable-libx264 \
  --enable-libx265 \
  --enable-nonfree \
  --enable-nvenc --enable-cuda --enable-cuvid --enable-cuda-nvcc

PATH="/media/data/ffmpeg/bin:/usr/local/cuda/bin:$PATH" make -j8

make install && \
hash -r


# Not used because of some install issues
  --enable-gnutls \
  --enable-libaom \