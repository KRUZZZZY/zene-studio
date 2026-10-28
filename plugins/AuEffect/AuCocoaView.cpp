/*
 * AuCocoaView.cpp - an Audio Unit's own Cocoa view (Q050-491, macOS only)
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
 */

// Compiled as Objective-C++ (CMakeLists.txt: -x objective-c++), without ARC, like AuHost.cpp.

#include <cstddef>
#include <cstdint>
#include <vector>

#import <AppKit/AppKit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AUCocoaUIView.h>

namespace lmms::au
{

namespace
{

//! The view the factory class @a info names makes for @a unit, retained; nil when the class is not one.
NSView* factoryView(AudioUnit unit, const AudioUnitCocoaViewInfo& info)
{
	NSURL* location = (NSURL*)info.mCocoaAUViewBundleLocation;
	NSString* className = (NSString*)info.mCocoaAUViewClass[0];
	NSBundle* bundle = location != nil ? [NSBundle bundleWithURL:location] : nil;
	Class factoryClass = bundle != nil && className != nil ? [bundle classNamed:className] : nil;
	if (factoryClass == nil || ![factoryClass conformsToProtocol:@protocol(AUCocoaUIBase)]) { return nil; }
	// AUCocoaUIBase does not adopt <NSObject>: the factory is typed as the NSObject it is, for -release.
	NSObject<AUCocoaUIBase>* factory = (NSObject<AUCocoaUIBase>*)[[factoryClass alloc] init];
	NSView* view = [[factory uiViewForAudioUnit:unit withSize:NSZeroSize] retain];
	[factory release];
	return view;
}

//! kAudioUnitProperty_CocoaUI hands over one reference to the URL and to each class name of its @a size bytes.
void releaseViewInfo(AudioUnitCocoaViewInfo* info, UInt32 size)
{
	const auto classes = (size - offsetof(AudioUnitCocoaViewInfo, mCocoaAUViewClass)) / sizeof(CFStringRef);
	if (info->mCocoaAUViewBundleLocation != nullptr) { CFRelease(info->mCocoaAUViewBundleLocation); }
	for (std::size_t i = 0; i < classes; ++i)
	{
		if (info->mCocoaAUViewClass[i] != nullptr) { CFRelease(info->mCocoaAUViewClass[i]); }
	}
}

} // namespace

NSView* customView(AudioUnit unit)
{
	UInt32 size = 0;
	Boolean writable = false;
	if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) != noErr
		|| size < sizeof(AudioUnitCocoaViewInfo))
	{
		return nil;
	}
	std::vector<std::uint8_t> raw(size);
	auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(raw.data());
	if (AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size) != noErr) { return nil; }
	NSView* view = factoryView(unit, *info);
	releaseViewInfo(info, size);
	return view;
}

} // namespace lmms::au
