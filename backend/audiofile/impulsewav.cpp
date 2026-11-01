#include "impulsewav.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

CabinetImpulse loadImpulseWav(const std::string& path,double targetRate) {
    auto fail=[](const char* message){throw std::runtime_error(message);};
    if(!std::isfinite(targetRate)||targetRate<8000||targetRate>384000)fail("Unsupported audio sample rate.");
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)fail("Could not open cabinet IR WAV.");
    const auto size=file.tellg();
    if(size<44||size>32*1024*1024)fail("IR WAV is empty, truncated, or larger than 32 MB.");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),size))fail("Could not read cabinet IR WAV.");
    auto u16=[&](std::size_t p){return unsigned(bytes[p])|(unsigned(bytes[p+1])<<8);};
    auto u32=[&](std::size_t p){return std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|(std::uint32_t(bytes[p+2])<<16)|(std::uint32_t(bytes[p+3])<<24);};
    auto tag=[&](std::size_t p,const char* s){return std::equal(bytes.begin()+p,bytes.begin()+p+4,s);};
    if(!tag(0,"RIFF")||!tag(8,"WAVE"))fail("Cabinet IR must be a little-endian RIFF WAV.");
    const std::size_t end=std::size_t(u32(4))+8;
    if(end>bytes.size()||end<12)fail("Truncated WAV RIFF container.");
    unsigned format=0,channels=0,sourceRate=0,bits=0,align=0;
    std::size_t data=0,dataBytes=0;
    for(std::size_t p=12;p+8<=end;) {
        const std::size_t length=u32(p+4),start=p+8;
        if(length>end-start)fail("Truncated WAV chunk.");
        if(tag(p,"fmt ")) {
            if(length<16)fail("Invalid WAV format chunk.");
            format=u16(start);channels=u16(start+2);sourceRate=u32(start+4);align=u16(start+12);bits=u16(start+14);
            if(format==0xfffe) {
                if(length<40||u16(start+16)<22||u16(start+16)>length-18||u16(start+18)==0||u16(start+18)>bits)fail("Invalid extensible WAV format.");
                const unsigned char suffix[]{0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(!std::equal(std::begin(suffix),std::end(suffix),bytes.begin()+start+26))fail("Unsupported extensible WAV encoding.");
                format=u16(start+24);
            }
        } else if(tag(p,"data") && data==0){data=start;dataBytes=length;}
        p=start+length+(length&1);
    }
    if((channels!=1&&channels!=2)||sourceRate<8000||sourceRate>384000||!data||!dataBytes
        || !((format==1&&(bits==16||bits==24||bits==32))||(format==3&&(bits==32||bits==64)))
        || align!=channels*(bits/8)||dataBytes%align)fail("IR WAV must contain mono/stereo PCM16/24/32 or float32/64 audio.");
    const std::size_t frames=std::min(dataBytes/align,std::size_t(std::ceil(sourceRate*0.2))+2);
    std::array<std::vector<float>,2> decoded;
    double peak=0;
    for(unsigned c=0;c<channels;++c) {
        decoded[c].resize(frames);
        for(std::size_t i=0;i<frames;++i) {
            const auto p=data+i*align+c*(bits/8);double value=0;
            if(format==3) {
                if(bits==32)value=std::bit_cast<float>(u32(p));
                else value=std::bit_cast<double>(std::uint64_t(u32(p))|(std::uint64_t(u32(p+4))<<32));
            } else if(bits==16)value=std::bit_cast<std::int16_t>(std::uint16_t(u16(p)))/32768.0;
            else if(bits==24) { const auto n=std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|(std::uint32_t(bytes[p+2])<<16);value=(n>=0x800000?double(n)-16777216.0:double(n))/8388608.0; }
            else value=std::bit_cast<std::int32_t>(u32(p))/2147483648.0;
            if(!std::isfinite(value)||std::abs(value)>16)fail("IR WAV contains invalid or excessive sample values.");
            decoded[c][i]=float(value);peak=std::max(peak,std::abs(value));
        }
    }
    if(peak<1e-9)fail("IR WAV contains only silence in its first 200 ms.");
    CabinetImpulse result;result.sampleRate=int(std::round(targetRate));
    const auto outputFrames=std::min(std::size_t(std::ceil(targetRate*0.2)),std::size_t(std::ceil((dataBytes/align)*targetRate/sourceRate)));
    for(unsigned c=0;c<2;++c) {
        auto& output=result.channels[c];output.resize(outputFrames);
        const auto& source=decoded[c%channels];
        // Windowed-sinc resampling; lower the cutoff when downsampling.
        const double cutoff=std::min(1.0,targetRate/sourceRate);
        for(std::size_t i=0;i<outputFrames;++i) {
            const double position=i*double(sourceRate)/targetRate;const int center=int(position);
            double sum=0,weights=0;
            for(int j=center-24;j<=center+24;++j) {
                const double distance=position-j;
                if(std::abs(distance)>24)continue;
                const double angle=3.141592653589793*distance*cutoff;
                const double weight=cutoff*(std::abs(angle)<1e-12?1:std::sin(angle)/angle)*(0.5+0.5*std::cos(3.141592653589793*distance/24));
                if(j>=0&&j<int(source.size()))sum+=source[j]*weight;
                weights+=weight;
            }
            // Preserve impulse integral when sample rate changes.
            output[i]=float(sum/(std::abs(weights)>1e-12?weights:1)*double(sourceRate)/targetRate);
        }
    }
    return result;
}
