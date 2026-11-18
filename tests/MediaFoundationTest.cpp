// SPDX-License-Identifier: MIT
// Exercise the same reader and writer as the panel, using locally generated
// moving footage. The test executable takes an empty temporary folder.
#define MOTION_BLUR_TEST_DRIVER
#include "../worker/RenderClip.cpp"
#include <filesystem>
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    try {
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");
        check(MFStartup(MF_VERSION),"Media Foundation");
        {
            auto dir=std::filesystem::path(argv[1]);
            std::wstring source=(dir/L"source.mp4").wstring(), output=(dir/L"trim.mp4").wstring();
            VideoFormat format;
            format.width=160; format.height=96; format.num=30000; format.den=1001;
            format.native=mediaType(MFVideoFormat_NV12);
            {
                Writer w(source.c_str(),format);
                for (int n=0;n<60;++n) {
                    Frame f(format.width,format.height);
                    std::fill(f.pixels.begin(),f.pixels.end(),128);
                    for (int y=0;y<f.height;++y) for (int x=0;x<f.width;++x)
                        f.pixels[y*f.width+x]=motion::Byte(30+n*2+(x>=n && x<n+18?60:0));
                    w.write(f,n);
                }
                check(w.sink->Finalize(),"Finalize test fixture");
            }
            auto ticks=std::to_wstring(adobeTicks*1001/30000);
            wchar_t trim[]=L"37", count[]=L"9", angle[]=L"180", samples[]=L"8";
            wchar_t* args[]={nullptr,source.data(),output.data(),trim,count,ticks.data(),angle,samples};
            render(args);
            Reader result(output.c_str(),adobeTicks*1001/30000);
            for (int n=0;n<9;++n) {
                i64 pts=0;
                auto s=result.read(pts);
                need(std::llabs(pts-result.time(n))<=10000,"Unexpected output PTS");
                Frame f=result.unpack(s.Get());
                // The final column is free of the moving bar for this trim.
                need(std::abs(int(f.pixels[f.width-1])-(30+2*(37+n)))<=5,"Incorrect source trim");
            }
            for (;;) {
                DWORD flags=0; ComPtr<IMFSample> extra;
                check(result.source->ReadSample(video,0,nullptr,&flags,nullptr,&extra),"Check end of trim");
                need(!extra,"Too many output frames");
                if (flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
                need(!(flags&MF_SOURCE_READERF_ERROR),"Decoder error at end of trim");
            }
        }
        MFShutdown(); CoUninitialize();
        std::puts("Media Foundation round-trip passed"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
