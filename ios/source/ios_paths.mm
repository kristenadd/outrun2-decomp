// iOS implementation of mac/source/mac_paths.hpp. The game folder is the
// application's Documents folder (visible in the Files app and in the Finder's
// device window), or one folder inside it; saves go to Application Support.
#include "mac_paths.hpp"
#include "ios_screens.hpp"
#include "platform/retail_asset_store.hpp"
#import <Foundation/Foundation.h>
#include <SDL.h>
#include <string>
#include <vector>
namespace outrun::mac {
namespace {
std::string documents_folder(){
    @autoreleasepool {
        NSURL* url=[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        return url?url.path.UTF8String:"";
    }
}
// Documents itself, else its first sub-folder that holds the game (the player
// may copy the installation folder as a whole).
std::string find_game_folder(){
    const std::string documents=documents_folder();
    if(documents.empty())return "";
    if(platform::retail_asset_store_is_game_root(documents))return documents;
    @autoreleasepool {
        NSArray<NSString*>* names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(documents.c_str()) error:nil];
        for(NSString* name in [names sortedArrayUsingSelector:@selector(compare:)]){
            const std::string candidate=documents+"/"+name.UTF8String;
            if(platform::retail_asset_store_is_game_root(candidate))return candidate;
        }
    }
    return "";
}
// The Files app shows the application's folder only once it holds something.
void leave_instructions(const std::string& documents){
    @autoreleasepool {
        NSString* path=[@(documents.c_str()) stringByAppendingPathComponent:@"Copy your OutRun 2006 game files here.txt"];
        if([[NSFileManager defaultManager] fileExistsAtPath:path])return;
        NSString* text=@"Copy the contents of your OutRun 2006 Coast 2 Coast installation folder (Steam), "
            "including OR2006C2C.EXE, into this folder, or copy the installation folder itself into it. "
            "Then open OutRun again.\n";
        [text writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
}
}
std::string application_folder(){
    const std::string found=find_game_folder();
    return found.empty()?documents_folder():found;
}
// The game is never inside the iOS application bundle.
bool inside_application_bundle(const std::string&){return false;}
bool choose_retail_directory(std::string& path){
    const std::string documents=documents_folder();
    if(documents.empty())return false;
    leave_instructions(documents);
    // Waits on an instruction screen until the files are in place.
    return ios::wait_for_game_files([&path]{path=find_game_folder();return !path.empty();});
}
bool user_paths(Paths& paths,std::string& error){
    @autoreleasepool {
        NSFileManager* manager=[NSFileManager defaultManager];NSError* e=nil;
        NSURL* support=[manager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&e];
        if(!support){error=e.localizedDescription.UTF8String;return false;}
        support=[support URLByAppendingPathComponent:@"OutRun/Saves" isDirectory:YES];
        if(![manager createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:&e]){error=e.localizedDescription.UTF8String;return false;}
        // Saves are the player's data: keep them in device backups, but out of the Files app.
        paths.saves=support.path.UTF8String;return true;
    }
}
}
