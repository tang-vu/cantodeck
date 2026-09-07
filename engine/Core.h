#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace canto {
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<double>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free, "Audio parameters require lock-free atomics");
inline float clean(float x) { return std::isfinite(x) ? std::clamp(x, -16.f, 16.f) : 0.f; }
template<class T, size_t N> class Ring {
    std::array<T,N> data{};
    alignas(64) std::atomic<uint64_t> read{0};
    alignas(64) std::atomic<uint64_t> write{0};
public:
    bool push(const T& value) noexcept { auto w=write.load(std::memory_order_relaxed); if(w-read.load(std::memory_order_acquire)>=N) return false; data[w%N]=value; write.store(w+1,std::memory_order_release); return true; }
    bool pop(T& value) noexcept { auto r=read.load(std::memory_order_relaxed); if(r==write.load(std::memory_order_acquire)) return false; value=data[r%N]; read.store(r+1,std::memory_order_release); return true; }
    size_t size() const noexcept { return size_t(write.load(std::memory_order_acquire)-read.load(std::memory_order_acquire)); }
    void reset() noexcept { read=0; write=0; } // Only while both endpoints stopped.
};
struct Parameters {
    std::atomic<float> inputBoostDb{0.f};
    std::atomic<float> mic{0.7f}, music{0.65f}, master{0.5f}, echo{0.18f}, feedback{0.25f}, delayMs{180.f}, reverb{0.12f}, tone{0.f}, threshold{-18.f};
    std::atomic<bool> monitor{false}, mute{false}, gate{true}, compressor{true}, eq{true}, effects{true}, musicMute{false};
    std::atomic<int> channel{0}; // 0=first, 1=second, 2=average
};
class VocalDSP {
    std::vector<float> delay;
    std::array<std::vector<float>,4> room;
    std::array<size_t,4> roomPos{};
    std::array<float,4> damping{};
    size_t pos=0;
    double rate=48000;
    float x1=0,y1=0, low=0, envelope=0, gain=0, echoGain=0, roomGain=0, toneGain=0, compGain=1;
    double delaySamples=0;
    float inputGain=1.f;
public:
    void prepare(double sr) { rate=sr; delay.assign(size_t(sr*0.8)+1,0); const double times[]={0.0297,0.0371,0.0411,0.0437}; for(size_t i=0;i<4;++i) room[i].assign(size_t(sr*times[i])+1,0); pos=0; roomPos={}; damping={}; delaySamples=0; inputGain=1; x1=y1=low=envelope=gain=echoGain=roomGain=toneGain=0; compGain=1; }
    float process(float input,const Parameters& p) noexcept {
        inputGain+=0.001f*(std::pow(10.f,std::clamp(p.inputBoostDb.load(),0.f,24.f)/20.f)-inputGain);
        const float x=clean(clean(input)*inputGain);
        const float hp=x-x1+float(std::exp(-2*3.141592653589793*75/rate))*y1; x1=x; y1=hp;
        envelope += (std::abs(hp)>envelope ? 0.01f:0.0002f)*(std::abs(hp)-envelope);
        float v=hp;
        if(p.gate.load()) v*=std::clamp(envelope/0.003f,0.15f,1.f);
        low+=float(1-std::exp(-2*3.141592653589793*1800/rate))*(v-low);
        toneGain+=0.001f*((p.eq.load()?p.tone.load():0.f)-toneGain);
        v+=(v-low)*toneGain;
        float targetComp=1;
        if(p.compressor.load()) { const float t=std::pow(10.f,p.threshold.load()/20); if(envelope>t) targetComp=std::pow(t/envelope,0.65f); }
        compGain+=(targetComp<compGain?0.01f:0.0005f)*(targetComp-compGain);
        gain+=0.001f*(p.mic.load()-gain); v*=compGain*gain;
        echoGain+=0.001f*((p.effects.load()?p.echo.load():0.f)-echoGain);
        roomGain+=0.001f*((p.effects.load()?p.reverb.load():0.f)-roomGain);
        const double wantedDelay=std::clamp(p.delayMs.load(),30.f,700.f)*rate/1000;
        if(delaySamples==0) delaySamples=wantedDelay;
        delaySamples+=0.0002*(wantedDelay-delaySamples);
        auto d=std::clamp(size_t(delaySamples),size_t(1),delay.size()-2);
        const float frac=float(delaySamples-double(d));
        const float near=delay[(pos+delay.size()-d)%delay.size()],far=delay[(pos+delay.size()-d-1)%delay.size()];
        const float tail=near+(far-near)*frac;
        delay[pos]=clean(v+tail*std::clamp(p.feedback.load(),0.f,0.65f)); pos=(pos+1)%delay.size();
        float reverberation=0;
        for(size_t j=0;j<4;++j) { float r=room[j][roomPos[j]]; damping[j]+=0.35f*(r-damping[j]); room[j][roomPos[j]]=clean(v+damping[j]*0.72f); roomPos[j]=(roomPos[j]+1)%room[j].size(); reverberation+=r*0.25f; }
        return clean(v+tail*echoGain+reverberation*roomGain);
    }
};
// Output-driven adaptive linear sample-rate conversion. Ring belongs to two callbacks.
class ClockBridge {
    Ring<float,32768> fifo;
    double phase=0, nominal=1, correction=1;
    float a=0,b=0;
    bool primed=false;
    size_t target=1024;
public:
    std::atomic<uint64_t> underruns{0}, overruns{0};
    std::atomic<double> ratio{1};
    void prepare(double inputRate,double outputRate,int block,bool lowLatency=false,int inputBlock=0,int outputBlock=0) {
        fifo.reset(); nominal=inputRate/outputRate; phase=0; correction=1; primed=false; a=b=0;
        const auto captureQuantum=inputBlock>0?inputBlock:block;
        const auto renderQuantum=outputBlock>0?outputBlock:block;
        const auto lowTarget=size_t(std::ceil(renderQuantum*nominal))+size_t(captureQuantum)*2+2;
        target=std::clamp(lowLatency?lowTarget:size_t(block*3),size_t(lowLatency?128:512),size_t(8192)); underruns=0; overruns=0;
    }
    void push(float v) noexcept { if(!fifo.push(clean(v))) ++overruns; }
    void beginBlock() noexcept { double error=(double(fifo.size())-double(target))/double(target); correction+=0.002*(std::clamp(1+error*0.01,0.98,1.02)-correction); ratio.store(nominal*correction); }
    float next() noexcept {
        if(!primed) { if(fifo.size()<target) return 0; fifo.pop(a); fifo.pop(b); primed=true; }
        const float result=a+(b-a)*float(phase); phase+=nominal*correction;
        while(phase>=1) { phase-=1; a=b; if(!fifo.pop(b)) { ++underruns; primed=false; a=b=0; phase=0; return 0; } }
        return result;
    }
    size_t buffered() const { return fifo.size(); }
    size_t targetFrames() const { return target; }
};
}
