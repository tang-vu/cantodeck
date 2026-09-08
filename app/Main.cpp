#include "engine/AudioEngine.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "Offline.h"
#include "EqPanel.h"
#include <future>

using namespace juce;
class Console final : public Component, private Timer
{
    canto::AudioEngine engine;
    LookAndFeel_V4 theme;
    ComboBox input, output, preset, channel, sampleRate, buffer, queue;
    TextButton connect, refresh, monitor, mute, load, play, record, lyricsButton, advanced, language, test,
        importButton, exportButton, diagnosticButton, fullLyrics, measureLatency, eqEditor;
    ToggleButton stems, gate, compressor, eq, effects, musicMute, lowLatency, nativeBackend, transparent;
    Slider mic, boost, music, master, echo, reverb, delay, feedback, tone, threshold, position, offset;
    Label title, status, meters, lyrics, trackLabel;
    TextEditor details;
    std::unique_ptr<FileChooser> chooser;
    std::vector<File> tracks;
    std::unique_ptr<DocumentWindow> lyricWindow;
    std::unique_ptr<EqWindow> eqWindow;
    Label* bigLyric = nullptr;
    std::future<String> trackLoad;
    std::future<canto::LatencyResult> latencyAnalysis;
    uint32 measurementStarted = 0;
    File pendingTrack;
    bool vi = true, showAdvanced = false;
    const bool persistSession;
    String message;
    String savedInputId, savedOutputId;
    struct Lyric
    {
        double time;
        String text;
    };
    std::vector<Lyric> lines;
    File settings =
        File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("CantoDeck/session.json");
    String tr(const char* a, const char* b) const { return String::fromUTF8(vi ? a : b); }
    void notify(String s)
    {
        message = s;
        status.setText(s, dontSendNotification);
    }
    void slider(Slider& s, const String& name, double low, double high, double initial,
                std::function<void(double)> action)
    {
        addAndMakeVisible(s);
        s.setName(name);
        s.setRange(low, high, 0.01);
        s.setValue(initial);
        s.setTextBoxStyle(Slider::TextBoxRight, false, 75, 24);
        s.setTooltip(name);
        s.onValueChange = [&s, action] { action(s.getValue()); };
    }
    void updateDeviceMenus()
    {
        const auto i = input.getText(), o = output.getText();
        input.clear(dontSendNotification);
        output.clear(dontSendNotification);
        input.addItemList(engine.inputs(), 1);
        output.addItemList(engine.outputs(), 1);
        input.setText(i, dontSendNotification);
        output.setText(o, dontSendNotification);
    }
    void scan()
    {
        engine.scan();
        updateDeviceMenus();
        notify(tr("Chọn mic và loa, rồi nhấn Kết nối. Nghe mic mặc định tắt.",
                  "Select input and output, then Connect. Monitoring starts off."));
    }
    void selectTrack(const File& f)
    {
        if (engine.latencyProbe.busy() || latencyAnalysis.valid())
        {
            notify(tr("Chờ phép đo trễ kết thúc trước khi đổi bài.",
                      "Wait for latency measurement to finish before changing tracks."));
            return;
        }
        if (trackLoad.valid() || engine.recorder.active.load())
        {
            notify(tr("Dừng thu trước khi đổi bài.", "Stop recording before changing tracks."));
            return;
        }
        pendingTrack = f;
        notify(tr("Đang đọc WAV…", "Loading WAV…"));
        for (auto* c : std::initializer_list<Component*>{&load, &queue, &connect, &refresh, &record, &play})
            c->setEnabled(false);
        trackLoad = std::async(std::launch::async,
                               [this, f]
                               {
                                   try
                                   {
                                       return engine.loadTrack(f);
                                   }
                                   catch (const std::exception& e)
                                   {
                                       return String("WAV load failed: ") + e.what();
                                   }
                               });
    }
    var state()
    {
        auto* o = new DynamicObject();
        o->setProperty("version", 1);
        o->setProperty("input", input.getText());
        o->setProperty("output", output.getText());
        o->setProperty("vi", vi);
        const auto currentInputId = engine.endpointId(input.getText(), true),
                   currentOutputId = engine.endpointId(output.getText(), false);
        if (currentInputId.isNotEmpty())
            savedInputId = currentInputId;
        if (currentOutputId.isNotEmpty())
            savedOutputId = currentOutputId;
        o->setProperty("inputId", savedInputId);
        o->setProperty("outputId", savedOutputId);
        o->setProperty("nativeBackend", nativeBackend.getToggleState());
        o->setProperty("lowLatency", lowLatency.getToggleState());
        o->setProperty("sampleRateId", sampleRate.getSelectedId());
        o->setProperty("bufferId", buffer.getSelectedId());
        o->setProperty("channelId", channel.getSelectedId());
        for (auto* s : {&mic, &boost, &music, &master, &echo, &reverb, &delay, &feedback, &tone, &threshold})
            o->setProperty(s->getName(), s->getValue());
        o->setProperty("gate", gate.getToggleState());
        o->setProperty("compressor", compressor.getToggleState());
        o->setProperty("eq", eq.getToggleState());
        o->setProperty("effects", effects.getToggleState());
        o->setProperty("transparent", transparent.getToggleState());
        Array<var> bands;
        for (const auto& band : engine.params.eqBands)
        {
            auto* values = new DynamicObject();
            values->setProperty("frequency", band.frequency.load());
            values->setProperty("gainDb", band.gainDb.load());
            values->setProperty("q", band.q.load());
            bands.add(var(values));
        }
        o->setProperty("eqBands", var(bands));
        return var(o);
    }
    bool restore(const var& v, bool devices)
    {
        if (!v.isObject() || int(v["version"]) != 1)
        {
            notify("Unsupported/invalid JSON version");
            return false;
        }
        for (auto* s : {&mic, &boost, &music, &master, &echo, &reverb, &delay, &feedback, &tone, &threshold})
        {
            auto x = v[Identifier(s->getName())];
            if (!x.isDouble() && !x.isInt())
                continue;
            double d = double(x);
            if (std::isfinite(d))
                s->setValue(jlimit(s->getMinimum(), s->getMaximum(), d));
        }
        for (auto item : {std::pair<ToggleButton*, const char*>{&gate, "gate"},
                          {&compressor, "compressor"},
                          {&eq, "eq"},
                          {&effects, "effects"}})
            if (v.hasProperty(item.second))
                item.first->setToggleState(bool(v[item.second]), sendNotificationSync);
        if (v.hasProperty("transparent"))
            transparent.setToggleState(bool(v["transparent"]), sendNotificationSync);
        for (auto& band : engine.params.eqBands)
        {
            band.frequency = 1000;
            band.gainDb = 0;
            band.q = 0.707f;
        }
        if (auto* bands = v["eqBands"].getArray())
            for (int i = 0; i < std::min(3, bands->size()); ++i)
            {
                auto assign = [&](const char* name, std::atomic<float>& target, float low, float high)
                {
                    auto value = (*bands)[i][name];
                    if ((value.isDouble() || value.isInt()) && std::isfinite(double(value)))
                        target = jlimit(low, high, float(value));
                };
                assign("frequency", engine.params.eqBands[size_t(i)].frequency, 40, 16000);
                assign("gainDb", engine.params.eqBands[size_t(i)].gainDb, -12, 12);
                assign("q", engine.params.eqBands[size_t(i)].q, 0.2f, 8);
            }
        if (devices)
        {
            savedInputId = v["inputId"].toString();
            savedOutputId = v["outputId"].toString();
            const auto resolvedInput = engine.endpointName(savedInputId, true),
                       resolvedOutput = engine.endpointName(savedOutputId, false);
            input.setText(resolvedInput.isNotEmpty() ? resolvedInput : v["input"].toString(),
                          dontSendNotification);
            output.setText(resolvedOutput.isNotEmpty() ? resolvedOutput : v["output"].toString(),
                           dontSendNotification);
            vi = bool(v["vi"]);
            if (v.hasProperty("nativeBackend"))
                nativeBackend.setToggleState(bool(v["nativeBackend"]), dontSendNotification);
            if (v.hasProperty("lowLatency"))
                lowLatency.setToggleState(bool(v["lowLatency"]), dontSendNotification);
            if (v.hasProperty("sampleRateId"))
                sampleRate.setSelectedId(jlimit(1, 2, int(v["sampleRateId"])), dontSendNotification);
            if (v.hasProperty("bufferId"))
                buffer.setSelectedId(jlimit(1, 4, int(v["bufferId"])), dontSendNotification);
            if (v.hasProperty("channelId"))
                channel.setSelectedId(jlimit(1, 3, int(v["channelId"])), sendNotificationSync);
        }
        return true;
    }
    void save()
    {
        if (!persistSession)
            return;
        settings.getParentDirectory().createDirectory();
        if (settings.existsAsFile() && JSON::parse(settings).isObject())
            settings.copyFileTo(settings.getSiblingFile("session.last-good.json"));
        TemporaryFile temp(settings);
        if (temp.getFile().replaceWithText(JSON::toString(state())))
            temp.overwriteTargetFileWithTemporary();
    }
    void choose(const String& titleText, const String& wildcard, int flags, std::function<void(File)> action)
    {
        chooser = std::make_unique<FileChooser>(titleText, File{}, wildcard);
        chooser->launchAsync(flags,
                             [safe = Component::SafePointer<Console>(this), action](const FileChooser& fc)
                             {
                                 if (safe && fc.getResult() != File{})
                                     action(fc.getResult());
                             });
    }
    void readLyrics(const File& file)
    {
        lines.clear();
        StringArray text;
        text.addLines(file.loadFileAsString());
        for (auto line : text)
        {
            bool tagged = false;
            std::vector<double> times;
            while (line.startsWithChar('[') && line.containsChar(']'))
            {
                auto tag =
                    line.fromFirstOccurrenceOf("[", false, false).upToFirstOccurrenceOf("]", false, false);
                line = line.fromFirstOccurrenceOf("]", false, false);
                if (tag.containsChar(':') && CharacterFunctions::isDigit(tag[0]))
                {
                    auto parts = StringArray::fromTokens(tag, ":", "");
                    if (parts.size() == 2)
                    {
                        times.push_back(parts[0].getDoubleValue() * 60 + parts[1].getDoubleValue());
                        tagged = true;
                    }
                }
            }
            for (double t : times)
                lines.push_back({t, line});
            if (!tagged && line.isNotEmpty())
                lines.push_back({-1, line});
        }
        std::stable_sort(lines.begin(), lines.end(), [](auto& a, auto& b) { return a.time < b.time; });
    }
    void localize()
    {
        title.setText("CantoDeck  /  WINDOWS ALPHA", dontSendNotification);
        connect.setButtonText(tr("Kết nối", "Connect"));
        refresh.setButtonText(tr("Quét thiết bị", "Refresh devices"));
        monitor.setButtonText(tr("Bật / tắt nghe mic", "Toggle monitoring"));
        mute.setButtonText(tr("TẮT TẤT CẢ / MỞ LẠI", "MUTE ALL / RESTORE"));
        load.setButtonText(tr("Mở nhạc WAV", "Open WAV"));
        play.setButtonText(tr("Phát / Tạm dừng", "Play / Pause"));
        record.setButtonText(tr("Thu / Dừng thu", "Record / Stop"));
        lyricsButton.setButtonText(tr("Mở LRC / TXT", "Open LRC / TXT"));
        advanced.setButtonText(tr("Nâng cao", "Advanced"));
        language.setButtonText("VI / EN");
        test.setButtonText(tr("Thử loa nhỏ", "Quiet output test"));
        fullLyrics.setButtonText(tr("Cửa sổ lời / F11", "Lyrics window / F11"));
        importButton.setButtonText(tr("Nhập preset", "Import preset"));
        exportButton.setButtonText(tr("Xuất preset", "Export preset"));
        diagnosticButton.setButtonText(tr("Lưu chẩn đoán", "Export diagnostics"));
        stems.setButtonText(tr("Thu thêm giọng dry / wet", "Separate dry / wet WAV"));
        gate.setButtonText("Expander");
        compressor.setButtonText("Compressor");
        eq.setButtonText("Vocal EQ");
        effects.setButtonText("Echo / Room");
        musicMute.setButtonText(tr("Tắt nhạc", "Mute music"));
        repaint();
        lowLatency.setButtonText(tr("Giảm trễ", "Low latency"));
        nativeBackend.setButtonText("Native");
        transparent.setButtonText(tr("Giọng gốc", "Dry voice"));
        measureLatency.setButtonText(tr("Đo trễ · phát thử", "Measure · test sound"));
    }

