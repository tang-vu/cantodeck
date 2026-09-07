#pragma once
#include "engine/AudioEngine.h"
#include "platform/windows/InputLevel.h"

inline int runOffline(const juce::String& args) {
    using namespace juce;
    auto tokens=StringArray::fromTokens(args,true); for(auto& t:tokens) t=t.unquoted(); tokens.removeEmptyStrings();
    if(tokens.size()<2) return 2;
    auto ownedEngine=std::make_unique<canto::AudioEngine>();
    auto& engine=*ownedEngine;
    const File destination=File::getCurrentWorkingDirectory().getChildFile(tokens[tokens.size()-1]);
    if(destination.exists()) return 3;
    if(tokens[0]=="--diagnostics" || tokens[0]=="--probe" || tokens[0]=="--probe-low") {
        String report=engine.diagnostics()+"\nInputs:\n"+engine.inputs().joinIntoString("\n")+"\nOutputs:\n"+engine.outputs().joinIntoString("\n")+"\n"+canto::windowsInputLevels();
        if(tokens[0]=="--probe" || tokens[0]=="--probe-low") {
            auto ins=engine.inputs(),outs=engine.outputs();
            if(ins.isEmpty() || outs.isEmpty()) report+="\nPROBE: no input/output endpoints";
            else { const bool low=tokens[0]=="--probe-low"; const String selectedIn=tokens.size()==4?tokens[1]:ins[0],selectedOut=tokens.size()==4?tokens[2]:outs[0]; report+="\nSelected input: "+selectedIn+"\nSelected output: "+selectedOut; auto e=engine.connect(selectedIn,selectedOut,48000,low?128:512,low); if(e.isNotEmpty()) report+="\nOPEN FAILED: "+e; else {Thread::sleep(3000); report+="\nSILENT PROBE (no monitoring or recording):\n"+engine.diagnostics()+"\nInput peak: "+String(engine.inputPeak.load());} }
        }
        return destination.replaceWithText(report)?0:4;
    }
    if(tokens[0]!="--render" || tokens.size()!=3) return 2;
    const File source=File::getCurrentWorkingDirectory().getChildFile(tokens[1]);
    WavAudioFormat format; auto stream=source.createInputStream(); if(!stream) return 5;
    std::unique_ptr<AudioFormatReader> reader(format.createReaderFor(stream.release(),true)); if(!reader || reader->numChannels>2 || reader->sampleRate<8000 || reader->sampleRate>192000) return 6;
    engine.prepareOffline(reader->sampleRate);
    auto e=engine.recorder.start(destination,reader->sampleRate,true); if(e.isNotEmpty()) return 7;
    AudioBuffer<float> input(2,256),output(2,256);
    int64 processed=0,total=reader->lengthInSamples+int64(reader->sampleRate*2);
    while(processed<total) {
        int n=int(std::min<int64>(256,total-processed)); input.clear();
        if(processed<reader->lengthInSamples) reader->read(&input,0,int(std::min<int64>(n,reader->lengthInSamples-processed)),processed,true,true);
        engine.render(input.getArrayOfReadPointers(),2,nullptr,0,n,true);
        engine.render(nullptr,0,output.getArrayOfWritePointers(),2,n,false);
        processed+=n;
        // Offline producer throttling, never executed in a live audio callback.
        if(processed%32768==0) Thread::sleep(10);
    }
    engine.recorder.stop();
    if(engine.recorder.failed.load() || engine.recorder.frames.load()!=uint64_t(total)) return 8;
    for(auto suffix:{String{},String("-dry"),String("-wet")}) {
        auto f=suffix.isEmpty()?destination:destination.getSiblingFile(destination.getFileNameWithoutExtension()+suffix+".wav");
        auto checkStream=f.createInputStream(); if(!checkStream) return 9;
        std::unique_ptr<AudioFormatReader> check(format.createReaderFor(checkStream.release(),true));
        if(!check || check->lengthInSamples!=total || check->numChannels!=(suffix.isEmpty()?2u:1u)) return 9;
    }
    engine.params.mute=true;
    engine.render(nullptr,0,output.getArrayOfWritePointers(),2,256,false);
    if(output.getMagnitude(0,256)!=0) return 10;
    return 0;
}
