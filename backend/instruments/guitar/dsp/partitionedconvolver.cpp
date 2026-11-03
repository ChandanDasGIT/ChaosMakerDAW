#include "partitionedconvolver.h"
#include <algorithm>
#include <cmath>
void PartitionedConvolver::prepare(const std::vector<float>& ir) {
    head.fill(0);std::copy_n(ir.begin(),std::min(ir.size(),std::size_t(P)),head.begin());
    for(int i=0;i<N;++i) { int x=i,r=0;for(int b=0;b<8;++b){r=(r<<1)|(x&1);x>>=1;}reverse[i]=r; }
    for(int i=0;i<N/2;++i) { const float a=-6.283185307179586f*i/N;roots[i]={std::cos(a),std::sin(a)}; }
    const int count=ir.size()>P?int((ir.size()-P+P-1)/P):0;
    kernels.resize(count);spectra.resize(count);
    for(int j=0;j<count;++j) {
        kernels[j].fill({});
        for(int i=0;i<P && std::size_t(P+j*P+i)<ir.size();++i) kernels[j][i]=ir[P+j*P+i];
        fft(kernels[j],false);
    }
    reset();
}
void PartitionedConvolver::fft(Spectrum& data,bool inverse) noexcept {
    for(int i=0;i<N;++i)if(i<reverse[i])std::swap(data[i],data[reverse[i]]);
    for(int length=2;length<=N;length*=2)
        for(int start=0;start<N;start+=length)
            for(int j=0;j<length/2;++j) {
                auto root=roots[j*N/length];if(inverse)root=std::conj(root);
                const auto a=data[start+j],b=data[start+j+length/2]*root;
                data[start+j]=a+b;data[start+j+length/2]=a-b;
            }
    if(inverse)for(auto& value:data)value/=N;
}
void PartitionedConvolver::reset() noexcept {
    history.fill(0);block.fill(0);tail.fill(0);overlap.fill(0);work.fill({});sum.fill({});
    for(auto& spectrum:spectra)spectrum.fill({});
    position=headPosition=spectrumPosition=0;
}
void PartitionedConvolver::completeBlock() noexcept {
    if(kernels.empty())return;
    work.fill({});for(int i=0;i<P;++i)work[i]=block[i];fft(work,false);
    spectra[spectrumPosition]=work;sum.fill({});
    const int count=int(kernels.size());
    for(int part=0;part<count;++part) {
        const auto& input=spectra[(spectrumPosition-part+count)%count];
        for(int k=0;k<N;++k)sum[k]+=input[k]*kernels[part][k];
    }
    fft(sum,true);
    for(int i=0;i<P;++i){tail[i]=sum[i].real()+overlap[i];overlap[i]=sum[i+P].real();}
    spectrumPosition=(spectrumPosition+1)%count;
}
float PartitionedConvolver::process(float input) noexcept {
    history[headPosition]=input;float output=tail[position];
    for(int i=0;i<P;++i)output+=head[i]*history[(headPosition-i+P)%P];
    headPosition=(headPosition+1)%P;block[position]=input;
    if(++position==P){completeBlock();position=0;}
    return output;
}