  public:
    explicit Console(bool persist = true) : persistSession(persist)
    {
        theme.setColour(ResizableWindow::backgroundColourId, Colour(0xff141922));
        theme.setColour(Slider::thumbColourId, Colour(0xff49cbb0));
        theme.setColour(TextButton::buttonColourId, Colour(0xff293547));
        setLookAndFeel(&theme);
        for (auto* c : std::initializer_list<Component*>{
                 &input,      &output,       &preset,       &channel,          &sampleRate, &buffer,
                 &queue,      &fullLyrics,   &connect,      &refresh,          &monitor,    &mute,
                 &load,       &play,         &record,       &lyricsButton,     &advanced,   &language,
                 &test,       &importButton, &exportButton, &diagnosticButton, &stems,      &gate,
                 &compressor, &eq,           &effects,      &musicMute,        &title,      &status,
                 &meters,     &lyrics,       &trackLabel,   &details})
            addAndMakeVisible(c);
        title.setFont(Font(FontOptions(25.f, Font::bold)));
        lyrics.setFont(Font(FontOptions(25.f)));
        lyrics.setJustificationType(Justification::centred);
        status.setColour(Label::textColourId, Colour(0xffefca80));
        details.setMultiLine(true);
        details.setReadOnly(true);
        input.setTextWhenNothingSelected("Microphone");
        output.setTextWhenNothingSelected("Speakers / headphones");
        addAndMakeVisible(lowLatency);
        lowLatency.setTooltip("WASAPI shared low-latency mode. Press Connect to apply. Driver support "
                              "required; disable if opening fails or sound crackles.");
        addAndMakeVisible(nativeBackend);
        nativeBackend.setTooltip("Native WASAPI: one MMCSS thread, independent capture/output clocks. Press "
                                 "Connect to apply. Experimental until physical validation.");
        addAndMakeVisible(transparent);
        transparent.setTooltip("Bypass all vocal coloration and effects; software gain remains. Use this to "
                               "judge monitoring latency and transparency.");
        addAndMakeVisible(measureLatency);
        addAndMakeVisible(eqEditor);
        eqEditor.setButtonText("EQ 3 bands");
        eqEditor.onClick = [this]
        {
            if (!eqWindow)
                eqWindow = std::make_unique<EqWindow>(engine.params);
            eqWindow->setVisible(true);
            eqWindow->toFront(true);
        };
        measureLatency.setTooltip("Plays a quiet short probe and measures speaker-to-microphone return. "
                                  "Pause YouTube first. Includes acoustic travel and device queues; mic "
                                  "audio stays in RAM. Monitoring stays off afterward.");
        transparent.setToggleState(true, dontSendNotification);
        engine.params.transparent = true;
        slider(mic, "Mic", 0, 8, 0.7, [this](double v) { engine.params.mic = float(v); });
        mic.setSkewFactorFromMidPoint(1.5);
        slider(boost, "Mic boost dB", 0, 24, 0, [this](double v) { engine.params.inputBoostDb = float(v); });
        boost.setTextValueSuffix(" dB");
        boost.setTooltip("Software input gain before expander/compressor; also raises noise. Start at +6 dB "
                         "with headphones.");
        slider(music, "Music", 0, 1.5, 0.65, [this](double v) { engine.params.music = float(v); });
        slider(master, "Master", 0, 1, 0.5, [this](double v) { engine.params.master = float(v); });
        slider(echo, "Echo", 0, 0.7, 0.18, [this](double v) { engine.params.echo = float(v); });
        slider(reverb, "Room", 0, 0.5, 0.12, [this](double v) { engine.params.reverb = float(v); });
        slider(delay, "Delay ms", 30, 700, 180, [this](double v) { engine.params.delayMs = float(v); });
        slider(feedback, "Feedback", 0, 0.65, 0.25, [this](double v) { engine.params.feedback = float(v); });
        slider(tone, "Tone", -0.8, 1, 0, [this](double v) { engine.params.tone = float(v); });
        slider(threshold, "Comp dB", -36, 0, -18, [this](double v) { engine.params.threshold = float(v); });
        slider(position, "Seek sec", 0, 1, 0, [this](double v) { engine.seek = v; });
        slider(offset, "Lyrics offset sec", -20, 20, 0, [](double) {});
        preset.addItemList(
            {"Natural", "Warm Karaoke", "Bright Karaoke", "Small Room", "Speech", "Dry Recording"}, 1);
        preset.setSelectedId(1, dontSendNotification);
        preset.onChange = [this]
        {
            int p = preset.getSelectedId();
            for (auto& band : engine.params.eqBands)
                band.gainDb = 0;
            echo.setValue(p == 6 ? 0 : p == 5 ? 0.03 : 0.18);
            reverb.setValue(p == 6 ? 0 : p == 4 ? 0.22 : 0.1);
            tone.setValue(p == 2 ? -0.35 : p == 3 ? 0.4 : 0);
            delay.setValue(p == 2 ? 220 : 180);
            gate.setToggleState(p == 5, sendNotificationSync);
            compressor.setToggleState(p != 6 && p != 1, sendNotificationSync);
            transparent.setToggleState(p == 1 || p == 6, sendNotificationSync);
        };
        transparent.onClick = [this] { engine.params.transparent = transparent.getToggleState(); };
        measureLatency.onClick = [this]
        {
            if (engine.startLatencyMeasurement())
            {
                measurementStarted = Time::getMillisecondCounter();
                notify(tr("Đang phát tín hiệu nhỏ để đo loa → mic. Nhạc ngoài cần tạm dừng; nghe mic đã tắt.",
                          "Playing a quiet speaker-to-mic probe. Pause external music; monitoring is off."));
            }
            else
                notify(tr("Cần kết nối âm thanh và dừng thu trước khi đo.",
                          "Connect audio and stop recording before measuring."));
        };
        channel.addItemList({"Mic 1", "Mic 2", "Mic 1 + 2 (mono)"}, 1);
        channel.setSelectedId(1);
        channel.onChange = [this] { engine.params.channel = channel.getSelectedId() - 1; };
        sampleRate.addItemList({"48000 Hz", "44100 Hz"}, 1);
        sampleRate.setSelectedId(1);
        buffer.addItemList({"128 samples", "256 samples", "512 samples", "1024 samples"}, 1);
        buffer.setSelectedId(3);
        for (auto* b : {&gate, &compressor, &eq, &effects})
            b->setToggleState(true, dontSendNotification);
        gate.onClick = [this] { engine.params.gate = gate.getToggleState(); };
        compressor.onClick = [this] { engine.params.compressor = compressor.getToggleState(); };
        eq.onClick = [this] { engine.params.eq = eq.getToggleState(); };
        effects.onClick = [this] { engine.params.effects = effects.getToggleState(); };
        musicMute.onClick = [this] { engine.params.musicMute = musicMute.getToggleState(); };
        refresh.onClick = [this]
        {
            engine.close();
            scan();
        };
        connect.onClick = [this]
        {
            auto e = engine.connect(input.getText(), output.getText(),
                                    sampleRate.getSelectedId() == 2 ? 44100 : 48000,
                                    1 << (buffer.getSelectedId() + 6), lowLatency.getToggleState(),
                                    nativeBackend.getToggleState());
            notify(e.isEmpty()
                       ? tr("Đã kết nối. Kiểm tra mức mic rồi bật nghe mic; nên dùng tai nghe có dây.",
                            "Connected. Check input level before enabling monitoring; use wired headphones.")
                       : e);
            save();
        };
        nativeBackend.onClick = [this]
        {
            if (nativeBackend.getToggleState())
            {
                lowLatency.setToggleState(true, dontSendNotification);
                buffer.setSelectedId(1);
            }
            notify(tr("Nhấn Kết nối để dùng backend vừa chọn. Nghe mic sẽ tắt khi đổi backend.",
                      "Press Connect to apply backend selection. Monitoring turns off on backend changes."));
        };
        lowLatency.onClick = [this]
        {
            if (lowLatency.getToggleState())
                buffer.setSelectedId(1);
            notify(tr("Nhấn Kết nối để áp dụng. Nếu rè/lỗi, tăng buffer hoặc tắt Giảm trễ.",
                      "Press Connect to apply. If crackling/open failure occurs, increase buffer or disable "
                      "Low latency."));
        };
        monitor.onClick = [this]
        {
            if (engine.connected())
                engine.params.monitor = !engine.params.monitor.load();
            else
                notify(tr("Chưa kết nối âm thanh. Chọn thiết bị và nhấn Kết nối.",
                          "Audio is disconnected. Select devices and Connect."));
        };
        mute.onClick = [this] { engine.params.mute = !engine.params.mute.load(); };
        test.onClick = [this]
        {
            if (engine.connected())
                engine.testRequested = true;
        };
        load.onClick = [this]
        {
            choose("Open WAV", "*.wav", FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                   [this](File f)
                   {
                       tracks.push_back(f);
                       queue.addItem(f.getFileName(), int(tracks.size()));
                       queue.setSelectedId(int(tracks.size()), dontSendNotification);
                       selectTrack(f);
                   });
        };
        queue.onChange = [this]
        {
            auto index = queue.getSelectedId() - 1;
            if (index >= 0 && index < int(tracks.size()))
                selectTrack(tracks[size_t(index)]);
        };
        fullLyrics.onClick = [this]
        {
            if (lyricWindow)
            {
                lyricWindow->setVisible(!lyricWindow->isVisible());
                return;
            }
            class LyricsWindow final : public DocumentWindow
            {
              public:
                LyricsWindow()
                    : DocumentWindow("CantoDeck Lyrics — F11 / Esc", Colour(0xff141922), allButtons)
                {
                }
                void closeButtonPressed() override { setVisible(false); }
                bool keyPressed(const KeyPress& key) override
                {
                    if (key.getKeyCode() == KeyPress::F11Key)
                    {
                        setFullScreen(!isFullScreen());
                        return true;
                    }
                    if (key.getKeyCode() == KeyPress::escapeKey)
                    {
                        setFullScreen(false);
                        return true;
                    }
                    return false;
                }
            };
            lyricWindow = std::make_unique<LyricsWindow>();
            bigLyric = new Label;
            bigLyric->setFont(Font(FontOptions(40.f)));
            bigLyric->setJustificationType(Justification::centred);
            lyricWindow->setUsingNativeTitleBar(true);
            lyricWindow->setContentOwned(bigLyric, false);
            lyricWindow->setResizable(true, true);
            lyricWindow->centreWithSize(900, 500);
            lyricWindow->setVisible(true);
        };
        play.onClick = [this]
        {
            if (engine.seconds >= engine.duration)
                engine.seek = 0;
            engine.playing = !engine.playing.load();
        };
        record.onClick = [this]
        {
            if (engine.recorder.active.load() || engine.recorder.failed.load())
            {
                engine.stopRecording();
                notify(tr("Đã đóng WAV. Kiểm tra chẩn đoán nếu có lỗi thu.",
                          "WAV finalized. Check diagnostics for recording errors."));
                engine.recorder.failed = false;
            }
            else
                choose("New recording WAV", "*.wav",
                       FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                       [this](File f)
                       { notify(engine.record(f.withFileExtension("wav"), stems.getToggleState())); });
        };
        lyricsButton.onClick = [this]
        {
            choose("Lyrics", "*.lrc;*.txt",
                   FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                   [this](File f) { readLyrics(f); });
        };
        advanced.onClick = [this]
        {
            showAdvanced = !showAdvanced;
            resized();
        };
        language.onClick = [this]
        {
            vi = !vi;
            localize();
        };
        importButton.onClick = [this]
        {
            choose("Import JSON", "*.json",
                   FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                   [this](File f) { restore(JSON::parse(f), false); });
        };
        exportButton.onClick = [this]
        {
            choose("Export JSON", "*.json",
                   FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                   [this](File f)
                   {
                       if (f.exists())
                       {
                           notify("File exists; choose a new name");
                           return;
                       }
                       notify(f.replaceWithText(JSON::toString(state())) ? "Saved" : "Write failed");
                   });
        };
        diagnosticButton.onClick = [this]
        {
            choose("Diagnostics", "*.txt",
                   FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                   [this](File f)
                   {
                       if (f.exists())
                       {
                           notify("File exists; choose a new name");
                           return;
                       }
                       notify(f.replaceWithText(engine.diagnostics()) ? "Saved" : "Write failed");
                   });
        };
        scan();
        if (persistSession && settings.existsAsFile())
        {
            auto v = JSON::parse(settings);
            if (!v.isObject())
                v = JSON::parse(settings.getSiblingFile("session.last-good.json"));
            restore(v, true);
        }
        localize();
        setSize(1080, 820);
        startTimerHz(20);
    }
    ~Console() override
    {
        stopTimer();
        if (trackLoad.valid())
            trackLoad.wait();
        if (latencyAnalysis.valid())
            latencyAnalysis.wait();
        lyricWindow.reset();
        eqWindow.reset();
        engine.close();
        save();
        setLookAndFeel(nullptr);
    }
    void smokeAdvanced()
    {
        showAdvanced = true;
        resized();
        details.setText(engine.diagnostics(), false);
    }
    bool smokeEq(const File& file)
    {
        engine.params.eqBands[1].frequency = 2300;
        engine.params.eqBands[1].gainDb = -3;
        engine.params.eqBands[1].q = 1.2f;
        const auto saved = JSON::parse(JSON::toString(state()));
        engine.params.eqBands[1].gainDb = 0;
        if (!restore(saved, false) || engine.params.eqBands[1].frequency != 2300 ||
            engine.params.eqBands[1].gainDb != -3 || std::abs(engine.params.eqBands[1].q - 1.2f) > 0.0001f)
            return false;
        EqPanel panel(engine.params);
        if (file.exists())
            return false;
        auto stream = file.createOutputStream();
        return stream && PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
    }
    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff141922));
        g.setColour(Colours::lightgrey);
        g.setFont(14.f);
        for (auto* s :
             {&mic, &boost, &music, &master, &echo, &reverb, &delay, &feedback, &tone, &threshold, &offset})
            if (s->isVisible())
                g.drawText(s->getName(), s->getX(), s->getY() - 18, s->getWidth(), 18,
                           Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        auto row = r.removeFromTop(42);
        title.setBounds(row.removeFromLeft(560));
        language.setBounds(row.removeFromRight(90));
        advanced.setBounds(row.removeFromRight(130));
        row = r.removeFromTop(38);
        input.setBounds(row.removeFromLeft(r.getWidth() / 3).reduced(3));
        output.setBounds(row.removeFromLeft(r.getWidth() / 3).reduced(3));
        connect.setBounds(row.removeFromLeft(125).reduced(3));
        refresh.setBounds(row.reduced(3));
        row = r.removeFromTop(42);
        monitor.setBounds(row.removeFromLeft(210).reduced(3));
        test.setBounds(row.removeFromLeft(145).reduced(3));
        mute.setBounds(row.reduced(3));
        status.setBounds(r.removeFromTop(44));
        meters.setBounds(r.removeFromTop(28));
        r.removeFromTop(22);
        row = r.removeFromTop(36);
        auto quarter = row.getWidth() / 4;
        mic.setBounds(row.removeFromLeft(quarter).reduced(5, 0));
        boost.setBounds(row.removeFromLeft(quarter).reduced(5, 0));
        music.setBounds(row.removeFromLeft(quarter).reduced(5, 0));
        master.setBounds(row.reduced(5, 0));
        r.removeFromTop(24);
        row = r.removeFromTop(32);
        preset.setBounds(row.removeFromLeft(180));
        transparent.setBounds(row.removeFromLeft(110));
        echo.setBounds(row.removeFromLeft(230).reduced(8, 0));
        reverb.setBounds(row.removeFromLeft(230).reduced(8, 0));
        musicMute.setBounds(row);
        r.removeFromTop(10);
        row = r.removeFromTop(34);
        load.setBounds(row.removeFromLeft(150).reduced(3));
        play.setBounds(row.removeFromLeft(160).reduced(3));
        lyricsButton.setBounds(row.removeFromLeft(160).reduced(3));
        record.setBounds(row.removeFromLeft(145).reduced(3));
        stems.setBounds(row);
        row = r.removeFromTop(30);
        trackLabel.setBounds(row.removeFromLeft(r.getWidth() / 2));
        queue.setBounds(row.removeFromLeft(r.getWidth() / 3));
        fullLyrics.setBounds(row);
        position.setBounds(r.removeFromTop(30));
        for (auto* c : std::initializer_list<Component*>{
                 &channel, &sampleRate, &buffer, &lowLatency, &nativeBackend, &delay, &feedback, &tone,
                 &threshold, &gate, &compressor, &eq, &effects, &details, &importButton, &exportButton,
                 &diagnosticButton, &measureLatency, &eqEditor, &offset})
            c->setVisible(showAdvanced);
        if (showAdvanced)
        {
            r.removeFromTop(22);
            row = r.removeFromTop(30);
            for (auto* s : {&delay, &feedback, &tone, &threshold})
                s->setBounds(row.removeFromLeft(r.getWidth() / 4).reduced(5, 0));
            row = r.removeFromTop(32);
            for (auto* c :
                 std::initializer_list<Component*>{&channel, &sampleRate, &buffer, &lowLatency,
                                                   &nativeBackend, &gate, &compressor, &eq, &effects})
                c->setBounds(row.removeFromLeft(r.getWidth() / 9).reduced(2));
            row = r.removeFromTop(40);
            importButton.setBounds(row.removeFromLeft(130).reduced(3));
            exportButton.setBounds(row.removeFromLeft(130).reduced(3));
            diagnosticButton.setBounds(row.removeFromLeft(140).reduced(3));
            measureLatency.setBounds(row.removeFromLeft(185).reduced(3));
            eqEditor.setBounds(row.removeFromLeft(115).reduced(3));
            offset.setBounds(row.reduced(8));
            details.setBounds(r.removeFromBottom(140));
        }
        lyrics.setBounds(r);
        lyrics.setFont(Font(FontOptions(showAdvanced ? 20.f : 25.f)));
    }
    void timerCallback() override
    {
        // Finalize an interrupted take on the control thread, never in a device
        // callback. Do not race the worker that is replacing a backing track.
        if (engine.fault.load() && engine.recorder.active.load() && !trackLoad.valid())
            engine.stopRecording();
        if (engine.devicesChanged.exchange(false))
        {
            engine.scan();
            updateDeviceMenus();
        }
        if (engine.latencyProbe.state() == canto::LatencyProbe::State::ready && !latencyAnalysis.valid())
            latencyAnalysis =
                std::async(std::launch::async, [this] { return engine.latencyProbe.analyse(); });
        if (latencyAnalysis.valid() &&
            latencyAnalysis.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try
            {
                const auto result = latencyAnalysis.get();
                notify(result.valid
                           ? tr("Vòng loa → mic: ", "Speaker → mic loop: ") + String(result.milliseconds, 1) +
                                 " ms; correlation " + String(result.correlation, 2) +
                                 tr(". Bao gồm khoảng cách loa–mic. Nghe mic vẫn tắt.",
                                    ". Includes acoustic travel. Monitoring remains off.")
                           : tr("Chưa đo được tin cậy: ", "No reliable measurement: ") +
                                 String(result.reason));
            }
            catch (const std::exception& e)
            {
                engine.latencyProbe.cancel();
                notify(String("Measurement failed: ") + e.what());
            }
        }
        const auto probeState = engine.latencyProbe.state();
        if ((probeState == canto::LatencyProbe::State::capturing ||
             probeState == canto::LatencyProbe::State::requested) &&
            (engine.fault.load() || Time::getMillisecondCounter() - measurementStarted > 5000))
        {
            engine.latencyProbe.cancel();
            notify(tr("Không đủ dữ liệu để đo. Kiểm tra thiết bị.",
                      "No capture data for measurement. Check devices."));
        }
        const bool measuring = engine.latencyProbe.busy() || latencyAnalysis.valid();
        for (auto* c : std::initializer_list<Component*>{&connect, &refresh, &load, &queue, &play, &record})
            c->setEnabled(!measuring && !trackLoad.valid());
        monitor.setEnabled(!measuring);
        test.setEnabled(!measuring);
        measureLatency.setEnabled(!measuring && !trackLoad.valid());
        for (auto* c : std::initializer_list<Component*>{&echo, &reverb, &delay, &feedback, &tone, &threshold,
                                                         &gate, &compressor, &eq, &effects})
            c->setEnabled(!transparent.getToggleState());
        if (trackLoad.valid())
        {
            if (trackLoad.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                return;
            auto error = trackLoad.get();
            notify(error);
            for (auto* c :
                 std::initializer_list<Component*>{&load, &queue, &connect, &refresh, &record, &play})
                c->setEnabled(true);
            if (error.isEmpty())
            {
                position.setRange(0, std::max(1.0, engine.duration), 0.01);
                lines.clear();
                auto lrc = pendingTrack.withFileExtension("lrc");
                if (lrc.existsAsFile())
                    readLyrics(lrc);
            }
        }
        monitor.setColour(TextButton::buttonColourId,
                          engine.params.monitor.load() ? Colour(0xff18735c) : Colour(0xff293547));
        mute.setColour(TextButton::buttonColourId,
                       engine.params.mute.load() ? Colours::red : Colour(0xff81393d));
        auto db = [](float f) { return String(Decibels::gainToDecibels(f, -90.f), 1) + " dBFS"; };
        meters.setText("MIC  " + db(engine.inputPeak) + "       MUSIC  " + db(engine.musicPeak) +
                           "       MASTER  " + db(engine.outputPeak) +
                           (engine.recorder.active.load() ? "      REC ●" : "") +
                           (engine.params.monitor.load() ? "     MONITOR ON" : "     MONITOR OFF"),
                       dontSendNotification);
        meters.setColour(Label::textColourId,
                         engine.inputPeak.load() >= 0.98f || engine.outputPeak.load() >= 0.94f
                             ? Colours::orange
                             : Colours::white);
        if (!position.isMouseButtonDown())
            position.setValue(engine.seconds.load(), dontSendNotification);
        trackLabel.setText(engine.trackName + "   " + String(engine.seconds.load(), 1) + " / " +
                               String(engine.duration, 1) + " s",
                           dontSendNotification);
        if (showAdvanced)
            details.setText(engine.diagnostics(), false);
        String lyric;
        double now = engine.seconds.load() + offset.getValue();
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (lines[i].time < 0)
                lyric += lines[i].text + "\n";
            else if (lines[i].time <= now)
            {
                lyric = lines[i].text;
                if (i + 1 < lines.size())
                    lyric += "\n" + lines[i + 1].text;
            }
        }
        lyrics.setText(
            lyric.isEmpty()
                ? tr("Mở WAV và LRC để hát karaoke\nNhạc YouTube bên ngoài không được thu vào bản mix.",
                     "Open WAV + LRC to sing\nExternal YouTube audio is not included in recordings.")
                : lyric,
            dontSendNotification);
        if (bigLyric)
            bigLyric->setText(lyrics.getText(), dontSendNotification);
        if (engine.fault.load())
            notify(tr("Thiết bị lỗi / mất kết nối. Nghe mic đã tắt; chọn lại thiết bị và Kết nối.",
                      "Device lost/error. Monitoring disabled; select devices and reconnect."));
        if (engine.recorder.failed.load())
            notify(tr("LỖI THU: hàng đợi / ghi đĩa / giới hạn 3 giờ. Nhấn Dừng thu.",
                      "RECORD ERROR: queue / disk / 3-hour limit. Press Stop recording."));
    }
};
class CantoDeckApp final : public JUCEApplication
{
    class Window final : public DocumentWindow
    {
      public:
        explicit Window(bool persist = true) : DocumentWindow("CantoDeck", Colour(0xff141922), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new Console(persist), true);
            setResizable(true, true);
            setResizeLimits(1000, 780, 2400, 1600);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;

