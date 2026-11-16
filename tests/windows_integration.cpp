#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <vlc/vlc.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()};
}
LONG CALLBACK native_fault(EXCEPTION_POINTERS* exception) {
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    std::cerr << "Native access violation at " << exception->ExceptionRecord->ExceptionAddress << '\n';
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, "build/Release", TRUE);
    void* stack[40]{}; const auto count = CaptureStackBackTrace(0,40,stack,nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+512]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage); symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (SymFromAddr(process,address,&displacement,symbol)) std::cerr << symbol->Name << " + " << displacement << '\n';
        else {
            HMODULE module = nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(stack[i]),&module);
            wchar_t name[MAX_PATH]{}; GetModuleFileNameW(module,name,MAX_PATH);
            std::cerr << utf8(name) << " + 0x" << std::hex << address-reinterpret_cast<DWORD64>(module) << std::dec << '\n';
        }
    }
    std::cerr.flush(); TerminateProcess(process,3); return EXCEPTION_CONTINUE_SEARCH;
}
template<class T> T symbol(HMODULE module, const char* name) {
    const auto value = GetProcAddress(module,name);
    if (!value) throw std::runtime_error(std::string("Missing libVLC symbol ")+name);
    return reinterpret_cast<T>(value);
}
struct Capture {
    std::mutex mutex;
    std::vector<std::string> lines;
    decltype(&libvlc_log_get_context) context = nullptr;
    static void callback(void* data, int, const libvlc_log_t* log, const char* format, va_list arguments) {
        auto& capture = *static_cast<Capture*>(data);
        char message[4096]{}; vsnprintf(message,sizeof(message),format,arguments);
        const char* module = nullptr;
        capture.context(log,&module,nullptr,nullptr);
        const std::string line = std::string(module ? module : "vlc") + ": " + message;
        { std::lock_guard lock(capture.mutex); capture.lines.push_back(line); }
        if (module && std::string_view(module)=="upmix3d") { std::cout << line << '\n'; std::cout.flush(); }
    }
};
void word(std::ofstream& file, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (8*i)));
}
void write_silence(const std::filesystem::path& path, unsigned channels, double seconds) {
    constexpr std::uint32_t masks[]{0,4,3,7,0x33,0x37,0x3f,0x70f,0x63f,0x73f};
    const auto total_size = static_cast<std::uint64_t>(48000*seconds)*2*channels;
    if (total_size > 0xffffffffULL-60) throw std::runtime_error("Use a compressed input file for long multichannel fixtures");
    const auto data_size = static_cast<std::uint32_t>(total_size);
    std::ofstream file(path,std::ios::binary);
    file.write("RIFF",4); word(file,60+data_size,4); file.write("WAVEfmt ",8); word(file,40,4);
    word(file,0xfffe,2); word(file,channels,2); word(file,48000,4); word(file,48000*2*channels,4);
    word(file,2*channels,2); word(file,16,2); word(file,22,2); word(file,16,2); word(file,masks[channels],4);
    constexpr unsigned char guid[]{1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
    file.write(reinterpret_cast<const char*>(guid),16); file.write("data",4); word(file,data_size,4);
    std::vector<char> zero(65536);
    for (std::uint32_t remaining=data_size; remaining;) { const auto n=std::min<std::uint32_t>(remaining,65536); file.write(zero.data(),n); remaining-=n; }
    if (!file) throw std::runtime_error("Cannot write the silent WAV fixture");
}
}
int main(int argc,char** argv) {
    AddVectoredExceptionHandler(1,native_fault);
    try {
        std::filesystem::path root="external/vlc-3.0.24", plugin="build/Release/libupmix3d_plugin.dll", input;
        std::string route="spatial", method="frequency", layout="7.1.4";
        unsigned channels=2, cycles=0; double seconds=3; bool controls=true, top_middle=false, select_device=false, edit_preset=false, read_config=false, stop_paused=false;
        std::string case_name;
        std::vector<std::string> extra, expected;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto value=[&]() -> std::string { if (++i==argc) throw std::runtime_error("Missing argument value"); return argv[i]; };
            if (arg=="--vlc-root") root=value(); else if(arg=="--plugin") plugin=value(); else if(arg=="--input") input=value();
            else if(arg=="--route") route=value(); else if(arg=="--method") method=value(); else if(arg=="--layout") layout=value();
            else if(arg=="--channels") channels=static_cast<unsigned>(std::stoul(value())); else if(arg=="--seconds") seconds=std::stod(value());
            else if(arg=="--option") extra.push_back(value()); else if(arg=="--expect") expected.push_back(value());
            else if(arg=="--no-controls") controls=false; else if(arg=="--top-middle") top_middle=true; else if(arg=="--select-device") select_device=true;
            else if(arg=="--edit-preset") edit_preset=true; else if(arg=="--read-config") read_config=true; else if(arg=="--case") case_name=value();
            else if(arg=="--stop-paused")stop_paused=true; else if(arg=="--cycles")cycles=static_cast<unsigned>(std::stoul(value()));
            else throw std::runtime_error("Unknown integration argument: "+arg);
        }
        if (channels<1 || channels>9 || seconds<=0 || seconds>7200) throw std::runtime_error("Invalid fixture duration or channels");
        const auto work=std::filesystem::absolute("build/integration"); std::filesystem::create_directories(work);
        root=std::filesystem::absolute(root);
        std::filesystem::copy_file(plugin,root/"plugins/audio_output/libupmix3d_plugin.dll",std::filesystem::copy_options::overwrite_existing);
        if(input.empty()) { input=work/("silence-"+std::to_string(channels)+".wav"); write_silence(input,channels,seconds); }
        input=std::filesystem::absolute(input);
        const HMODULE module=LoadLibraryExW((root/"libvlc.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) throw std::runtime_error("Cannot load project-local libVLC");
#define LOAD(name) auto name##_fn=symbol<decltype(&name)>(module,#name)
        LOAD(libvlc_new); LOAD(libvlc_release); LOAD(libvlc_media_new_path); LOAD(libvlc_media_release);
        LOAD(libvlc_media_player_new_from_media); LOAD(libvlc_media_player_release); LOAD(libvlc_media_player_play);
        LOAD(libvlc_media_player_set_pause); LOAD(libvlc_media_player_set_time); LOAD(libvlc_media_player_get_state);
        LOAD(libvlc_audio_set_mute); LOAD(libvlc_audio_set_volume); LOAD(libvlc_media_player_stop);
        LOAD(libvlc_log_set); LOAD(libvlc_log_get_context); LOAD(libvlc_audio_output_device_enum);
        LOAD(libvlc_audio_output_device_list_release); LOAD(libvlc_audio_output_device_set);
#undef LOAD
        std::mutex watchdog_mutex; std::condition_variable watchdog_condition; bool done=false;
        std::jthread watchdog([&] {
            std::unique_lock lock(watchdog_mutex);
            if(!watchdog_condition.wait_for(lock,std::chrono::duration<double>(seconds+20),[&]{return done;})) {
                std::cerr << "Integration watchdog expired\n"; std::cerr.flush(); TerminateProcess(GetCurrentProcess(),4);
            }
        });
        struct Complete { std::mutex& mutex; std::condition_variable& condition; bool& done;
            ~Complete(){std::lock_guard lock(mutex);done=true;condition.notify_all();} } complete{watchdog_mutex,watchdog_condition,done};
        std::vector<std::string> options{"--no-plugins-cache","--no-media-library","--no-video","--aout=upmix3d,none","--verbose=0",
            "--config="+utf8(work/"test.vlcrc"),"--upmix3d-layout="+layout,"--upmix3d-route="+route,"--upmix3d-method="+method};
        options.push_back(read_config ? "--no-ignore-config" : "--ignore-config");
        if(top_middle)options.push_back("--upmix3d-top-middle");
        options.insert(options.end(),extra.begin(),extra.end()); std::vector<const char*> pointers;
        for(const auto& option:options)pointers.push_back(option.c_str());
        auto* instance=libvlc_new_fn(static_cast<int>(pointers.size()),pointers.data()); if(!instance)throw std::runtime_error("Cannot initialize libVLC");
        Capture capture; capture.context=libvlc_log_get_context_fn; libvlc_log_set_fn(instance,Capture::callback,&capture);
        auto* media=libvlc_media_new_path_fn(instance,utf8(input).c_str());
        auto* player=libvlc_media_player_new_from_media_fn(media);
        if(!media||!player)throw std::runtime_error("Cannot create the integration media player");
        libvlc_audio_set_mute_fn(player,1);
        if(libvlc_media_player_play_fn(player))throw std::runtime_error("Cannot start integration playback");
        if(controls) {
            std::this_thread::sleep_for(500ms); libvlc_media_player_set_pause_fn(player,1); std::this_thread::sleep_for(150ms);
            if(libvlc_media_player_get_state_fn(player)!=libvlc_Paused)throw std::runtime_error("Pause state was not reached");
            if(stop_paused){libvlc_media_player_stop_fn(player);libvlc_media_player_play_fn(player);}
            else libvlc_media_player_set_pause_fn(player,0);
            libvlc_media_player_set_time_fn(player,1000);
            libvlc_audio_set_volume_fn(player,50); libvlc_audio_set_mute_fn(player,1);
        }
        for(unsigned i=0;i<cycles;++i) {
            libvlc_media_player_set_pause_fn(player,1); std::this_thread::sleep_for(30ms);
            libvlc_media_player_set_time_fn(player,static_cast<libvlc_time_t>(1000+i%5*20));
            libvlc_media_player_set_pause_fn(player,0); std::this_thread::sleep_for(20ms);
        }
        if(edit_preset) {
            std::this_thread::sleep_for(300ms);
            const auto core=GetModuleHandleW(L"libvlccore.dll");
            using PutFloat=void (__cdecl*)(void*,const char*,float);
            const auto put=symbol<PutFloat>(core,"config_PutFloat");
            put(nullptr,"upmix3d-center",30.f);
            libvlc_media_player_stop_fn(player);
            libvlc_media_player_play_fn(player);
        }
        if(select_device) {
            std::this_thread::sleep_for(200ms);
            auto* devices=libvlc_audio_output_device_enum_fn(player);
            if(!devices)throw std::runtime_error("No audio devices were enumerated");
            auto* selected=devices;
            while(selected->p_next && (!selected->psz_device || !*selected->psz_device))selected=selected->p_next;
            libvlc_audio_output_device_set_fn(player,nullptr,selected->psz_device);
            libvlc_audio_output_device_list_release_fn(devices);
        }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<double>(seconds+8);
        while(std::chrono::steady_clock::now()<deadline && libvlc_media_player_get_state_fn(player)!=libvlc_Ended && libvlc_media_player_get_state_fn(player)!=libvlc_Error)std::this_thread::sleep_for(20ms);
        const auto final_state=libvlc_media_player_get_state_fn(player);
        libvlc_media_player_stop_fn(player); libvlc_media_player_release_fn(player); libvlc_media_release_fn(media); libvlc_release_fn(instance);
        std::string all; {std::lock_guard lock(capture.mutex); for(const auto& line:capture.lines)all+=line+'\n';}
        const auto log=work/(case_name.empty()?route+"-"+method+"-"+layout+"-"+input.filename().string()+".log":case_name+".log"); std::ofstream(log)<<all;
        if(final_state!=libvlc_Ended)throw std::runtime_error("Playback did not finish; log="+utf8(log));
        if(all.find("input mask")==std::string::npos&&all.find("Bitstream passthrough")==std::string::npos)throw std::runtime_error("The requested output module did not start");
        if(all.find("Audio output failed")!=std::string::npos||all.find("worker exception")!=std::string::npos)throw std::runtime_error("The output worker reported a failure");
        for(const auto& text:expected)if(all.find(text)==std::string::npos)throw std::runtime_error("Expected evidence is missing: "+text);
        std::cout<<"PASS: native VLC playback and controls; log="<<utf8(log)<<'\n';
        return 0;
    } catch(const std::exception& error){std::cerr<<"Integration check: "<<error.what()<<'\n';return 1;}
}
