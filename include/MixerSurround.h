/*
 * MixerSurround.h - surround mixer channels: their formats, the surround panner, the downmix to stereo
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_MIXER_SURROUND_H
#define LMMS_MIXER_SURROUND_H

#include <array>
#include <vector>

#include <QString>
#include <QStringList>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class HardwareOutputBus;
class Mixer;
class MixerChannel;
class MixerRoute;
class SampleFrame;

/*! 0.5.0 F7 (MX-060): surround channels, opted into one mixer channel at a time.
 *
 *  The engine's audio path stays stereo: a channel's buffer, its devices, its meters, the sends' PDC
 *  delay lines, the listen bus, the cue mixes and the recorder all carry L/R. A channel set to a surround
 *  format keeps its other speakers (centre, LFE, the surrounds) in a SIDE buffer next to its stereo one,
 *  allocated once with the channel for the largest layout (8 channels), so changing the format never
 *  allocates. Its devices process L/R only; the side channels pass by them with the channel's trim, VCA
 *  gain, its pan as a balance on each left/right pair, its fader and mute.
 *
 *  - Formats: stereo (the default), LCR, quad, 5.1, 7.1, in the WAVE channel order
 *    (L R C LFE Ls Rs; 7.1: L R C LFE Lrs Rrs Ls Rs), mono (one M role) and discrete-2..discrete-8,
 *    whose channels have no speaker roles: channel i is routed to output i by index, never folded or
 *    reordered by speaker. A discrete channel name carries its count ("discrete-5"), so a save and a
 *    load keep both the layout and how many channels it has.
 *  - The panner: a stereo channel sending into a surround channel can pan to a point of the room (x, y in
 *    -1..1, front is y = 1) with an LFE send and a spread, in place of its stereo pan. Constant-power
 *    pairwise panning between the two speakers either side of the point's direction; the spread, and a
 *    point nearer the middle of the room, blend toward every speaker at once, the power kept constant.
 *    The source is the channel's L/R summed to mono.
 *  - Between formats: a speaker the receiver has is carried to it; one it lacks folds into the ones it
 *    has with ITU-R BS.775 coefficients - the centre into L and R at -3 dB, a surround into its side's
 *    front speaker at -3 dB, 7.1's rear surrounds into 5.1's surrounds, the LFE dropped unless the
 *    downmix options keep it. A surround channel sending into a stereo one (the master, usually)
 *    is downmixed that way, and so is a surround master whenever its outputs are not all on the device,
 *    and in a stereo export (the default). A surround export writes the master's channels as they are
 *    (include/SurroundExport.h, docs/SURROUND-EXPORT.md).
 *  - Hardware: a surround channel's direct out (ChannelHardwareIo) names its FIRST output; it takes as
 *    many consecutive outputs as it has channels. The master's side channels go after its pair. A device
 *    with fewer outputs gets the stereo downmix on the pair instead.
 *
 *  Threads: the settings and the gain tables are written on the control thread under the engine's
 *  change lock (the commands, a project load); the render thread and the mixer workers only read them.
 *  Each channel's side buffer is written by that channel's own worker, and read by its receivers after it
 *  (the dependency count orders them, as for the stereo buffer) and by the render thread at the end of
 *  the master mix. Nothing here allocates or locks on the audio path. */
