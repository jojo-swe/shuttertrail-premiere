// SPDX-License-Identifier: MIT
// Exercise the same reader and writer as the panel, using footage generated
// in this process. The test executable takes an empty temporary folder.
// Clips are synthetic: a moving-bar fixture and a frame-index clip.
// Media Foundation reads both back with a nonzero first PTS. No external media is read.
#define MOTION_BLUR_TEST_DRIVER
#include "../worker/RenderClip.cpp"
#include <filesystem>

constexpr int indexBits=6, indexBlock=16;

void paintIndex(Frame& frame, int index) {
    std::fill(frame.pixels.begin(), frame.pixels.end(), motion::Byte(128));
    for (int y=0; y<frame.height; ++y) for (int x=0; x<frame.width; ++x)
        frame.pixels[y*frame.width+x]=motion::Byte(32);
    for (int bit=0; bit<indexBits; ++bit) if ((index>>bit)&1) {
        const int x0=bit*indexBlock;
        for (int y=0; y<indexBlock; ++y) for (int x=x0; x<x0+indexBlock; ++x)
            frame.pixels[y*frame.width+x]=motion::Byte(208);
    }
}
int readIndex(const Frame& frame, int outputFrame) {
    int index=0;
    for (int bit=0; bit<indexBits; ++bit) {
        int ones=0;
        for (int y=4; y<12; ++y) for (int x=bit*indexBlock+4; x<bit*indexBlock+12; ++x)
            if (frame.pixels[y*frame.width+x]>=128) ++ones;
        if (ones!=0 && ones!=64) {
            char message[96];
            std::snprintf(message,sizeof(message),
                "Frame index block %d was ambiguous on output frame %d (%d/64)",
                bit,outputFrame,ones);
            need(false,message);
        }
        if (ones) index|=1<<bit;
    }
    return index;
}
void checkRounding() {
    VideoFormat rate;
    rate.num=30000; rate.den=1001;
    for (i64 n=0; n<=100000; ++n)
        need(rate.frameFor(rate.time(n))==n,"Frame rounding is not exact at 30000/1001");
    const i64 half=i64(rate.den)*hns/(2*i64(rate.num));
    const i64 frames[]={0,1,2,36,37,38,45,59,1000,30000};
    for (i64 n : frames) {
        const i64 stamp=rate.time(n);
        for (i64 delta=n==0?0:-(half-2); delta<half-1; ++delta)
            need(rate.frameFor(stamp+delta)==n,"Rounding window is not frame-exact");
    }
    // Two seconds plus a fraction must not be treated as frame 0, and subtracting
    // that origin must restore the frame the writer stamped.
    const i64 origin=2*hns+50;
    need(rate.frameFor(origin)!=0,"A multi-second timestamp must not round to frame 0");
    const i64 relative[]={0,1,36,37,45,59};
    for (i64 n : relative)
        need(rate.frameFor(origin+rate.time(n)-origin)==n,"Origin-relative rounding drifted");
}
void writeFrames(const wchar_t* path, const VideoFormat& format, int count, i64 origin, bool indexed) {
    Writer writer(path,format);
    for (int n=0; n<count; ++n) {
        Frame frame(format.width,format.height);
        if (indexed) paintIndex(frame,n);
        else {
            std::fill(frame.pixels.begin(),frame.pixels.end(),motion::Byte(128));
            for (int y=0; y<frame.height; ++y) for (int x=0; x<frame.width; ++x)
                frame.pixels[y*frame.width+x]=motion::Byte(30+n*2+(x>=n && x<n+18?60:0));
        }
        writer.write(frame,n,origin);
    }
    check(writer.sink->Finalize(),"Finalize test fixture");
}
i64 probeTimestamps(const wchar_t* path, i64 ticks, const char* label) {
    Reader probe(path,ticks);
    i64 first=0;
    std::printf("%s presentation timestamps:",label);
    for (int n=0; n<3; ++n) {
        i64 pts=0;
        probe.read(pts);
        if (n==0) first=pts;
        else if (probe.frameFor(pts-first)!=n) {
            char message[160];
            std::snprintf(message,sizeof(message),
                "%s frame %d is not one frame after the first PTS %lld (pts %lld)",
                label,n,first,pts);
            need(false,message);
        }
        std::printf(" %lld",pts);
    }
    std::printf("\n");
    std::fflush(stdout);
    if (std::llabs(first)<=10000) {
        char message[128];
        std::snprintf(message,sizeof(message),
            "%s did not expose a nonzero start timestamp (first PTS %lld)",label,first);
        need(false,message);
    }
    return first;
}
void checkOutput(const wchar_t* path, i64 ticks, int startFrame, int count, bool indexed) {
    Reader result(path,ticks);
    i64 origin=0, previous=0;
    for (int n=0; n<count; ++n) {
        i64 pts=0;
        auto sample=result.read(pts);
        if (n==0) {
            origin=pts;
            if (std::llabs(origin)>result.time(30)) {
                char message[128];
                std::snprintf(message,sizeof(message),
                    "Output timeline has an unexpected start offset %lld",origin);
                need(false,message);
            }
        } else need(pts>previous,"Output timestamps must increase");
        previous=pts;
        const i64 index=result.frameFor(pts-origin);
        if (index!=n) {
            char message[160];
            std::snprintf(message,sizeof(message),
                "Unexpected output PTS %lld for frame %d (origin %lld, index %lld)",
                pts,n,origin,index);
            need(false,message);
        }
        Frame frame=result.unpack(sample.Get());
        if (indexed) {
            const int got=readIndex(frame,n);
            if (got!=startFrame+n) {
                char message[128];
                std::snprintf(message,sizeof(message),
                    "Incorrect source frame. Output %d decoded as %d, expected %d",
                    n,got,startFrame+n);
                need(false,message);
            }
        } else {
            const int expect=30+2*(startFrame+n), got=frame.pixels[frame.width-1];
            if (std::abs(got-expect)>5) {
                char message[128];
                std::snprintf(message,sizeof(message),
                    "Incorrect source trim. Pixel %d expected %d",got,expect);
                need(false,message);
            }
        }
    }
    for (;;) {
        DWORD flags=0; ComPtr<IMFSample> extra;
        check(result.source->ReadSample(video,0,nullptr,&flags,nullptr,&extra),"Check end of trim");
        need(!extra,"Too many output frames");
        if (flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
        need(!(flags&MF_SOURCE_READERF_ERROR),"Decoder error at end of trim");
    }
}
void refuseOverwrite(wchar_t** args, const char* message) {
    bool refused=false;
    try { render(args); }
    catch (const std::exception& e) {
        refused=std::strstr(e.what(),"Output already exists")!=nullptr;
        if (!refused) throw;
    }
    need(refused,message);
}

int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    bool com=false, mf=false; int status=0;
    try {
        checkRounding();
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM"); com=true;
        check(MFStartup(MF_VERSION),"Media Foundation"); mf=true;
        const auto dir=std::filesystem::path(argv[1]);
        VideoFormat format;
        format.width=160; format.height=96; format.num=30000; format.den=1001;
        format.native=mediaType(MFVideoFormat_NV12);
        const i64 ticks=adobeTicks*1001/30000;
        const auto ticksText=std::to_wstring(ticks);
        constexpr int startFrame=37, count=9, frames=60;
        wchar_t trim[]=L"37", frameCount[]=L"9", angle180[]=L"180", angle0[]=L"0", samples[]=L"8";
        auto run=[&](const std::wstring& source, const std::wstring& output, wchar_t* angle) {
            wchar_t* args[]={nullptr,const_cast<wchar_t*>(source.c_str()),const_cast<wchar_t*>(output.c_str()),
                             trim,frameCount,const_cast<wchar_t*>(ticksText.c_str()),angle,samples};
            render(args);
        };

        const auto source=(dir/L"source.mp4").wstring(), trimOut=(dir/L"trim.mp4").wstring();
        writeFrames(source.c_str(),format,frames,0,false);
        const i64 sourcePts=probeTimestamps(source.c_str(),ticks,"source.mp4");
        std::printf("source.mp4 first presentation timestamp: %lld\n",sourcePts);
        std::fflush(stdout);
        {
            wchar_t* args[]={nullptr,const_cast<wchar_t*>(source.c_str()),const_cast<wchar_t*>(source.c_str()),
                             trim,frameCount,const_cast<wchar_t*>(ticksText.c_str()),angle180,samples};
            refuseOverwrite(args,"Renderer must refuse to overwrite the original clip");
        }
        run(source,trimOut,angle180);
        checkOutput(trimOut.c_str(),ticks,startFrame,count,false);
        {
            wchar_t* args[]={nullptr,const_cast<wchar_t*>(source.c_str()),const_cast<wchar_t*>(trimOut.c_str()),
                             trim,frameCount,const_cast<wchar_t*>(ticksText.c_str()),angle180,samples};
            refuseOverwrite(args,"Renderer must refuse to overwrite an existing render");
        }

        const auto indexed=(dir/L"index-source.mp4").wstring(), indexOut=(dir/L"index-trim.mp4").wstring();
        writeFrames(indexed.c_str(),format,frames,0,true);
        probeTimestamps(indexed.c_str(),ticks,"index-source.mp4");
        run(indexed,indexOut,angle0);
        checkOutput(indexOut.c_str(),ticks,startFrame,count,true);

        std::puts("Media Foundation round-trip passed");
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); status=1; }
    if (mf) MFShutdown();
    if (com) CoUninitialize();
    return status;
}