  public:
    const String getApplicationName() override { return "CantoDeck"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const String& args) override
    {
        if (args.startsWith("--ui-smoke "))
        {
            window = std::make_unique<Window>(false);
            auto path = args.fromFirstOccurrenceOf("--ui-smoke ", false, false).trim().unquoted();
            Timer::callAfterDelay(
                600,
                [this, path]
                {
                    auto* content = window->getContentComponent();
                    File f = File::getCurrentWorkingDirectory().getChildFile(path);
                    if (f.exists())
                    {
                        setApplicationReturnValue(3);
                        quit();
                        return;
                    }
                    PNGImageFormat png;
                    auto stream = f.createOutputStream();
                    if (!stream || !png.writeImageToStream(
                                       content->createComponentSnapshot(content->getLocalBounds()), *stream))
                        setApplicationReturnValue(4);
                    stream.reset();
                    static_cast<Console*>(content)->smokeAdvanced();
                    auto advancedFile = f.getSiblingFile(f.getFileNameWithoutExtension() + "-advanced.png");
                    if (advancedFile.exists())
                        setApplicationReturnValue(3);
                    else
                    {
                        auto advancedStream = advancedFile.createOutputStream();
                        if (!advancedStream || !png.writeImageToStream(
                                content->createComponentSnapshot(content->getLocalBounds()), *advancedStream))
                            setApplicationReturnValue(4);
                    }
                    if (!static_cast<Console*>(content)->smokeEq(
                            f.getSiblingFile(f.getFileNameWithoutExtension() + "-eq.png")))
                        setApplicationReturnValue(4);
                    quit();
                });
            return;
        }
        if (args.startsWith("--"))
        {
            setApplicationReturnValue(runOffline(args));
            quit();
            return;
        }
        window = std::make_unique<Window>();
    }
    void shutdown() override { window.reset(); }
};
START_JUCE_APPLICATION(CantoDeckApp)
