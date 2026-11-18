// visionOS: the game's main() (mac/source/main.cpp, built as or2_game_main)
// on a thread of its own, the SwiftUI application keeping the main thread;
// and mac/source/display_rate.hpp (Apple Vision Pro shows 90 Hz; the game
// presents its 60 Hz ticks, FRAME RATE offers no other rate here).
#include "vision_bridge.h"
#include "display_rate.hpp"
#import <Foundation/Foundation.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>

int or2_game_main(int argc,char** argv);

namespace {
void* run_game(void*){
    static char name[]="OutRun";
    char* argv[]{name,nullptr};
    const int code=or2_game_main(1,argv);
    std::fprintf(stderr,"[visionos] game ended (%d)\n",code);std::fflush(stderr);
    dispatch_async(dispatch_get_main_queue(),^{std::exit(code);});
    return nullptr;
}
}
extern "C" void or2_vision_start(void){
    static bool started=false;
    if(started)return;
    started=true;
    // The translated game code keeps large frames on the stack: 16 MB, as a desktop main thread.
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,16u<<20);
    pthread_t thread;
    if(pthread_create(&thread,&attr,run_game,nullptr)!=0){std::fprintf(stderr,"[visionos] cannot start the game thread\n");std::exit(1);}
    pthread_attr_destroy(&attr);pthread_detach(thread);
}

namespace outrun::mac_runtime {
unsigned screen_max_frames_per_second(){return 90;}
void screen_request_frames_per_second(unsigned){}   // paced by the window's RealityKit updates
}
