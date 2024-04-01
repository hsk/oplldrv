#include <SDL.h>
#include <stdio.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
static const char* cmd = "ffmpeg -r 60 -f rawvideo -pix_fmt bgr24 -s 320x224 -i - -threads 0 -preset fast -y -crf 21 -r 60 bin/rom.mkv";
static FILE* ffmpeg;
static int skip=0;
    int fd;
    char *map;
void rec_init(int flg){
  if(!flg) return;
  printf("%s\n",cmd);
 ffmpeg = popen(cmd, "w");
 skip=0;
}
void rec_renderer(SDL_Renderer *renderer) {
    SDL_Rect rect={0,0,320,224};
    char* buf[320*224*3];
    int pitch=320*3;
    int rc=SDL_RenderReadPixels(renderer, &rect, SDL_PIXELFORMAT_BGR24, buf, pitch);
    if(rc==0){
      fwrite(buf, 3*320*224, 1, ffmpeg);
      fflush(ffmpeg);
    }
}
void rec(SDL_Renderer *renderer,SDL_Texture *tex) {
  if(ffmpeg) {
    skip++;
    if(skip<2)return;
    skip=0;
    rec_renderer(renderer);
  }
}

void rec_quit() {
  if(ffmpeg) {
    fflush(ffmpeg);
    pclose(ffmpeg);
  }
}