namespace surround
{

//! The largest layout's channels, and those beyond the stereo pair a side buffer holds.
constexpr int MaxChannels = 8;
constexpr int SideChannels = MaxChannels - 2;

enum class Format : int
{
	Stereo = 0,
	Lcr,
	Quad,
	Surround51,
	Surround71,
	Mono,
	Discrete2,
	Discrete3,
	Discrete4,
	Discrete5,
	Discrete6,
	Discrete7,
	Discrete8
};
constexpr int FormatCount = 13;

//! The discrete layouts' count range: Mono is the one-channel layout, Discrete2..Discrete8 the rest.
constexpr int MinDiscreteChannels = 2;
constexpr int MaxDiscreteChannels = MaxChannels;

//! Where a speaker sits: its role (what a downmix knows it by) and its point in the panner's square room.
//! M is mono's single role; None is a role-less discrete channel.
enum class Role : int
{
	L, R, C, Lfe, Ls, Rs, Lrs, Rrs, M, None
};

//! A channel's panner, as a sender into a surround channel.
struct PanPosition
{
	bool enabled = false;  //!< off: the channel's L/R go to the receiver's L/R, as into a stereo channel
	float x = 0.f;         //!< -1 left .. 1 right
	float y = 1.f;         //!< -1 back .. 1 front
	float lfe = 0.f;       //!< 0..1, the share sent to the LFE
	float spread = 0.f;    //!< 0 a point .. 1 every speaker alike
	bool operator==(const PanPosition&) const = default;
};

//! How a surround signal folds down. Linear gains; the LFE is dropped unless @a lfeOn.
struct DownmixOptions
{
	float centre = 0.70710678f;    //!< -3 dB (ITU-R BS.775)
	float surround = 0.70710678f;  //!< -3 dB
	bool lfeOn = false;
	float lfe = 1.f;
	bool operator==(const DownmixOptions&) const = default;
};

//! One receiver channel from each sender channel: [to][from].
using Matrix = std::array<std::array<float, MaxChannels>, MaxChannels>;
using Gains = std::array<float, MaxChannels>;

LMMS_EXPORT int channelCount(Format format);
LMMS_EXPORT Role roleOf(Format format, int channel);
//! The channel of @a role in @a format, or -1.
LMMS_EXPORT int channelOf(Format format, Role role);
LMMS_EXPORT QString formatName(Format format);
LMMS_EXPORT bool formatFromName(const QString& name, Format* format);
LMMS_EXPORT QStringList formatNames();
//! Whether @a format is one of the role-less discrete layouts (Discrete2..Discrete8).
LMMS_EXPORT bool isDiscrete(Format format);
//! @a format's discrete channel count (2..8), or -1 when it is not discrete.
LMMS_EXPORT int discreteChannels(Format format);
//! The discrete layout with @a channels (2..8), or Stereo when the count is outside the range.
LMMS_EXPORT Format discreteFormat(int channels);
//! "L", "R", "C", "LFE", "Ls", ...: @a format's speakers in channel order. Empty for a discrete layout:
//! its channels have no speaker roles.
LMMS_EXPORT QStringList speakerNames(Format format);
//! The channel-address labels a reader gets: speaker names, "M" for mono, or "1".."N" for discrete.
LMMS_EXPORT QStringList channelLabels(Format format);
//! A speaker's point in the panner's room (the LFE has none: 0, 0; a discrete channel has no position).
LMMS_EXPORT void speakerPoint(Format format, int channel, float* x, float* y);

//! The panner's gains for @a pan over @a format's channels (unit power over the speakers, plus the LFE send).
LMMS_EXPORT Gains panGains(Format format, const PanPosition& pan);
//! The mix from @a from into @a to with the current downmix options (built when they change).
LMMS_EXPORT const Matrix& matrix(Format from, Format to);

LMMS_EXPORT const DownmixOptions& downmixOptions();
//! Control thread, under the change lock: the options, and every matrix rebuilt from them.
LMMS_EXPORT void setDownmixOptions(const DownmixOptions& options);

} // namespace surround

/*! A mixer channel's surround side (a member of MixerChannel): its format, its panner and its side buffer. */
struct LMMS_EXPORT ChannelSurround
{
	//! Control thread: the side buffer for periods of @a frames, cleared (MixerChannel's constructor).
	void init(f_cnt_t frames);

	surround::Format format() const { return m_format; }
	bool isSurround() const { return m_format != surround::Format::Stereo; }
	//! Control thread, under the change lock. The side buffer is cleared.
	void setFormat(surround::Format format);

