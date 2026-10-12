// Shared preset iteration policy for the SF2 patch list.
#pragma once

namespace lmms::sf2
{

template <typename SoundFontAt, typename ForEachPreset, typename BankFor,
		  typename ProgramFor, typename HasProgram, typename AddPreset>
void forEachUniquePresetForBank(int soundFontCount, int selectedBank,
								SoundFontAt soundFontAt, ForEachPreset forEachPreset,
								BankFor bankFor, ProgramFor programFor,
								HasProgram hasProgram, AddPreset addPreset)
{
	for (int soundFontIndex = 0; soundFontIndex < soundFontCount; ++soundFontIndex)
	{
		auto* soundFont = soundFontAt(soundFontIndex);
		if (soundFont == nullptr)
		{
			continue;
		}

		forEachPreset(soundFont, [&](auto&& preset)
		{
			const int program = programFor(preset);
			if (bankFor(soundFont, preset) == selectedBank && !hasProgram(program))
			{
				addPreset(soundFont, preset, program);
			}
		});
	}
}

} // namespace lmms::sf2
