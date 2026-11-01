#include "testkit.h"
#include "audiofile/impulsewav.h"
#include <bit>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
struct Fixture {
    std::filesystem::path folder=std::filesystem::temp_directory_path()/("chaosmaker-ir-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path file=folder/"test.wav";
    Fixture(){std::filesystem::create_directory(folder);}
    ~Fixture(){std::error_code error;std::filesystem::remove_all(folder,error);}
    void wave(int rate,int bits,int channels,int format,const std::vector<double>& values) {
        std::ofstream out(file,std::ios::binary);
        auto put=[&](std::uint64_t x,int bytes){for(int i=0;i<bytes;++i)out.put(char((x>>(i*8))&255));};
        const int size=int(values.size())*(bits/8);
        out.write("RIFF",4);put(36+size+(size&1),4);out.write("WAVEfmt ",8);put(16,4);
        put(format,2);put(channels,2);put(rate,4);put(rate*channels*bits/8,4);put(channels*bits/8,2);put(bits,2);
        out.write("data",4);put(size,4);
        for(double value:values) {
            if(format==3) { if(bits==32)put(std::bit_cast<std::uint32_t>(float(value)),4);else put(std::bit_cast<std::uint64_t>(value),8); }
            else put(std::uint64_t(std::int64_t(value*std::pow(2.0,bits-1))),bits/8);
        }
        if(size&1)out.put(0);
    }
};
bool fails(const std::string& path) {try{loadImpulseWav(path,48000);return false;}catch(const std::exception&){return true;}}
}
TEST(impulsewav, supported_encodings_and_stereo_samples_decode_correctly) {
    Fixture f;
    for(auto encoding:std::vector<std::pair<int,int>>{{1,16},{1,24},{1,32},{3,32},{3,64}}) {
        f.wave(48000,encoding.second,2,encoding.first,{0.5,-0.25,0,0});
        const auto ir=loadImpulseWav(f.file.string(),48000);
        CHECK(ir.channels[0].size()==2);CHECK_NEAR(ir.channels[0][0],0.5,1e-6);CHECK_NEAR(ir.channels[1][0],-0.25,1e-6);
    }
}
TEST(impulsewav, mono_is_duplicated_resampling_changes_length_and_ir_is_capped) {
    Fixture f;std::vector<double> input(44100,0);input[0]=0.5;f.wave(44100,16,1,1,input);
    const auto ir=loadImpulseWav(f.file.string(),48000);
    CHECK(ir.channels[0].size()==9600);CHECK(ir.channels[0]==ir.channels[1]);CHECK(ir.sampleRate==48000);
    double energy=0;for(float x:ir.channels[0])energy+=x*x;CHECK(energy>0.1&&energy<1);
}
TEST(impulsewav, missing_truncated_silent_and_nonfinite_files_are_rejected) {
    Fixture f;CHECK(fails(f.file.string()));
    {std::ofstream out(f.file);out<<"bad";}CHECK(fails(f.file.string()));
    f.wave(48000,16,1,1,{0,0,0});CHECK(fails(f.file.string()));
    f.wave(48000,32,1,3,{std::numeric_limits<double>::quiet_NaN()});CHECK(fails(f.file.string()));
    f.wave(48000,32,1,3,{0.5,0});std::filesystem::resize_file(f.file,45);CHECK(fails(f.file.string()));
}

TEST(impulsewav, extensible_pcm_wav_is_supported) {
    Fixture f;f.wave(48000,16,1,1,{0.5,0});
    std::ifstream input(f.file,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});input.close();
    const unsigned char extension[]{22,0,16,0,1,0,0,0,1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
    bytes.insert(bytes.begin()+36,std::begin(extension),std::end(extension));
    bytes[4]+=24;bytes[16]=40;bytes[20]=0xfe;bytes[21]=0xff;
    {std::ofstream output(f.file,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    CHECK_NEAR(loadImpulseWav(f.file.string(),48000).channels[0][0],0.5,1e-6);
}
