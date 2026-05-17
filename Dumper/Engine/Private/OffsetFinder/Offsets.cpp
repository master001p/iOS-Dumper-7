#include <format>
#include <thread>
#include <chrono>

#include "Utils.h"

#include "OffsetFinder/Offsets.h"
#include "OffsetFinder/OffsetFinder.h"

#include "Unreal/ObjectArray.h"
#include "Unreal/NameArray.h"

#include "Platform.h"
#include "Architecture.h"


#include "Menu/Logger.h"
void Off::InSDK::ProcessEvent::InitPE()
{
#ifdef PLATFORM_WINDOWS

	void** Vft = *(void***)ObjectArray::GetByIndex(0).GetAddress();

#if defined(_WIN64)
	/* Primary, and more reliable, check for ProcessEvent */
	auto IsProcessEvent = [](const uint8_t* FuncAddress, [[maybe_unused]] int32_t Index) -> bool
	{
		return Platform::FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x0, 0x0, 0x0, 0x04, 0x0, 0x0 }, FuncAddress, 0x400)
			&& Platform::FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0 }, FuncAddress, 0xF00);
	};
#elif defined(_WIN32)
	/* Primary, and more reliable, check for ProcessEvent */
	auto IsProcessEvent = [](const uint8_t* FuncAddress, [[maybe_unused]] int32_t Index) -> bool
	{
		return Platform::FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x4, 0x0, 0x0 }, FuncAddress, 0x400)
			&& Platform::FindPatternInRange({ 0xF7, -0x1, Off::UFunction::FunctionFlags, 0x0, 0x0, 0x40, 0x0 }, FuncAddress, 0xF00);
	};
#endif

	const void* ProcessEventAddr = nullptr;
	int32_t ProcessEventIdx = 0;

	const auto [FuncPtr, FuncIdx] = Platform::IterateVTableFunctions(Vft, IsProcessEvent);

	ProcessEventAddr = FuncPtr;
	ProcessEventIdx = FuncIdx;

	if (!FuncPtr)
	{
		const void* StringRefAddr = Platform::FindByStringInAllSections(TEXT("Accessed None"), 0x0, 0x0, Settings::General::bSearchOnlyExecutableSectionsForStrings);
		/* ProcessEvent is sometimes located right after a func with the string "Accessed None". Might as well check for it, because else we're going to crash anyways. */
		const void* PossiblePEAddr = reinterpret_cast<void*>(Architecture_x86_64::FindNextFunctionStart(StringRefAddr));

		auto IsSameAddr = [PossiblePEAddr](const uint8_t* FuncAddress, [[maybe_unused]] int32_t Index) -> bool
		{
			return FuncAddress == PossiblePEAddr;
		};

		const auto [FuncPtr2, FuncIdx2] = Platform::IterateVTableFunctions(Vft, IsSameAddr);
		ProcessEventAddr = FuncPtr2;
		ProcessEventIdx = FuncIdx2;
	}

	if (ProcessEventAddr)
	{
		Off::InSDK::ProcessEvent::PEIndex = ProcessEventIdx;
		Off::InSDK::ProcessEvent::PEOffset = Platform::GetOffset(ProcessEventAddr);

		LogError("%s", std::format("PE-Offset: 0x{:X}\n", Off::InSDK::ProcessEvent::PEOffset).c_str());
		LogError("%s", std::format("PE-Index: 0x{:X}\n\n", ProcessEventIdx).c_str());
		return;
	}

	LogError("\nCouldn't find ProcessEvent!\n\n");

#endif // PLATFORM_WINDOWS
}

void Off::InSDK::ProcessEvent::InitPE(const int32 Index, const char* const ModuleName)
{
	Off::InSDK::ProcessEvent::PEIndex = Index;

	void** VFT = *reinterpret_cast<void***>(ObjectArray::GetByIndex(0).GetAddress());

	Off::InSDK::ProcessEvent::PEOffset = Platform::GetOffset(VFT[Off::InSDK::ProcessEvent::PEIndex], ModuleName);

	LogError("%s", std::format("PE-Offset: 0x{:X}\n", Off::InSDK::ProcessEvent::PEOffset).c_str());
}

