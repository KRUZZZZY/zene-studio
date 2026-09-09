/*
 * make_gig.cpp - create a minimal but valid .gig instrument file with libgig.
 *
 * One instrument ("Sine440", bank 0 / program 0), one sample (440 Hz sine,
 * 2 s, 16-bit mono 44100 Hz), one region spanning the whole keyboard, one
 * dimension region pointing at the sample.  Built with:
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

int main(int argc, char** argv)
{
    const std::string out = (argc > 1) ? argv[1] : "sine440.gig";
    const uint32_t N = SR * SECONDS;

    try
    {
        gig::File* f = new gig::File();
        f->SetFileName(out);
        f->pInfo->Name      = "GigProof Sine440";
        f->pInfo->Artists   = "gigproof";
        f->pInfo->Software  = std::string("libgig ") + gig::libraryVersion();
        f->pInfo->Comments  = "Minimal one-instrument one-sample .gig for LMMS GigPlayer verification";
        f->pInfo->Copyright = "public domain test tone";

        // ---------------------------------------------------------------- sample
        gig::Sample* s = f->AddSample();
        // File::AddSample() already created an (empty) 'fmt' chunk, so the
        // DLS "default" values do NOT apply -- every format field must be set
        // explicitly, otherwise FormatTag==0 and Resize() throws.
        s->FormatTag             = DLS_WAVE_FORMAT_PCM;
        s->Channels              = 1;
        s->SamplesPerSecond      = SR;
        s->BitDepth              = 16;
        s->FrameSize             = (s->BitDepth / 8) * s->Channels;  // 2 bytes
        s->BlockAlign            = s->FrameSize;
        s->AverageBytesPerSecond = SR * s->FrameSize;
        s->MIDIUnityNote         = 60;
        s->FineTune              = 0;
        s->Loops                 = 0;
        s->pInfo->Name           = "sine440_2s";

        std::vector<int16_t> pcm(N);
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

        // ------------------------------------------------------------ instrument
        gig::Instrument* instr = f->AddInstrument();
        instr->pInfo->Name    = "Sine440";
        instr->MIDIProgram    = 0;
        instr->MIDIBankCoarse = 0;
        instr->MIDIBankFine   = 0;
        instr->MIDIBank       = 0;

        gig::Region* r = instr->AddRegion();     // Region ctor creates dim region 0
        r->SetKeyRange(0, 127);
        r->VelocityRange.low  = 0;
        r->VelocityRange.high = 127;

        gig::DimensionRegion* dr = r->pDimensionRegions[0];
        dr->pSample     = s;
        dr->UnityNote   = 60;
        dr->FineTune    = 0;
        dr->PitchTrack  = true;
        dr->SampleLoops = 0;

        // ------------------------------------------------------------- save 1
        s->Resize(N);   // create/allocate the 'data' chunk
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
