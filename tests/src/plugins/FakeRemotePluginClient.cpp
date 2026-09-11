/*
 * FakeRemotePluginClient.cpp - a hand-written remote plugin client for the
 *                             host<->client contract test
 *                             (tests/src/core/RemotePluginClientE2ETest.cpp)
 *
 * A real remote plugin client (RemoteZynAddSubFx / RemoteVstPlugin) is a
 * separate process that talks to RemotePlugin over a Unix socket and renders
 * into a shared audio block allocated by the host. This fixture speaks the
 * same protocol and attaches the same block, but writes values a test can
 * predict, so the *exact* channel and frame offsets the host reads the
 * client's audio from can be asserted. It renders no DSP; the real clients
 * are driven by the same test where their audio is only checked for liveness.
 *
 * Two modes:
 *
 *   modern  the current (#589) protocol, mirroring RemoteZynAddSubFx's
 *           start-up order: report both channel counts at once
 *           (IdChangeInputOutputCount(0, 2)), ask for the host's buffer size,
 *           attach the shared block the host names, then IdInitDone. For every
 *           IdStartProcessing it writes a deterministic planar pattern -
 *           `plane0[f] = 1000 * period + f`, `plane1[f] = -(1000 * period + f)`
 *           - and answers IdProcessingDone. A wrong plane offset, a wrong
 *           plane stride or a stale plane therefore changes the samples the
 *           host reads, which is exactly what the test pins.
 *
 *   stale   a pre-#589 client: it announces its channel counts with the
 *           retired single-count id (IdChangeInputCount) and writes *sample
 *           frames interleaved* (block[2f], block[2f + 1]), which is what the
 *           retired client interface did. The host must refuse it loudly and
 *           render silence instead of consuming the interleaved writes as
 *           planar planes.
 *
 * Usage: FakeRemotePluginClient <socket-path> [modern|stale]
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

//! Message ids as compiled into both sides (include/RemotePluginBase.h).
enum RemoteMessageId : int
{
	IdUndefined = 0,
	IdHostInfoGotten = 1,
	IdInitDone = 2,
	IdQuit = 3,
	IdSyncKey = 4,
	IdSampleRateInformation = 5,
	IdBufferSizeInformation = 6,
	IdInformationUpdated = 7,
	IdMidiEvent = 8,
	IdStartProcessing = 9,
	IdProcessingDone = 10,
	IdChangeSharedMemoryKey = 11,
	IdChangeInputCount = 12,      //!< retired (#589)
	IdChangeOutputCount = 13,     //!< retired (#589)
	IdChangeInputOutputCount = 14 //!< the only channel-count message there is
};

//! One wire message: id plus string arguments.
struct Message
{
	int id = IdUndefined;
	std::vector<std::string> args;
};

//! Outputs in this client's shared block (RemoteZynAddSubFx reports 0/2).
constexpr std::uint64_t Outputs = 2;

//! Give up if the host goes quiet for this long; keeps a broken test from
//! hanging the suite instead of failing it.
constexpr int ReadTimeoutMs = 30000;

int g_socket = -1;

//! true when emulating the retired, pre-#589 client interface.
bool g_stale = false;

//! Shared audio block the host handed out (data, past SharedMemory's header).
float* g_block = nullptr;
std::uint64_t g_blockFloats = 0;

//! Frame count the host reported. This is the client's plane stride, and the
//! value the host lays its own planes out with; the test asserts agreement.
std::uint64_t g_frames = 0;

//! IdStartProcessing counter, so every period writes distinguishable samples.
int g_period = 0;

//! @return true if the whole buffer was written
auto writeAll(const void* buffer, std::size_t size) -> bool
{
	const auto* bytes = static_cast<const char*>(buffer);
	while (size > 0)
	{
		const ssize_t written = ::write(g_socket, bytes, size);
		if (written <= 0)
		{
			if (written < 0 && errno == EINTR) { continue; }
			return false;
		}
		bytes += written;
		size -= static_cast<std::size_t>(written);
	}
	return true;
}

//! @return false on EOF (the host went away) or error
auto readAll(void* buffer, std::size_t size) -> bool
{
	auto* bytes = static_cast<char*>(buffer);
	while (size > 0)
	{
		const ssize_t read = ::read(g_socket, bytes, size);
		if (read == 0) { return false; }
		if (read < 0)
		{
			if (errno == EINTR) { continue; }
			return false;
		}
		bytes += read;
		size -= static_cast<std::size_t>(read);
	}
	return true;
}

auto writeInt32(std::int32_t value) -> bool { return writeAll(&value, sizeof(value)); }
auto readInt32(std::int32_t& value) -> bool { return readAll(&value, sizeof(value)); }

auto writeString(const std::string& value) -> bool
{
	const auto size = static_cast<std::int32_t>(value.size());
	return writeInt32(size) && (size == 0 || writeAll(value.data(), value.size()));
}

auto readString(std::string& value) -> bool
{
	std::int32_t size = 0;
	if (!readInt32(size) || size < 0) { return false; }
	value.resize(static_cast<std::size_t>(size));
	return size == 0 || readAll(value.data(), static_cast<std::size_t>(size));
}

//! Sends one message in the framing RemotePluginBase::sendMessage() writes.
auto sendMessage(int id, const std::vector<std::string>& args) -> bool
{
	if (!writeInt32(id)) { return false; }
	if (!writeInt32(static_cast<std::int32_t>(args.size()))) { return false; }
	for (const auto& arg : args)
	{
		if (!writeString(arg)) { return false; }
	}
	std::fprintf(stderr, "fakeclient: -> id %d (%zu args)\n", id, args.size());
	return true;
}

//! Receives one message, waiting at most ReadTimeoutMs. @return false when the
//! host went away or went quiet; *timedOut distinguishes the two.
auto receiveMessage(Message& message, bool& timedOut) -> bool
{
	timedOut = false;

	pollfd descriptor{};
	descriptor.fd = g_socket;
	descriptor.events = POLLIN;
	const int ready = ::poll(&descriptor, 1, ReadTimeoutMs);
	if (ready == 0)
	{
		timedOut = true;
		return false;
	}
	if (ready < 0 || (descriptor.revents & POLLIN) == 0) { return false; }

	std::int32_t id = 0;
	if (!readInt32(id)) { return false; }
	std::int32_t count = 0;
	if (!readInt32(count) || count < 0) { return false; }

	message.id = id;
	message.args.clear();
	for (std::int32_t i = 0; i < count; ++i)
	{
		std::string arg;
		if (!readString(arg)) { return false; }
		message.args.push_back(std::move(arg));
	}
	return true;
}

//! Emulates RemotePluginClient::setShmKey(): attaches the block the host named.
//! SharedMemory's layout starts with a header holding the data size in bytes.
void attachSharedBlock(const std::string& key)
{
	if (g_block != nullptr) { return; }

	const std::string name = "/" + key;
	const int fd = ::shm_open(name.c_str(), O_RDWR, 0);
	if (fd == -1)
	{
		std::perror("fakeclient: shm_open");
		std::exit(3);
	}

	struct stat status{};
	if (::fstat(fd, &status) == -1)
	{
		std::perror("fakeclient: fstat");
		std::exit(3);
	}

	void* mapping = ::mmap(nullptr, static_cast<std::size_t>(status.st_size),
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED)
	{
		std::perror("fakeclient: mmap");
		std::exit(3);
	}

	const auto sizeBytes = *static_cast<const std::uint64_t*>(mapping);
	g_block = reinterpret_cast<float*>(static_cast<char*>(mapping) + sizeof(std::uint64_t));
	g_blockFloats = sizeBytes / sizeof(float);
	std::fprintf(stderr, "fakeclient: attached '%s' (%llu floats)\n", name.c_str(),
		static_cast<unsigned long long>(g_blockFloats));
}

//! Writes one period of the deterministic *planar* pattern the test expects.
void writePlanarPeriod()
{
	if (g_block == nullptr || g_frames == 0)
	{
		std::fprintf(stderr, "fakeclient: no shared block/frame count to write with\n");
		std::exit(3);
	}

	const auto frames = g_frames;
	// A matching host allocated (0 inputs + 2 outputs) * frames floats; the
	// second plane starts exactly `frames` floats past the first.
	if (g_blockFloats != Outputs * frames)
	{
		std::fprintf(stderr,
			"fakeclient: host block is %llu floats, expected %llu (2 planes x %llu frames)\n",
			static_cast<unsigned long long>(g_blockFloats),
			static_cast<unsigned long long>(Outputs * frames),
			static_cast<unsigned long long>(frames));
		std::exit(3);
	}

	++g_period;
	for (std::uint64_t frame = 0; frame < frames; ++frame)
	{
		const float value = 1000.0f * static_cast<float>(g_period) + static_cast<float>(frame);
		g_block[frame] = value;
		g_block[frames + frame] = -value;
	}
	std::fprintf(stderr, "fakeclient: wrote period %d (%llu planar frames per channel)\n",
		g_period, static_cast<unsigned long long>(frames));
}

//! Emulates the retired client interface: sample frames written interleaved
//! into the same block (block[2f] / block[2f + 1]).
void writeInterleavedPeriod()
{
	if (g_block == nullptr)
	{
		std::fprintf(stderr, "fakeclient: no shared block to write into\n");
		return;
	}

	const std::uint64_t frames = g_blockFloats / 2;
	for (std::uint64_t frame = 0; frame < frames; ++frame)
	{
		g_block[2 * frame] = 1.0f + static_cast<float>(frame);
		g_block[2 * frame + 1] = -(1.0f + static_cast<float>(frame));
	}
	std::fprintf(stderr, "fakeclient: wrote %llu interleaved sample frames\n",
		static_cast<unsigned long long>(frames));
}

//! Handles the messages both modes can receive. @return false to quit.
auto handleMessage(const Message& message) -> bool
{
	switch (message.id)
	{
	case IdQuit:
		return false;

	case IdChangeSharedMemoryKey:
		if (!message.args.empty()) { attachSharedBlock(message.args[0]); }
		break;

	case IdBufferSizeInformation:
		// The host sends (and replies with) its frame count. This is the value
		// a client lays its planes out with (RemotePluginClient::bufferSize()).
		if (!message.args.empty())
		{
			g_frames = std::strtoull(message.args[0].c_str(), nullptr, 10);
		}
		break;

	case IdStartProcessing:
		if (g_stale) { writeInterleavedPeriod(); }
		else { writePlanarPeriod(); }
		if (!sendMessage(IdProcessingDone, {})) { return false; }
		break;

	default:
		// IdSyncKey, IdMidiEvent, IdSampleRateInformation and the Zyn-specific
		// messages need no answer from this fixture.
		break;
	}
	return true;
}

//! Waits for one message and handles it. @return false on EOF/error/timeout.
auto serviceOneMessage() -> bool
{
	Message message;
	bool timedOut = false;
	if (!receiveMessage(message, timedOut))
	{
		if (timedOut)
		{
			std::fprintf(stderr, "fakeclient: host went quiet for %d ms, exiting\n", ReadTimeoutMs);
		}
		return false;
	}
	std::fprintf(stderr, "fakeclient: <- id %d\n", message.id);
	return handleMessage(message);
}

} // namespace

namespace
{

//! Connects to the host's socket; exits the process on failure.
auto connectToHost(const char* path) -> int
{
	const int socket = ::socket(AF_LOCAL, SOCK_STREAM, 0);
	if (socket == -1)
	{
		std::perror("fakeclient: socket");
		std::exit(1);
	}

	sockaddr_un address{};
	address.sun_family = AF_LOCAL;
	std::strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
	if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
	{
		std::perror("fakeclient: connect");
		std::exit(1);
	}
	return socket;
}

//! Sends the client's start-up message sequence.
auto startHandshake() -> bool
{
	if (g_stale)
	{
		// The pre-#589 client interface announced one channel count at a time.
		return sendMessage(IdChangeInputCount, {"0"}) && sendMessage(IdInitDone, {});
	}

	if (!sendMessage(IdChangeInputOutputCount, {"0", "2"})) { return false; }
	// Ask for the host's frame count; it answers with IdBufferSizeInformation.
	if (!sendMessage(IdBufferSizeInformation, {})) { return false; }
	// IdInitDone is the last start-up message. The host answers it and then
	// activates its audio ports, which allocates the shared block and names it
	// (IdChangeSharedMemoryKey) - so the key arrives after that reply, exactly
	// as it does for the real clients.
	return sendMessage(IdInitDone, {});
}

//! Handles messages until the host's IdInitDone reply ends the handshake.
auto waitForHandshakeReply() -> bool
{
	for (;;)
	{
		Message message;
		bool timedOut = false;
		if (!receiveMessage(message, timedOut))
		{
			std::fprintf(stderr, "fakeclient: handshake did not complete\n");
			return false;
		}
		std::fprintf(stderr, "fakeclient: <- id %d\n", message.id);
		if (message.id == IdInitDone) { return true; }
		if (!handleMessage(message)) { return false; }
	}
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: FakeRemotePluginClient <socket-path> [modern|stale]\n");
		return 1;
	}
	const std::string mode = argc > 2 ? argv[2] : "modern";
	g_stale = mode == "stale";
	if (mode != "modern" && mode != "stale")
	{
		std::fprintf(stderr, "fakeclient: unknown mode '%s'\n", mode.c_str());
		return 1;
	}

	g_socket = connectToHost(argv[1]);
	std::fprintf(stderr, "fakeclient: connected (%s client)\n", mode.c_str());

	if (!startHandshake()) { return 2; }
	if (!waitForHandshakeReply()) { return 2; }
	std::fprintf(stderr, "fakeclient: handshake complete\n");

	while (serviceOneMessage())
	{
	}

	std::fprintf(stderr, "fakeclient: exiting\n");
	return 0;
}
