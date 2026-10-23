/*
 * make_gig.cpp - create a minimal but valid .gig instrument file with libgig.
 *
 * By default, create one instrument ("Sine440", bank 0 / program 0) with two
 * samples (a 440 Hz sine and a constant release probe, both 2 s, 16-bit mono
 * 44100 Hz) and two adjacent key regions: the first has a release-trigger
 * dimension whose zones point at those samples; the second has no dimensions
 * and does not cover the test key. Passing --keyboard creates a two-zone
 * keyboard-dimension fixture, --one-frame-release creates a silent note with
 * a single non-zero release frame, --layers N creates N regions that all match
 * the same key (so one note has N matching samples), --dc replaces the sine
 * with a constant level (so the sum of any number of voices is exactly
 * proportional to the number of voices, whatever period each started in), and
 * --pingpong / --single-frame-loop create directional loop fixtures used by
 * the regression tests.
 * Built with:
 *   g++ -std=c++17 make_gig.cpp -o make_gig $(pkg-config --cflags --libs gig)
 *
 * Save order follows the libgig docs for File::AddSample():
 *   set attributes -> Resize() -> Save() -> Write() -> Save()
 * (the second Save persists the CRC-32 table that Sample::Write() updated).
 */
#include <gig.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static const uint32_t SR      = 44100;  // sample rate of the generated sample
static const uint32_t SECONDS = 2;
static const double   FREQ    = 440.0;
static const int16_t RELEASE_FRAME = 16384;

static void configureSample(gig::Sample* sample, const std::string& name)
{
    sample->FormatTag             = DLS_WAVE_FORMAT_PCM;
    sample->Channels              = 1;
    sample->SamplesPerSecond      = SR;
    sample->BitDepth              = 16;
    sample->FrameSize             = (sample->BitDepth / 8) * sample->Channels;
    sample->BlockAlign            = sample->FrameSize;
    sample->AverageBytesPerSecond = SR * sample->FrameSize;
    sample->MIDIUnityNote         = 60;
    sample->FineTune              = 0;
    sample->Loops                 = 0;
    sample->pInfo->Name           = name;
}

