
#include "Generators/Generator.h"
#include "Managers/StructManager.h"
#include "Managers/EnumManager.h"
#include "Managers/MemberManager.h"
#include "Managers/PackageManager.h"

#include "HashStringTable.h"
#include "Utils.h"
#include "Menu/Logger.h"
#include "Unreal/NameArray.h"

inline void InitSettings()
{
	Settings::InitWeakObjectPtrSettings();
	Settings::InitLargeWorldCoordinateSettings();

	Settings::InitObjectPtrPropertySettings();
	Settings::InitArrayDimSizeSettings();
}


void Generator::InitEngineCore()
{
	LogInfo("Initializing Engine Core...");

	/* manual override */
	//ObjectArray::Init(/*GObjects*/, /*ChunkSize*/, /*bIsChunked*/);
	//FName::Init(/*FName::AppendString*/);
	//FName::Init(/*FName::ToString, FName::EOffsetOverrideType::ToString*/);
	//FName::Init(/*GNames, FName::EOffsetOverrideType::GNames, true/false*/);
	//Off::InSDK::ProcessEvent::InitPE(/*PEIndex*/);

	/* Back4Blood (requires manual GNames override) */
	//InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x8375); });

	/* Multiversus [Unsupported, weird GObjects-struct] */
	//InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x1B5DEAFD6B4068C); });

	ObjectArray::Init();
    FName::Init((int32)0x0d59d400, FName::EOffsetOverrideType::GNames, true, "UAGame"); // ArenaBreakout
//    FName::Init((int32)0x05E4AD40, FName::EOffsetOverrideType::GNames, true, "ShooterGame"); // ARK Revamp
//	FName::Init((int32)0x420fc48, FName::EOffsetOverrideType::GNames, false /* Not FNamePool */, "ShooterGame"); // ARK 2.0
//    FName::Init();
	Off::Init();
	PropertySizes::Init();
	Off::InSDK::ProcessEvent::InitPE(71); //Must be at this position, relies on offsets initialized in Off::Init()

	Off::InSDK::World::InitGWorld(); //Must be at this position, relies on offsets initialized in Off::Init()

	Off::InSDK::Text::InitTextOffsets(); //Must be at this position, relies on offsets initialized in Off::InitPE()

	InitSettings();
	LogSuccess("Engine Core initialized successfully");
}

void Generator::InitInternal()
{
	LogInfo("Initializing Internal Generator...");

	// Initialize PackageManager with all packages, their names, structs, classes enums, functions and dependencies
	PackageManager::Init();

	// Initialize StructManager with all structs and their names
	StructManager::Init();

	// Initialize EnumManager with all enums and their names
	EnumManager::Init();

	// Initialized all Member-Name collisions
	MemberManager::Init();

	// Post-Initialize PackageManager after StructManager has been initialized. 'PostInit()' handles Cyclic-Dependencies detection
	PackageManager::PostInit();

	LogSuccess("Internal Generator initialized successfully");
}

bool Generator::SetupDumperFolder()
{
	try
	{
		std::string FolderName = (Settings::Generator::GameVersion + '-' + Settings::Generator::GameName);

		FileNameHelper::MakeValidFileName(FolderName);

		DumperFolder = fs::path(Settings::Generator::SDKGenerationPath) / "Documents" / FolderName;

		if (fs::exists(DumperFolder))
		{
			fs::path Old = DumperFolder.generic_string() + "_OLD";

			fs::remove_all(Old);

			fs::rename(DumperFolder, Old);
		}

		fs::create_directories(DumperFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		LogError("Could not create required folders! Info: %s", fe.what());
		return false;
	}

	LogSuccess("Dumper folder created: %s", DumperFolder.string().c_str());
	return true;
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder)
{
	fs::path Dummy;
	std::string EmptyName = "";
	return SetupFolders(FolderName, OutFolder, EmptyName, Dummy);
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder, std::string& SubfolderName, fs::path& OutSubFolder)
{
	FileNameHelper::MakeValidFileName(FolderName);
	FileNameHelper::MakeValidFileName(SubfolderName);

	try
	{
		OutFolder = DumperFolder / FolderName;
		OutSubFolder = OutFolder / SubfolderName;

		if (fs::exists(OutFolder))
		{
			fs::path Old = OutFolder.generic_string() + "_OLD";

			fs::remove_all(Old);

			fs::rename(OutFolder, Old);
		}

		fs::create_directories(OutFolder);

		if (!SubfolderName.empty())
			fs::create_directories(OutSubFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		LogError("Could not create required folders! Info: %s", fe.what());
		return false;
	}

	return true;
}


// iOS port: stub. Upstream emits a Metadata.json describing editor-only properties.
// The iOS port doesn't yet expose Off::FField::EditorOnlyMetadata; skipping for KISS.
void DumpEditorOnlyMetadata(const fs::path& /*DumperFolder*/)
{
}