/* UWorld */
void Off::InSDK::World::InitGWorld()
{
	UEClass UWorld = ObjectArray::FindClassFast("World");

	for (UEObject Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject) || !Obj.IsA(UWorld))
			continue;

		/* Try to find a pointer to the word, aka UWorld** GWorld */
		auto Results = Platform::FindAllAlignedValuesInProcess(Obj.GetAddress());

		void* Result = nullptr;
		if (Results.size())
		{
			if (Results.size() == 1)
			{
				Result = Results[0];
			}
			else if (Results.size() == 2)
			{
				auto ObjAddress = reinterpret_cast<uintptr_t>(Obj.GetAddress());
				auto PossibleGWorld = reinterpret_cast<volatile uintptr_t*>(Results[0]);
				auto CurrentValue = *PossibleGWorld;

				for (int i = 0; CurrentValue == ObjAddress && i < 50; ++i)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					CurrentValue = *PossibleGWorld;
				}
				if (CurrentValue == ObjAddress)
				{
					Result = Results[0];
				}
				else
				{
					Result = Results[1];
					LogError("%s", std::format("Filter GActiveLogWorld at 0x{:X}\n\n", reinterpret_cast<uintptr_t>(PossibleGWorld)).c_str());
				}
			}
			else
			{
				LogError("%s", std::format("Detected {} GWorld \n\n", Results.size()).c_str());
			}
		}

		/* Pointer to UWorld* couldn't be found */
		if (Result)
		{
			Off::InSDK::World::GWorld = Platform::GetOffset(Result);
			LogError("%s", std::format("GWorld-Offset: 0x{:X}\n\n", Off::InSDK::World::GWorld).c_str());
			break;
		}
	}

	if (Off::InSDK::World::GWorld == 0x0)
		LogError("%s", std::format("\nGWorld WAS NOT FOUND!!!!!!!!!\n\n").c_str());
}

/* FText */
void Off::InSDK::Text::InitTextOffsets()
{
	if (!Off::InSDK::ProcessEvent::PEIndex)
	{
		LogError("%s", std::format("\nDumper-7: Error, 'InitInSDKTextOffsets' was called before ProcessEvent was initialized!\n").c_str());
		return;
	}

	auto IsValidPtr = [](void* a) -> bool
	{
		return !Platform::IsBadReadPtr(a) /* && (uintptr_t(a) & 0x1) == 0*/; // realistically, there wont be any pointers to unaligned memory
	};


	const UEFunction Conv_StringToText = ObjectArray::FindObjectFast<UEFunction>("Conv_StringToText", EClassCastFlags::Function);

	UEProperty InStringProp = nullptr;
	UEProperty ReturnProp = nullptr;

	if (!Conv_StringToText)
	{
		LogError("Conv_StringToText is invalid!\n");
		return;
	}

	for (UEProperty Prop : Conv_StringToText.GetProperties())
	{
		/* Func has 2 params, if the param is the return value assign to ReturnProp, else InStringProp*/
		if (Prop.HasPropertyFlags(EPropertyFlags::ReturnParm))
		{
			ReturnProp = Prop;
		}
		else
		{
			InStringProp = Prop;
		}
	}

	const int32 ParamSize = Conv_StringToText.GetStructSize();
	const int32 FTextSize = ReturnProp.GetSize();

	const int32 StringOffset = InStringProp.GetOffset();
	const int32 ReturnValueOffset = ReturnProp.GetOffset();

	Off::InSDK::Text::TextSize = FTextSize;


	/* Allocate and zero-initialize ParamStruct */
#pragma warning(disable: 6255)
	uint8_t* ParamPtr = static_cast<uint8_t*>(alloca(ParamSize));
	memset(ParamPtr, 0, ParamSize);

	/* Choose a, fairly random, string to later search for in FTextData */
	constexpr const TCHAR* StringText = TEXT("ThisIsAGoodString!");
	constexpr int32 StringLength = (sizeof(TEXT("ThisIsAGoodString!")) / sizeof(TCHAR));
	constexpr int32 StringLengthBytes = (sizeof(TEXT("ThisIsAGoodString!")));

	/* Initialize 'InString' in the ParamStruct */
	*reinterpret_cast<FString*>(ParamPtr + StringOffset) = StringText;

	/* This function is 'static' so the object on which we call it doesn't matter */
	ObjectArray::GetByIndex(0).ProcessEvent(Conv_StringToText, ParamPtr);

	uint8_t* FTextDataPtr = nullptr;

	/* Search for the first valid pointer inside of the FText and make the offset our 'TextDatOffset' */
	for (int32 i = 0; i < (FTextSize - sizeof(void*)); i += sizeof(void*))
	{
		void* PossibleTextDataPtr = *reinterpret_cast<void**>(ParamPtr + ReturnValueOffset + i);

		if (IsValidPtr(PossibleTextDataPtr))
		{
			FTextDataPtr = static_cast<uint8_t*>(PossibleTextDataPtr);
			Off::InSDK::Text::TextDatOffset = i;
			break;
		}
	}

	if (!FTextDataPtr)
	{
		LogError("%s", std::format("\nDumper-7: Error, 'FTextDataPtr' could not be found!\n").c_str());
		return;
	}

	constexpr int32 MaxOffset = 0x50;
	constexpr int32 StartOffset = sizeof(void*); // FString::NumElements offset

	/* Search for a pointer pointing to a int32 Value (FString::NumElements) equal to StringLength */
	for (int32 i = StartOffset; i < MaxOffset; i += sizeof(int32))
	{
		TCHAR* PosibleStringPtr = *reinterpret_cast<TCHAR**>((FTextDataPtr + i) - sizeof(void*));
		const int32 PossibleLength = *reinterpret_cast<int32*>(FTextDataPtr + i);

		if (PossibleLength == StringLength && PosibleStringPtr && IsValidPtr(PosibleStringPtr) && memcmp(StringText, PosibleStringPtr, StringLengthBytes) == 0)
		{
			Off::InSDK::Text::InTextDataStringOffset = (i - sizeof(void*));
			break;
		}
	}

	LogError("%s", std::format("Off::InSDK::Text::TextSize: 0x{:X}\n", Off::InSDK::Text::TextSize).c_str());
	LogError("%s", std::format("Off::InSDK::Text::TextDatOffset: 0x{:X}\n", Off::InSDK::Text::TextDatOffset).c_str());
	LogError("%s", std::format("Off::InSDK::Text::InTextDataStringOffset: 0x{:X}\n\n", Off::InSDK::Text::InTextDataStringOffset).c_str());
}

