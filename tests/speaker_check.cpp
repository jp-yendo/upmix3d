#include "upmix3d/windows_audio.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

int main(int argc, char** argv) {
    using namespace upmix3d;
    bool play = false, list = false, back_center = false;
    std::string device, layout = "7.1.4";
    for (int i=1; i<argc; ++i) {
        const std::string arg=argv[i];
        if (arg=="--play") play=true;
        else if (arg=="--list") list=true;
        else if (arg=="--back-center") back_center=true;
        else if ((arg=="--device" || arg=="--layout") && i+1<argc) { const auto value=argv[++i]; if(arg=="--device")device=value; else layout=value; }
        else { std::cerr << "Unknown speaker-check argument\n"; return 1; }
    }
    if (FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))) return 1;
    int result=0;
    try {
        if (list) for (const auto& entry:enumerate_devices()) std::cout << entry.name << '\n' << entry.id << '\n';
        if (play) {
            const Mask requested=layout_mask(layout) | (back_center?bit(Channel::BC):0);
            AudioSink sink([](const auto& message){std::cout<<message<<'\n';});
            HRESULT hr=sink.open(device,requested,Route::Spatial,false);
            if(FAILED(hr)||sink.route()!=Route::Spatial)throw std::runtime_error("Spatial output is required for the position check");
            constexpr std::array names{"FrontLeft","FrontRight","Center","LFE","SideLeft","SideRight","BackLeft","BackRight","BackCenter","TopFrontLeft","TopFrontRight","TopBackLeft","TopBackRight"};
            const auto order=channels(requested);
            const auto per_channel=sink.rate();
            const std::uint64_t total=static_cast<std::uint64_t>(per_channel)*order.size();
            std::uint64_t position=0;
            std::mt19937 random(24); std::uniform_real_distribution<float> noise(-1,1);
            std::array<float,3> pink{};
            hr=sink.start(); if(FAILED(hr))throw std::runtime_error("Cannot start the spatial position check");
            unsigned announced=999;
            while(position<total) {
                if(WaitForSingleObject(sink.event(),100)!=WAIT_OBJECT_0)throw std::runtime_error("Spatial render event is unavailable");
                UINT32 frames=0; hr=sink.begin(frames); if(FAILED(hr))throw std::runtime_error("Cannot acquire a spatial test buffer");
                sink.volume(1);
                for(UINT32 i=0;i<frames;++i) {
                    Frame output{};
                    if(position<total) {
                        const auto slot=static_cast<unsigned>(position/per_channel);
                        if(slot!=announced){std::cout<<names[static_cast<unsigned>(order[slot])]<<'\n';announced=slot;}
                        const float white=noise(random);
                        pink[0]=.99765f*pink[0]+.099046f*white;
                        pink[1]=.963f*pink[1]+.296516f*white;
                        pink[2]=.57f*pink[2]+1.052691f*white;
                        float sample=(pink[0]+pink[1]+pink[2]+.1848f*white)*.05f;
                        const auto offset=position%per_channel;
                        if(order[slot]==Channel::LFE)sample=.2f*std::sin(2*std::numbers::pi_v<float>*60*static_cast<float>(offset)/per_channel);
                        const float fade=std::min(1.f,std::min(static_cast<float>(offset),static_cast<float>(per_channel-1-offset))/(static_cast<float>(per_channel)*.05f));
                        output[static_cast<unsigned>(order[slot])]=sample*db_gain(-30)*fade;
                        ++position;
                    }
                    sink.write(i,output);
                }
                hr=sink.end(frames); if(FAILED(hr))throw std::runtime_error("Cannot submit the spatial test buffer");
            }
            while(sink.pending_seconds()>.001)Sleep(1);
            sink.stop();
        } else if(!list) std::cout<<"Use --list to enumerate devices, or --play for a spatial channel check with -30 dB gain.\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    CoUninitialize();return result;
}