	const surround::PanPosition& pan() const { return m_pan; }
	//! Control thread, under the change lock: the panner and its gains for every format.
	void setPan(const surround::PanPosition& pan);
	//! The panner's gains into a receiver of @a format.
	const surround::Gains& gains(surround::Format format) const { return m_gains[static_cast<std::size_t>(format)]; }

	//! Side channel @a channel (2..7) of the period: frames() floats.
	float* plane(int channel) { return m_side.data() + static_cast<std::size_t>(channel - 2) * m_frames; }
	const float* plane(int channel) const { return m_side.data() + static_cast<std::size_t>(channel - 2) * m_frames; }
	f_cnt_t frames() const { return m_frames; }
	//! Whether the side buffer holds anything this period (its owner wrote it).
	bool written() const { return m_written; }
	void markWritten() { m_written = true; }
	//! Render thread, end of the master mix: the side buffer silenced, if anything was written.
	void clearPeriod() noexcept;

	//! A <surround> element under the channel, only while the format or the panner is not the default.
	void save(QDomDocument& doc, QDomElement& channelElement) const;
	//! Read it back; absent: stereo, panner off.
	void load(const QDomElement& channelElement);
	//! Stereo, panner off (a cleared channel).
	void reset();

private:
	surround::Format m_format = surround::Format::Stereo;
	surround::PanPosition m_pan;
	std::array<surround::Gains, surround::FormatCount> m_gains{};
	std::vector<float> m_side;
	f_cnt_t m_frames = 0;
	bool m_written = false;
};

namespace surround
{

// ---- The render path (MixerSurroundMix.cpp). Each is a no-op for stereo channels. ----

//! Whether @a route's sender pans into its receiver with the surround panner (MixerSendPan.cpp asks):
//! a stereo sender with its panner on, into a surround receiver.
LMMS_EXPORT bool panTakesSend(const MixerRoute& route);
//! The receiver's worker: @a src (the sender's block) panned into the receiver's speakers, at the send's
//! level and the sender's fader, sample-exact where those are.
LMMS_EXPORT void panSend(MixerRoute& route, const SampleFrame* src, f_cnt_t frames);
//! The receiver's worker, after a send's stereo part: a surround sender's side channels into the
//! receiver - carried or folded (matrix()). Not for feedback sends (their snapshot is stereo).
LMMS_EXPORT void addSideChannels(MixerRoute& route, f_cnt_t frames);
//! The channel's worker, after its pan stage: trim, VCA gain and the pan's balance on the side channels.
LMMS_EXPORT void processChannel(MixerChannel& channel, f_cnt_t frames);
//! The render thread, after the master's fader: a surround master's side channels onto the hardware
//! outputs after its pair, or folded into @a out when they cannot all play (and in an export).
LMMS_EXPORT void masterOutput(Mixer& mixer, SampleFrame* out, f_cnt_t frames);
//! The render thread (hardwareio::mixOutputs): a surround channel's direct out at output @a start - its
//! side channels after the pair on @a bus, or folded into @a stereo (the pair's signal, after the fader)
//! when the device lacks the outputs or the route is mono.
LMMS_EXPORT void directOut(HardwareOutputBus& bus, MixerChannel& channel, int start, int width, SampleFrame* stereo,
	f_cnt_t frames);
//! Whether outputs @a start .. @a start + @a count - 1 are all on the running device.
LMMS_EXPORT bool deviceCarries(int start, int count);

//! The downmix options under the mixer's element, only while they are not the defaults.
LMMS_EXPORT void saveMixer(QDomDocument& doc, QDomElement& mixerElement);
LMMS_EXPORT void loadMixer(const QDomElement& mixerElement);
//! A new or cleared project: the default downmix.
LMMS_EXPORT void resetMixer();

} // namespace surround

} // namespace lmms

#endif // LMMS_MIXER_SURROUND_H
