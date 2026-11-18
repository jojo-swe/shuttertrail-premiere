// SPDX-License-Identifier: MIT
// Platform adapter. All demuxing, decoding, encoding and muxing use Windows.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <numeric>
#include <utility>
#include <string>
#include "MotionBlur.hpp"
#include "Preview.hpp"
#include <optional>

using Microsoft::WRL::ComPtr;
using motion::Frame;
using i64 = long long;
constexpr DWORD video = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr i64 hns = 10000000, adobeTicks = 254016000000LL;
static void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char s[256];
        std::snprintf(s,sizeof(s),"%s (Windows HRESULT 0x%08lX)",operation,(unsigned long)hr);
        throw std::runtime_error(s);
    }
}
static void need(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static i64 integer(const wchar_t* s, i64 lo, i64 hi) {
    wchar_t* end=nullptr;
    const i64 v=std::wcstoll(s,&end,10);
    need(*s && end && !*end && v>=lo && v<=hi,"Invalid numeric argument");
    return v;
}
static ComPtr<IMFMediaType> mediaType(GUID subtype) {
    ComPtr<IMFMediaType> type;
    check(MFCreateMediaType(&type),"Create media type");
    check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Set major type");
    check(type->SetGUID(MF_MT_SUBTYPE,subtype),"Set subtype");
    return type;
}
struct VideoFormat {
    ComPtr<IMFMediaType> native;
    UINT32 width=0,height=0,num=0,den=0;
    i64 time(i64 frame) const {
        // Split first to avoid overflow in long clips at fractional frame rates.
        return (frame/num)*den*hns + ((frame%num)*den*hns + num/2)/num;
    }
    // Nearest frame for a 100-ns delta. Inverse of time() at this exact rate,
    // including 30000/1001, so a timestamp lands on the frame that produced it.
    i64 frameFor(i64 delta) const {
        const bool neg=delta<0;
        const unsigned long long magnitude=neg?static_cast<unsigned long long>(-delta)
                                              :static_cast<unsigned long long>(delta);
        const unsigned long long tick=static_cast<unsigned long long>(den)*static_cast<unsigned long long>(hns);
        const unsigned long long n=num;
        const unsigned long long frames=(magnitude/tick)*n+((magnitude%tick)*n+tick/2)/tick;
        if (neg && frames!=0) return -static_cast<i64>(frames);
        return static_cast<i64>(frames);
    }
};
struct Reader : VideoFormat {
    ComPtr<IMFSourceReader> source;
    Reader(const wchar_t* file, i64 ticksPerFrame) {
        check(MFCreateSourceReaderFromURL(file,nullptr,&source),"Open video");
        check(source->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Disable other streams");
        check(source->SetStreamSelection(video,TRUE),"Select video");
        check(source->GetNativeMediaType(video,0,&native),"Read source format");
        GUID sub;
        check(native->GetGUID(MF_MT_SUBTYPE,&sub),"Read codec");
        need(sub==MFVideoFormat_H264,"This version accepts H.264 source video");
        check(MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&num,&den),"Read source frame rate");
        // Some MP4 readers round the advertised average (e.g. 29.970).
        // Use the sequence's exact rational rate and validate decoded PTS below.
        need(num && den && std::abs(double(num)/den-double(adobeTicks)/ticksPerFrame)<0.001,
             "Source and sequence must have the same constant frame rate");
        i64 divisor=std::gcd(adobeTicks,ticksPerFrame);
        need(adobeTicks/divisor<=1000000 && ticksPerFrame/divisor<=100000,"Unsupported frame rate");
        num=UINT32(adobeTicks/divisor); den=UINT32(ticksPerFrame/divisor);
        UINT32 aspectX=1,aspectY=1;
        MFGetAttributeRatio(native.Get(),MF_MT_PIXEL_ASPECT_RATIO,&aspectX,&aspectY);
        need(aspectX==aspectY,"Anamorphic footage is unsupported");
        need(MFGetAttributeUINT32(native.Get(),MF_MT_VIDEO_ROTATION,0)==0,"Rotated footage is unsupported");
        auto prim=MFGetAttributeUINT32(native.Get(),MF_MT_VIDEO_PRIMARIES,0);
        auto transfer=MFGetAttributeUINT32(native.Get(),MF_MT_TRANSFER_FUNCTION,0);
        need(prim!=MFVideoPrimaries_BT2020 && transfer!=MFVideoTransFunc_2084 &&
             transfer!=MFVideoTransFunc_HLG,"HDR or wide-gamut footage is unsupported");
        auto interlace=MFGetAttributeUINT32(native.Get(),MF_MT_INTERLACE_MODE,0);
        need(interlace==0 || interlace==MFVideoInterlace_Progressive ||
             interlace==MFVideoInterlace_MixedInterlaceOrProgressive,"Interlaced footage is unsupported");
        auto input=mediaType(MFVideoFormat_NV12);
        check(source->SetCurrentMediaType(video,nullptr,input.Get()),"Select Windows NV12 decoder");
        ComPtr<IMFMediaType> decoded;
        check(source->GetCurrentMediaType(video,&decoded),"Read decoder format");
        check(MFGetAttributeSize(decoded.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read frame dimensions");
        need(width && height && width<=8192 && height<=8192 && !(width%2) && !(height%2),
             "Unsupported frame dimensions");
    }
    ComPtr<IMFSample> read(i64& timestamp) {
        auto sample=tryRead(timestamp);
        need(sample!=nullptr,"Source ended before the selected out point");
        return sample;
    }
    // Returns null at the end of the stream.
    ComPtr<IMFSample> tryRead(i64& timestamp) {
        for (;;) {
            DWORD flags=0; ComPtr<IMFSample> sample;
            check(source->ReadSample(video,0,nullptr,&flags,&timestamp,&sample),"Decode frame");
            need(!(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)),
                 "Decoder failed or the source format changed");
            if (flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                ComPtr<IMFMediaType> t; GUID sub; UINT32 w=0,h=0;
                check(source->GetCurrentMediaType(video,&t),"Refresh decoder format");
                check(t->GetGUID(MF_MT_SUBTYPE,&sub),"Read decoded pixel format");
                check(MFGetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,&w,&h),"Read decoded size");
                need(sub==MFVideoFormat_NV12 && w==width && h==height,"Decoded picture format changed");
            }
            if (sample) {
                need(!MFGetAttributeUINT32(sample.Get(),MFSampleExtension_Interlaced,0),"Interlaced sample");
                return sample;
            }
            if (flags&MF_SOURCE_READERF_ENDOFSTREAM) return nullptr;
        }
    }
    Frame unpack(IMFSample* sample) const {
        Frame f(width,height);
        ComPtr<IMFMediaBuffer> buffer;
        check(sample->ConvertToContiguousBuffer(&buffer),"Get decoded image buffer");
        const DWORD size=DWORD(f.pixels.size());
        ComPtr<IMF2DBuffer> image;
        if (SUCCEEDED(buffer.As(&image))) {
            DWORD length=0;
            check(image->GetContiguousLength(&length),"Measure image");
            need(length==size,"Unexpected decoded image layout");
            check(image->ContiguousCopyTo(f.pixels.data(),size),"Copy packed NV12 image");
        } else {
            BYTE* p=nullptr; DWORD length=0;
            check(buffer->Lock(&p,nullptr,&length),"Lock image");
            if (length==size) std::memcpy(f.pixels.data(),p,size);
            check(buffer->Unlock(),"Unlock image");
            need(length==size,"Unsupported padded video buffer");
        }
        return f;
    }
};

struct Writer {
    ComPtr<IMFSinkWriter> sink;
    DWORD stream=0;
    const VideoFormat& input;
    Writer(const wchar_t* file, const VideoFormat& reader) : input(reader) {
        check(MFCreateSinkWriterFromURL(file,nullptr,nullptr,&sink),"Create MP4 writer");
        auto compressed=mediaType(MFVideoFormat_H264), raw=mediaType(MFVideoFormat_NV12);
        for (auto t : {compressed.Get(),raw.Get()}) {
            check(MFSetAttributeSize(t,MF_MT_FRAME_SIZE,input.width,input.height),"Set output size");
            check(MFSetAttributeRatio(t,MF_MT_FRAME_RATE,input.num,input.den),"Set output fps");
            check(MFSetAttributeRatio(t,MF_MT_PIXEL_ASPECT_RATIO,1,1),"Set output pixel aspect");
            check(t->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"Set progressive scan");
            for (auto key : {MF_MT_VIDEO_PRIMARIES,MF_MT_TRANSFER_FUNCTION,MF_MT_YUV_MATRIX,MF_MT_VIDEO_NOMINAL_RANGE}) {
                UINT32 value=0;
                if (SUCCEEDED(input.native->GetUINT32(key,&value))) check(t->SetUINT32(key,value),"Copy colour metadata");
            }
        }
        double rate=double(input.width)*input.height*input.num/input.den*0.35;
        check(compressed->SetUINT32(MF_MT_AVG_BITRATE,UINT32(std::clamp(rate,2000000.,100000000.))),"Set bitrate");
        check(compressed->SetUINT32(MF_MT_MPEG2_PROFILE,100),"Set H.264 High profile");
        check(sink->AddStream(compressed.Get(),&stream),"Add H.264 encoder");
        check(sink->SetInputMediaType(stream,raw.Get(),nullptr),"Configure encoder");
        check(sink->BeginWriting(),"Start encoder");
    }
    void write(const Frame& f,i64 index,i64 origin=0) {
        const DWORD bytes=DWORD(f.pixels.size());
        ComPtr<IMFMediaBuffer> buf; ComPtr<IMFSample> sample;
        check(MFCreateMemoryBuffer(bytes,&buf),"Create encoder buffer");
        BYTE* p=nullptr;
        check(buf->Lock(&p,nullptr,nullptr),"Lock encoder buffer");
        std::memcpy(p,f.pixels.data(),bytes);
        check(buf->Unlock(),"Unlock encoder buffer");
        check(buf->SetCurrentLength(bytes),"Set buffer length");
        check(MFCreateSample(&sample),"Create sample");
        check(sample->AddBuffer(buf.Get()),"Attach encoder buffer");
        check(sample->SetSampleTime(origin+input.time(index)),"Set sample timestamp");
        check(sample->SetSampleDuration(input.time(index+1)-input.time(index)),"Set sample duration");
        check(sink->WriteSample(stream,sample.Get()),"Encode video frame");
    }
};
// Decodes source frames start..start+count-1, plus a handle frame on each side
// when the source has one, and hands each frame and its blurred version to
// sink(reader, original, blurred, index). Full renders and previews share it.
template <class Sink>
static void process(const wchar_t* file, i64 start, i64 count, i64 ticksPerFrame,
                    int angle, int samples, Sink&& sink) {
    Reader reader(file,ticksPerFrame);
    i64 pts=0;
    auto first=reader.read(pts);
    // The first presentation is frame 0 even when its timestamp is not zero
    // (edit lists and encoder delay). Later frames are measured from it.
    const i64 origin=pts;
    auto frameAt=[&](i64 timestamp){return reader.frameFor(timestamp-origin);};
    // One frame on each side of the trim, when the source has it, gives the
    // first and last frames the same blur as the rest instead of half of it.
    const bool before=start>0;
    const i64 begin=before?start-1:start;
    if (begin>0) {
        // Seek slightly before the selection. Decoders may return a keyframe
        // earlier than requested; choose the frame by rounding, not a tolerance.
        const i64 target=origin+reader.time(begin);
        i64 seekPos=target-hns;
        if (seekPos<origin) seekPos=origin;
        auto seekTo=[&](i64 position){
            PROPVARIANT pos={};
            pos.vt=VT_I8; pos.hVal.QuadPart=position;
            check(reader.source->SetCurrentPosition(GUID_NULL,pos),"Seek to trim");
            first=reader.read(pts);
        };
        seekTo(seekPos);
        if (frameAt(pts)>begin) seekTo(origin<0?origin:0);
        while (frameAt(pts)<begin) first=reader.read(pts);
    }
    auto validate=[&](i64 expected){
        const i64 got=frameAt(pts);
        if (got==expected) return;
        char s[256];
        std::snprintf(s,sizeof(s),
            "Frame timestamp mismatch at frame %lld (got %lld, pts %lld, origin %lld). Use constant-frame-rate media without gaps",
            expected,got,pts,origin);
        need(false,s);
    };
    validate(begin);
    Frame previous=reader.unpack(first.Get());
    if (before) {
        first=reader.read(pts);
        validate(start);
    }
    Frame current=before?reader.unpack(first.Get()):previous;
    first.Reset();
    motion::Exposer exposer(samples,angle);
    for (i64 i=0; i<count; ++i) {
        Frame next(0,0);
        bool hasNext=false;
        if (i+1<count) {
            auto s=reader.read(pts); validate(start+i+1); next=reader.unpack(s.Get()); hasNext=true;
        } else if (auto s=reader.tryRead(pts); s && frameAt(pts)==start+count) {
            next=reader.unpack(s.Get()); hasNext=true;
        }
        // Without a handle, pass the current frame itself to clamp at that edge.
        const Frame& p=(i>0 || before)?previous:current;
        const Frame& n=hasNext?next:current;
        sink(static_cast<const Reader&>(reader),current,exposer(p,current,n),i);
        previous=std::move(current); current=std::move(next);
    }
}

static void render(wchar_t** args) {
    // The panel reserves a private directory per job. Also refuse to
    // overwrite a file if someone invokes this executable directly.
    need(GetFileAttributesW(args[2])==INVALID_FILE_ATTRIBUTES,"Output already exists");
    const i64 start=integer(args[3],0,100000000), count=integer(args[4],1,100000000);
    const i64 ticksPerFrame=integer(args[5],1000000,adobeTicks);
    const int angle=int(integer(args[6],0,360)), samples=int(integer(args[7],1,32));
    std::optional<Writer> writer;
    process(args[1],start,count,ticksPerFrame,angle,samples,
            [&](const Reader& reader, const Frame&, const Frame& blurred, i64 i) {
        if (!writer) writer.emplace(args[2],reader);
        writer->write(blurred,i);
        if (i+1==count) check(writer->sink->Finalize(),"Finalize MP4");
        if (i%5==0 || i+1==count) { std::printf("%lld/%lld\n",i+1,count); std::fflush(stdout); }
    });
}

// The colour standard the decoder reports, or the usual one for the size.
static preview::Colour colourOf(const Reader& reader) {
    preview::Colour colour;
    const auto matrix=MFGetAttributeUINT32(reader.native.Get(),MF_MT_YUV_MATRIX,MFVideoTransferMatrix_Unknown);
    colour.bt709=matrix==MFVideoTransferMatrix_BT709 || (matrix!=MFVideoTransferMatrix_BT601 && reader.height>=720);
    colour.fullRange=MFGetAttributeUINT32(reader.native.Get(),MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_Unknown)==MFNominalRange_0_255;
    return colour;
}

// --preview input outputBase frame ticksPerFrame angle samples
// Writes outputBase.bmp (blurred) and outputBase-original.bmp for one frame.
static void renderPreview(wchar_t** args) {
    const std::wstring base(args[2]), blurredFile=base+L".bmp", originalFile=base+L"-original.bmp";
    for (const auto& f : {blurredFile,originalFile})
        need(GetFileAttributesW(f.c_str())==INVALID_FILE_ATTRIBUTES,"Output already exists");
    const i64 frame=integer(args[3],0,100000000), ticksPerFrame=integer(args[4],1000000,adobeTicks);
    const int angle=int(integer(args[5],0,360)), samples=int(integer(args[6],1,32));
    process(args[1],frame,1,ticksPerFrame,angle,samples,
            [&](const Reader& reader, const Frame& original, const Frame& blurred, i64) {
        const auto colour=colourOf(reader);
        need(preview::writeFile(blurredFile,preview::toBmp(blurred,colour)),"Could not write the preview image");
        need(preview::writeFile(originalFile,preview::toBmp(original,colour)),"Could not write the preview image");
    });
}

#ifndef MOTION_BLUR_TEST_DRIVER
int wmain(int argc,wchar_t** argv) {
    const bool previewMode=argc==8 && std::wcscmp(argv[1],L"--preview")==0;
    if (argc!=8) {
        std::fputs("Usage: MotionBlurWorker input output.mp4 startFrame frameCount ticksPerFrame angle samples\n"
                   "       MotionBlurWorker --preview input outputBase frame ticksPerFrame angle samples\n",stderr);
        return 2;
    }
    bool com=false,mf=false; int status=0;
    try {
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize COM"); com=true;
        check(MFStartup(MF_VERSION),"Start Media Foundation"); mf=true;
        if (previewMode) renderPreview(argv+1); else render(argv);
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); status=1; }
    if (mf) MFShutdown();
    if (com) CoUninitialize();
    return status;
}
#endif
