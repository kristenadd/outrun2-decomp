#include "mac_paths.hpp"
#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#include "platform/retail_asset_store.hpp"
namespace outrun::mac {
namespace {
// <bundle>/Contents/Resources/Game itself, else its first sub-folder that holds
// the game (the player may copy the installation folder as a whole).
std::string bundled_game_folder(){
    @autoreleasepool {
        NSString* resources=[NSBundle mainBundle].resourcePath;
        if(!resources)return "";
        const std::string game=[resources stringByAppendingPathComponent:@"Game"].UTF8String;
        if(platform::retail_asset_store_is_game_root(game))return game;
        NSArray<NSString*>* names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(game.c_str()) error:nil];
        for(NSString* name in [names sortedArrayUsingSelector:@selector(compare:)]){
            const std::string candidate=game+"/"+name.UTF8String;
            if(platform::retail_asset_store_is_game_root(candidate))return candidate;
        }
        return "";
    }
}
}
bool inside_application_bundle(const std::string& path){
    @autoreleasepool {
        NSString* bundle=[NSBundle mainBundle].bundlePath;
        if(![bundle.pathExtension isEqualToString:@"app"]||path.empty())return false;
        NSString* a=[@(path.c_str()) stringByResolvingSymlinksInPath].stringByStandardizingPath;
        NSString* b=[bundle stringByResolvingSymlinksInPath].stringByStandardizingPath;
        return [a isEqualToString:b]||[a hasPrefix:[b stringByAppendingString:@"/"]];
    }
}
std::string application_folder(){
    const std::string bundled=bundled_game_folder();
    if(!bundled.empty())return bundled;
    @autoreleasepool {
        NSString* bundle=[NSBundle mainBundle].bundlePath;
        if([bundle.pathExtension isEqualToString:@"app"])return bundle.stringByDeletingLastPathComponent.UTF8String;
        return [NSBundle mainBundle].executablePath.stringByDeletingLastPathComponent.UTF8String;
    }
}
bool choose_retail_directory(std::string& path){
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        panel.canChooseFiles=NO;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=NO;
        panel.message=@"Choose your OutRun 2006 Coast 2 Coast folder (Steam), or put the game files in this application (Contents/Resources/Game).";
        if([panel runModal]!=NSModalResponseOK)return false;
        path=panel.URL.path.UTF8String;return true;
    }
}
bool user_paths(Paths& paths,std::string& error){
    @autoreleasepool {
        NSFileManager* manager=[NSFileManager defaultManager];NSError* e=nil;
        NSURL* support=[manager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&e];
        if(!support){error=e.localizedDescription.UTF8String;return false;}
        support=[support URLByAppendingPathComponent:@"OutRunMac/Saves" isDirectory:YES];
        if(![manager createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:&e]){error=e.localizedDescription.UTF8String;return false;}
        paths.saves=support.path.UTF8String;return true;
    }
}
}