void Off::Init()
{
	auto OverwriteIfInvalidOffset = [](int32& Offset, int32 DefaultValue)
	{
		if (Offset == OffsetFinder::OffsetNotFound)
		{
			LogError("%s", std::format("Defaulting to offset: 0x{:X}\n", DefaultValue).c_str());
			Offset = DefaultValue;
		}
	};

	Off::UObject::Flags = OffsetFinder::FindUObjectFlagsOffset();
	OverwriteIfInvalidOffset(Off::UObject::Flags, sizeof(void*)); // Default to right after VTable
	LogError("%s", std::format("Off::UObject::Flags: 0x{:X}\n", Off::UObject::Flags).c_str());

	Off::UObject::Index = OffsetFinder::FindUObjectIndexOffset();
	OverwriteIfInvalidOffset(Off::UObject::Index, (Off::UObject::Flags + sizeof(int32))); // Default to right after Flags
	LogError("%s", std::format("Off::UObject::Index: 0x{:X}\n", Off::UObject::Index).c_str());

	Off::UObject::Class = OffsetFinder::FindUObjectClassOffset();
	OverwriteIfInvalidOffset(Off::UObject::Class, (Off::UObject::Index + sizeof(int32))); // Default to right after Index
	LogError("%s", std::format("Off::UObject::Class: 0x{:X}\n", Off::UObject::Class).c_str());

	Off::UObject::Outer = OffsetFinder::FindUObjectOuterOffset();
	LogError("%s", std::format("Off::UObject::Outer: 0x{:X}\n", Off::UObject::Outer).c_str());

	Off::UObject::Name = OffsetFinder::FindUObjectNameOffset();
	OverwriteIfInvalidOffset(Off::UObject::Name, (Off::UObject::Class + sizeof(void*))); // Default to right after Class
	LogError("%s", std::format("Off::UObject::Name: 0x{:X}\n\n", Off::UObject::Name).c_str());

	OverwriteIfInvalidOffset(Off::UObject::Outer, (Off::UObject::Name + sizeof(int32) + sizeof(int32)));  // Default to right after Name

	OffsetFinder::InitFNameSettings();

	::NameArray::PostInit();

	// Castflags needs to stay here since the FindChildOffset() uses CastFlags
	Off::UClass::CastFlags = OffsetFinder::FindCastFlagsOffset();
	LogError("%s", std::format("Off::UClass::CastFlags: 0x{:X}\n", Off::UClass::CastFlags).c_str());

	Off::UStruct::Children = OffsetFinder::FindChildOffset();
	LogError("%s", std::format("Off::UStruct::Children: 0x{:X}\n", Off::UStruct::Children).c_str());

	Off::UField::Next = OffsetFinder::FindUFieldNextOffset();
	LogError("%s", std::format("Off::UField::Next: 0x{:X}\n", Off::UField::Next).c_str());

	Off::UStruct::SuperStruct = OffsetFinder::FindSuperOffset();
	LogError("%s", std::format("Off::UStruct::SuperStruct: 0x{:X}\n", Off::UStruct::SuperStruct).c_str());

	Off::UStruct::Size = OffsetFinder::FindStructSizeOffset();
	LogError("%s", std::format("Off::UStruct::Size: 0x{:X}\n", Off::UStruct::Size).c_str());

	Off::UStruct::MinAlignment = OffsetFinder::FindMinAlignmentOffset();
	LogError("%s", std::format("Off::UStruct::MinAlignment: 0x{:X}\n", Off::UStruct::MinAlignment).c_str());

	Off::UClass::CastFlags = OffsetFinder::FindCastFlagsOffset();
	LogError("%s", std::format("Off::UClass::CastFlags: 0x{:X}\n", Off::UClass::CastFlags).c_str());

	// Castflags become available for use

	if (Settings::Internal::bUseFProperty)
	{
		LogError("%s", std::format("\nGame uses FProperty system\n\n").c_str());

		Off::UStruct::ChildProperties = OffsetFinder::FindChildPropertiesOffset();
		LogError("%s", std::format("Off::UStruct::ChildProperties: 0x{:X}\n", Off::UStruct::ChildProperties).c_str());

		OffsetFinder::FixupHardcodedOffsets(); // must be called after FindChildPropertiesOffset 

		Off::FField::Next = OffsetFinder::FindFFieldNextOffset();
		LogError("%s", std::format("Off::FField::Next: 0x{:X}\n", Off::FField::Next).c_str());

		Off::FField::Class = OffsetFinder::FindFFieldClassOffset();
		LogError("%s", std::format("Off::FField::Class: 0x{:X}\n", Off::FField::Class).c_str());

		// Comment out this line if you're crashing here and see if the NewFindFFieldNameOffset might work!
		Off::FField::Name = OffsetFinder::FindFFieldNameOffset();
		//Off::FField::Name = OffsetFinder::NewFindFFieldNameOffset();

		if (Off::FField::Name == OffsetFinder::OffsetNotFound)
			Off::FField::Name = OffsetFinder::NewFindFFieldNameOffset();

		LogError("%s", std::format("Off::FField::Name: 0x{:X}\n", Off::FField::Name).c_str());

		/*
		* FNameSize might be wrong at this point of execution.
		* FField::Flags is not critical so a fix is only applied later in OffsetFinder::PostInitFNameSettings().
		*/
		Off::FField::Flags = Off::FField::Name + Off::InSDK::Name::FNameSize;
		LogError("%s", std::format("Off::FField::Flags: 0x{:X}\n", Off::FField::Flags).c_str());

		Off::FField::EditorOnlyMetadata = OffsetFinder::FindFFieldEditorOnlyMetaDataOffset();
		if (Off::FField::EditorOnlyMetadata != OffsetFinder::OffsetNotFound)
			LogError("%s", std::format("Off::FField::EditorOnlyMetadata: 0x{:X}\n", Off::FField::EditorOnlyMetadata).c_str());

		Off::FFieldClass::CastFlags = OffsetFinder::FindFieldClassCastFlagsOffset();
		LogError("%s", std::format("Off::FFieldClass::CastFlags: 0x{:X}\n\n", Off::FFieldClass::CastFlags).c_str());
	}

	Off::UStruct::StructBaseChain = OffsetFinder::FindStructBaseChainOffset();
	if (Off::UStruct::StructBaseChain != OffsetFinder::OffsetNotFound)
		LogError("%s", std::format("Off::UStruct::StructBaseChain: 0x{:X}\n", Off::UStruct::StructBaseChain).c_str());

	Off::UClass::ClassDefaultObject = OffsetFinder::FindDefaultObjectOffset();
	LogError("%s", std::format("Off::UClass::ClassDefaultObject: 0x{:X}\n", Off::UClass::ClassDefaultObject).c_str());

	Off::UClass::ImplementedInterfaces = OffsetFinder::FindImplementedInterfacesOffset();
	LogError("%s", std::format("Off::UClass::ImplementedInterfaces: 0x{:X}\n", Off::UClass::ImplementedInterfaces).c_str());

	Off::UEnum::Names = OffsetFinder::FindEnumNamesOffset();
	LogError("%s", std::format("Off::UEnum::Names: 0x{:X}\n", Off::UEnum::Names).c_str());

	Off::UFunction::FunctionFlags = OffsetFinder::FindFunctionFlagsOffset();
	LogError("%s", std::format("Off::UFunction::FunctionFlags: 0x{:X}\n", Off::UFunction::FunctionFlags).c_str());

	Off::UFunction::ExecFunction = OffsetFinder::FindFunctionNativeFuncOffset();
	LogError("%s", std::format("Off::UFunction::ExecFunction: 0x{:X}\n", Off::UFunction::ExecFunction).c_str());

	Off::Property::ElementSize = OffsetFinder::FindElementSizeOffset();
	LogError("%s", std::format("Off::Property::ElementSize: 0x{:X}\n", Off::Property::ElementSize).c_str());

	Off::Property::ArrayDim = OffsetFinder::FindArrayDimOffset();
	LogError("%s", std::format("Off::Property::ArrayDim: 0x{:X}\n", Off::Property::ArrayDim).c_str());

	Off::Property::Offset_Internal = OffsetFinder::FindOffsetInternalOffset();
	LogError("%s", std::format("Off::Property::Offset_Internal: 0x{:X}\n", Off::Property::Offset_Internal).c_str());

	Off::Property::PropertyFlags = OffsetFinder::FindPropertyFlagsOffset();
	LogError("%s", std::format("Off::Property::PropertyFlags: 0x{:X}\n", Off::Property::PropertyFlags).c_str());

	Off::BoolProperty::Base = OffsetFinder::FindBoolPropertyBaseOffset();
	LogError("%s", std::format("UBoolProperty::Base: 0x{:X}\n", Off::BoolProperty::Base).c_str());

	Off::EnumProperty::Base = OffsetFinder::FindEnumPropertyBaseOffset();
	LogError("%s", std::format("Off::EnumProperty::Base: 0x{:X}\n", Off::EnumProperty::Base).c_str());


	if (Off::EnumProperty::Base == OffsetFinder::OffsetNotFound)
	{
		Off::InSDK::Properties::PropertySize = Off::BoolProperty::Base;
		Off::EnumProperty::Base = Off::BoolProperty::Base;
	}
	else
	{
		Off::InSDK::Properties::PropertySize = Off::EnumProperty::Base;
	}

	LogError("%s", std::format("UPropertySize: 0x{:X}\n", Off::InSDK::Properties::PropertySize).c_str());

	Off::ObjectProperty::PropertyClass = OffsetFinder::FindObjectPropertyClassOffset();
	LogError("%s", std::format("Off::ObjectProperty::PropertyClass: 0x{:X}", Off::ObjectProperty::PropertyClass).c_str());
	OverwriteIfInvalidOffset(Off::ObjectProperty::PropertyClass, Off::InSDK::Properties::PropertySize);

	Off::ByteProperty::Enum = OffsetFinder::FindBytePropertyEnumOffset();
	OverwriteIfInvalidOffset(Off::ByteProperty::Enum, Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::ByteProperty::Enum: 0x{:X}", Off::ByteProperty::Enum).c_str());

	Off::StructProperty::Struct = OffsetFinder::FindStructPropertyStructOffset();
	OverwriteIfInvalidOffset(Off::StructProperty::Struct, Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::StructProperty::Struct: 0x{:X}\n", Off::StructProperty::Struct).c_str());

	Off::DelegateProperty::SignatureFunction = OffsetFinder::FindDelegatePropertySignatureFunctionOffset();
	OverwriteIfInvalidOffset(Off::DelegateProperty::SignatureFunction, Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::DelegateProperty::SignatureFunction: 0x{:X}\n", Off::DelegateProperty::SignatureFunction).c_str());

	Off::ArrayProperty::Inner = OffsetFinder::FindInnerTypeOffset(Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::ArrayProperty::Inner: 0x{:X}\n", Off::ArrayProperty::Inner).c_str());

	Off::SetProperty::ElementProp = OffsetFinder::FindSetPropertyBaseOffset(Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::SetProperty::ElementProp: 0x{:X}\n", Off::SetProperty::ElementProp).c_str());

	Off::MapProperty::Base = OffsetFinder::FindMapPropertyBaseOffset(Off::InSDK::Properties::PropertySize);
	LogError("%s", std::format("Off::MapProperty::Base: 0x{:X}\n", Off::MapProperty::Base).c_str());

	Off::InSDK::ULevel::Actors = OffsetFinder::FindLevelActorsOffset();
	LogError("%s", std::format("Off::InSDK::ULevel::Actors: 0x{:X}\n", Off::InSDK::ULevel::Actors).c_str());

	Off::InSDK::UDataTable::RowMap = OffsetFinder::FindDatatableRowMapOffset();
	LogError("%s", std::format("Off::InSDK::UDataTable::RowMap: 0x{:X}\n", Off::InSDK::UDataTable::RowMap).c_str());

	OffsetFinder::PostInitFNameSettings();

	LogError("");

	Off::FieldPathProperty::FieldClass = Off::InSDK::Properties::PropertySize;
	Off::OptionalProperty::ValueProperty = Off::InSDK::Properties::PropertySize;

	Off::ClassProperty::MetaClass = Off::ObjectProperty::PropertyClass + sizeof(void*); //0x8 inheritance from ObjectProperty
}

void PropertySizes::Init()
{
	InitTDelegateSize();
	InitFFieldPathSize();
	InitTMulticastInlineDelegateSize();
}

void PropertySizes::InitTDelegateSize()
{
	/* If the AudioComponent class or the OnQueueSubtitles member weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
	{
		for (UEObject Obj : ObjectArray())
		{
			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::DelegateProperty))
				{
					PropertySizes::DelegateProperty = Prop.GetSize();
					return;
				}
			}
		}
	};

	const UEClass AudioComponentClass = ObjectArray::FindClassFast("AudioComponent");

	if (!AudioComponentClass)
		return OnPropertyNotFound();

	const UEProperty OnQueueSubtitlesProp = AudioComponentClass.FindMember("OnQueueSubtitles", EClassCastFlags::DelegateProperty);

	if (!OnQueueSubtitlesProp)
		return OnPropertyNotFound();

	PropertySizes::DelegateProperty = OnQueueSubtitlesProp.GetSize();
}

void PropertySizes::InitFFieldPathSize()
{
	if (!Settings::Internal::bUseFProperty)
		return;

	/* If the SetFieldPathPropertyByName function or the Value parameter weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
	{
		for (UEObject Obj : ObjectArray())
		{
			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::FieldPathProperty))
				{
					PropertySizes::FieldPathProperty = Prop.GetSize();
					return;
				}
			}
		}
	};

	const UEFunction SetFieldPathPropertyByNameFunc = ObjectArray::FindObjectFast<UEFunction>("SetFieldPathPropertyByName", EClassCastFlags::Function);

	if (!SetFieldPathPropertyByNameFunc)
		return OnPropertyNotFound();

	const UEProperty ValueParamProp = SetFieldPathPropertyByNameFunc.FindMember("Value", EClassCastFlags::FieldPathProperty);

	if (!ValueParamProp)
		return OnPropertyNotFound();

	PropertySizes::FieldPathProperty = ValueParamProp.GetSize();
}

void PropertySizes::InitTMulticastInlineDelegateSize()
{
	/* If the AudioComponent class or the OnQueueSubtitles member weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
		{
			for (UEObject Obj : ObjectArray())
			{
				if (!Obj.IsA(EClassCastFlags::Struct))
					continue;

				for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
				{
					if (Prop.IsA(EClassCastFlags::MulticastInlineDelegateProperty))
					{
						PropertySizes::DelegateProperty = Prop.GetSize();
						return;
					}
				}
			}
		};

	const UEClass EmitterClass = ObjectArray::FindClassFast("Emitter");

	if (!EmitterClass)
		return OnPropertyNotFound();

	const UEProperty OnParticleSpawn = EmitterClass.FindMember("OnParticleSpawn", EClassCastFlags::MulticastDelegateProperty);

	if (!OnParticleSpawn)
		return OnPropertyNotFound();

	PropertySizes::MulticastInlineDelegateProperty = OnParticleSpawn.GetSize();
}