int main(int argc, char** argv)
{
    bool keyboardDimension = false;
    bool singleFrameLoop = false;
    bool pingPong = false;
    bool oneFrameRelease = false;
    uint32_t layers = 0;
    bool dc = false;
    std::string out;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--keyboard") keyboardDimension = true;
        else if (arg == "--pingpong") pingPong = true;
        else if (arg == "--single-frame-loop") { singleFrameLoop = true; pingPong = true; }
        else if (arg == "--one-frame-release") oneFrameRelease = true;
        else if (arg == "--layers" && i + 1 < argc) layers = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (arg == "--dc") dc = true;
        else if (out.empty()) out = arg;
    }
    if (keyboardDimension) pingPong = false;
    if (oneFrameRelease) { pingPong = false; keyboardDimension = false; }
    if (layers > 0) { pingPong = false; keyboardDimension = false; oneFrameRelease = false; }
    if (dc) { pingPong = false; keyboardDimension = false; oneFrameRelease = false; }
    const bool releaseTrigger = !pingPong && !keyboardDimension && layers == 0 && !dc;
    if (out.empty())
    {
        if (dc && layers > 0) out = "layered-dc.gig";
        else if (dc) out = "dc.gig";
        else if (layers > 0) out = "layered.gig";
        else if (keyboardDimension) out = "keyboard-dimension.gig";
        else if (singleFrameLoop) out = "single-frame-loop.gig";
        else if (pingPong) out = "pingpong.gig";
        else if (oneFrameRelease) out = "one-frame-release.gig";
        else out = "sine440.gig";
    }
    constexpr uint32_t LoopStart = 64;
    const uint32_t loopLength = singleFrameLoop ? 1 : 64;
    // The layered fixture only needs enough frames to measure a level, and it
    // has one sample per layer, so it is kept short to keep the file small.
    const uint32_t N = oneFrameRelease ? SR
        : (layers > 0 ? SR / 4 : (pingPong ? SR / 4 : SR * SECONDS));

    try
    {
        // File::SetFileName() makes the first Save() below dereference an
        // uninitialised chunk list in libgig 4.6.0; Save(path) alone sets the
        // file name and writes the whole file, so it is not used here.
        gig::File* f = new gig::File();
        f->pInfo->Name      = dc ? "GigProof DC" : (layers > 0 ? "GigProof Layered"
            : (pingPong ? "GigProof PingPong" : (keyboardDimension ? "GigProof KeyboardDimension" : "GigProof Sine440")));
        f->pInfo->Artists   = "gigproof";
        f->pInfo->Software  = std::string("libgig ") + gig::libraryVersion();
        f->pInfo->Comments  = layers > 0
            ? "Layered fixture: every region matches the same key, so one note has several matching samples"
            : (oneFrameRelease
            ? "GigPlayer regression: silent note and one-frame release trigger"
            : (pingPong
                ? "Bidirectional loop fixture for LMMS GigPlayer verification"
                : (keyboardDimension ? "Keyboard dimension regression fixture for LMMS GigPlayer verification"
                                     : "GigPlayer release-trigger regression fixture")));
        f->pInfo->Copyright = "public domain test tone";

        // ---------------------------------------------------------------- sample
        gig::Sample* s = f->AddSample();
        configureSample(s, pingPong ? "pingpong_loop" : (keyboardDimension ? "zone0_sine440"
                             : (oneFrameRelease ? "silent_note_1s" : "sine440_2s")));
        gig::Sample* silentSample = nullptr;
        if (keyboardDimension)
        {
            silentSample = f->AddSample();
            configureSample(silentSample, "zone1_silence");
        }
        std::vector<gig::Sample*> layerSamples;
        for (uint32_t layer = 1; layer < layers; ++layer)
        {
            gig::Sample* layerSample = f->AddSample();
            configureSample(layerSample, "layer" + std::to_string(layer));
            layerSamples.push_back(layerSample);
        }
        gig::Sample* releaseSample = nullptr;
        std::vector<int16_t> releasePcm;
        if (releaseTrigger)
        {
            releaseSample = f->AddSample();
            configureSample(releaseSample, oneFrameRelease ? "release_1frame" : "release_probe_2s");
        }

        std::vector<int16_t> pcm(N);
        if (pingPong)
        {
            for (uint32_t i = 0; i < loopLength; ++i)
            {
                if (singleFrameLoop)
                {
                    pcm[LoopStart + i] = -20000;
                }
                else
                {
                    const int16_t ramp = static_cast<int16_t>(4000 + 300 * i);
                    pcm[LoopStart + i] = i < loopLength / 2 || i == loopLength - 1
                        ? static_cast<int16_t>(-ramp)
                        : ramp;
                }
            }
            if (singleFrameLoop)
            {
                for (uint32_t i = LoopStart + loopLength; i < N; ++i)
                {
                    pcm[i] = static_cast<int16_t>(3000 + (i % 100) * 100);
                }
            }
            else
            {
                pcm[LoopStart + loopLength] = -25000;
            }
        }
        else if (dc)
        {
            // A constant level: the sum of N playing voices is N times the
            // level of one voice, in every period and at every phase.
            pcm.assign(N, static_cast<int16_t>(8192));
        }
        else if (!oneFrameRelease)
        {
            const uint32_t fadeIn  = SR / 200;  // 5 ms
            const uint32_t fadeOut = SR / 20;   // 50 ms
            for (uint32_t i = 0; i < N; ++i)
            {
                double env = 1.0;
                if (i < fadeIn)                 env = double(i) / fadeIn;
                else if (i >= N - fadeOut)      env = double(N - 1 - i) / fadeOut;
                pcm[i] = int16_t(std::lround(
                    0.5 * 32767.0 * env * std::sin(2.0 * M_PI * FREQ * double(i) / SR)));
            }
            if (releaseTrigger)
            {
                releasePcm.assign(N, 8192);
            }
        }
        else
        {
            releasePcm.assign(1, RELEASE_FRAME);
        }

        // ------------------------------------------------------------ instrument
        gig::Instrument* instr = f->AddInstrument();
        instr->pInfo->Name    = layers > 0 ? "Layered"
            : (pingPong ? "PingPong" : (keyboardDimension ? "KeyboardDimension" : "Sine440"));
        instr->MIDIProgram    = 0;
        instr->MIDIBankCoarse = 0;
        instr->MIDIBankFine   = 0;
        instr->MIDIBank       = 0;
        if (keyboardDimension)
        {
            instr->DimensionKeyRange.low = 48;
            instr->DimensionKeyRange.high = 80;
        }

        if (layers > 0)
        {
            // Every region covers the same key and velocity, so a note that
            // matches one of them matches every one of them. Each region gets
            // its own sample, so dropping one of them is audible as a level
            // change rather than as a silent layer.
            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                gig::Sample* layerSample = layer == 0 ? s : layerSamples[layer - 1];
                gig::Region* r = instr->AddRegion();
                r->SetKeyRange(0, 127);
                r->VelocityRange.low  = 0;
                r->VelocityRange.high = 127;
                gig::DimensionRegion* dr = r->pDimensionRegions[0];
                dr->pSample     = layerSample;
                dr->UnityNote   = 60;
                dr->FineTune    = 0;
                dr->PitchTrack  = true;
                dr->SampleLoops = 0;
            }
        }
        else if (releaseTrigger)
        {
            gig::Region* r = instr->AddRegion();
            r->SetKeyRange(0, 100);
            r->VelocityRange.low  = 0;
            r->VelocityRange.high = 127;

            gig::dimension_def_t releaseDimension;
            releaseDimension.dimension = gig::dimension_releasetrigger;
            releaseDimension.bits = 1;
            releaseDimension.zones = 2;
            releaseDimension.split_type = gig::split_type_bit;
            releaseDimension.zone_size = 0.0f;
            r->AddDimension(&releaseDimension);

            uint dimensionValues[8] = {};
            for (uint releaseZone = 0; releaseZone < 2; ++releaseZone)
            {
                dimensionValues[0] = releaseZone;
                gig::DimensionRegion* dr = r->GetDimensionRegionByValue(dimensionValues);
                dr->pSample     = releaseZone == 0 ? s : releaseSample;
                dr->UnityNote   = 60;
                dr->FineTune    = 0;
                dr->PitchTrack  = oneFrameRelease && releaseZone == 1 ? false : true;
                dr->SampleLoops = 0;
                if (releaseZone == 1)
                {
                    dr->ReleaseTriggerDecay = 8;
                }
            }

            // This later, non-release region deliberately does not cover MIDI key
            // 60. Its dimension state still follows the first region during note
            // setup, exposing accidental replacement of the release flag.
            gig::Region* laterRegion = instr->AddRegion();
            laterRegion->SetKeyRange(101, 127);
            laterRegion->VelocityRange.low = 0;
            laterRegion->VelocityRange.high = 127;
            laterRegion->pDimensionRegions[0]->pSample = s;
            laterRegion->pDimensionRegions[0]->UnityNote = 60;
            laterRegion->pDimensionRegions[0]->FineTune = 0;
            laterRegion->pDimensionRegions[0]->PitchTrack = true;
            laterRegion->pDimensionRegions[0]->SampleLoops = 0;
        }
        else
        {
            gig::Region* r = instr->AddRegion();     // Region ctor creates dim region 0
            r->SetKeyRange(0, 127);
            r->VelocityRange.low  = 0;
            r->VelocityRange.high = 127;

            if (keyboardDimension)
            {
                gig::dimension_def_t dimension{
                    gig::dimension_keyboard, 1, 2, gig::split_type_normal, 64.0f};
                r->AddDimension(&dimension);
            }
            for (uint32_t i = 0; i < r->DimensionRegions; ++i)
            {
                gig::DimensionRegion* dr = r->pDimensionRegions[i];
                dr->pSample     = (keyboardDimension && i == 1) ? silentSample : s;
                dr->UnityNote   = 60;
                dr->FineTune    = 0;
                dr->PitchTrack  = !pingPong;
                dr->SampleLoops = 0;
                if (pingPong)
                {
                    dr->EG1Sustain = 1000;
                    DLS::sample_loop_t loop{};
                    loop.Size = sizeof(loop);
                    loop.LoopType = gig::loop_type_bidirectional;
                    loop.LoopStart = LoopStart;
                    loop.LoopLength = loopLength;
                    dr->AddSampleLoop(&loop);
                }
            }
        }

        // ------------------------------------------------------------- save 1
        s->Resize(N);   // create/allocate the 'data' chunk
        for (gig::Sample* layerSample : layerSamples)
        {
            layerSample->Resize(N);
        }
        if (silentSample != nullptr)
        {
            silentSample->Resize(N);
        }
        if (releaseSample != nullptr)
        {
            releaseSample->Resize(oneFrameRelease ? 1 : N);
        }
        f->Save(out);   // write file skeleton, reserve sample data space

        // ------------------------------------------------------------- write
        s->SetPos(0);
        const gig::file_offset_t written = s->Write(pcm.data(), N);
        if (written != gig::file_offset_t(N))
        {
            fprintf(stderr, "ERROR: wrote %lld of %u sample points\n",
                    (long long) written, (unsigned) N);
            return 1;
        }
        for (gig::Sample* layerSample : layerSamples)
        {
            layerSample->SetPos(0);
            const gig::file_offset_t layerWritten = layerSample->Write(pcm.data(), N);
            if (layerWritten != gig::file_offset_t(N))
            {
                fprintf(stderr, "ERROR: wrote %lld of %u layer sample points\n",
                        (long long) layerWritten, (unsigned) N);
                return 1;
            }
        }
        if (silentSample != nullptr)
        {
            std::vector<int16_t> silence(N, 0);
            silentSample->SetPos(0);
            const gig::file_offset_t silentWritten = silentSample->Write(silence.data(), N);
            if (silentWritten != gig::file_offset_t(N))
            {
                fprintf(stderr, "ERROR: wrote %lld of %u silent sample points\n",
                        (long long) silentWritten, (unsigned) N);
                return 1;
            }
        }
        if (releaseSample != nullptr)
        {
            releaseSample->SetPos(0);
            const auto releaseFrames = static_cast<uint32_t>(releasePcm.size());
            const gig::file_offset_t releaseWritten = releaseSample->Write(releasePcm.data(), releaseFrames);
            if (releaseWritten != gig::file_offset_t(releaseFrames))
            {
                fprintf(stderr, "ERROR: wrote %lld of %u release sample points\n",
                        (long long) releaseWritten, (unsigned) releaseFrames);
                return 1;
            }
        }

        // ------------------------------------------------------------- save 2
        f->Save(out);   // persist chunk headers, file offsets and CRC-32 table

        printf("wrote %s (%u frames, %u Hz, 16-bit mono, %u samples in file)\n",
               out.c_str(), (unsigned) N, (unsigned) SR, (unsigned) f->CountSamples());
        printf("instruments in file: %u\n", (unsigned) f->CountInstruments());
        delete f;
        return 0;
    }
    catch (gig::Exception& e)
    {
        e.PrintMessage();
        return 1;
    }
    catch (RIFF::Exception& e)
    {
        e.PrintMessage();
        return 1;
    }
}
