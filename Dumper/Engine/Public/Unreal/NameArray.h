#pragma once

#include "Unreal/UnrealTypes.h"

class FNameEntry
{
private:
	friend class NameArray;

private:
	static constexpr int32 NameWideMask = 0x1;

private:
	static inline int32 FNameEntryLengthShiftCount = 0x0;

	static inline UnrealString(*GetStr)(uint8* NameEntry) = nullptr;

private:
	uint8* Address;

public:
	FNameEntry() = default;

	FNameEntry(void* Ptr);

public:
	UnrealString GetWString();
	std::string GetString();
	void* GetAddress();

private:
	//Optional to avoid code duplication for FNamePool
	static void Init(const uint8_t* FirstChunkPtr = nullptr, int64 NameEntryStringOffset = 0x0);
};

class NameArray
{
private:
	static inline uint32 FNameBlockOffsetBits = 0x10;
private:
	static uint8* GNames;

	static inline int64 NameEntryStride = 0x0;

	static inline void* (*ByIndex)(void* NamesArray, int32 ComparisonIndex, int32 NamePoolBlockOffsetBits) = nullptr;

private:
	static bool InitializeNameArray(uint8_t* NameArray);
	static bool InitializeNamePool(uint8_t* NamePool);

public:
	/* Per-game FNameEntry decryption hook (default: identity).
	 *
	 * Called on every raw FNameEntry* immediately before the header / chars
	 * are read. Use it for games that XOR or otherwise mangle entry bytes,
	 * or that store entries behind a pointer-level transform.
	 *
	 * The function may either:
	 *   - return its input unchanged (no decryption)
	 *   - return a pointer to a transformed copy in a scratch buffer
	 *   - mutate the entry in place and return the same pointer
	 *
	 * Captured-source-string analogue to ObjectArray::DecryptionLambdaStr so
	 * the SDK emitter can inline the same logic into generated code. */
	static inline std::string DecryptionLambdaStr;

	static inline uint8_t* (*DecryptName)(uint8_t* RawEntryPtr) = [](uint8_t* Ptr) -> uint8_t* { return Ptr; };

	static void InitDecryption(uint8_t* (*DecryptionFunction)(uint8_t* RawEntryPtr), const char* DecryptionLambdaAsStr);

	/* Per-game NamePoolData *pointer* decryption (default: identity).
	 *
	 * Called once during TryInit / TryInit-override, right after the GNames
	 * symbol is resolved but before InitializeNamePool runs. Use it for games
	 * where the discovered address isn't the real pool base, e.g.:
	 *   - PUBG: ADRP+ADD points at an indirection chain that must be walked.
	 *   - Valorant: 8 byte fragments at +128..+156 of the encrypted pool
	 *     reassemble into a pointer that, dereferenced, yields the real pool.
	 *
	 * For games where the chars within each FNameEntry are XOR'd (DeltaForce),
	 * use DecryptName instead — that runs per-entry. */
	static inline std::string NamePoolDecryptionLambdaStr;

	static inline uintptr_t (*DecryptNamePool)(uintptr_t EncryptedNamePoolData) = [](uintptr_t P) -> uintptr_t { return P; };

	static void InitPoolDecryption(uintptr_t (*DecryptionFunction)(uintptr_t EncryptedNamePoolData), const char* DecryptionLambdaAsStr);

public:
	/* Should be changed later and combined */
	static bool TryFindNameArray();
	static bool TryFindNamePool();

	static bool TryInit(bool bIsTestOnly = false);
	static bool TryInit(int32 OffsetOverride, bool bIsNamePool, const char* const ModuleName = nullptr);

	/* Initializes the GNames offset, but doesn't call NameArray::InitializeNameArray() or NameArray::InitializedNamePool() */
	static bool SetGNamesWithoutCommiting();

	static void PostInit();

public:
	static int32 GetNumChunks();

	static int32 GetNumElements();
	static int32 GetByteCursor();

	static FNameEntry GetNameEntry(const void* Name);
	static FNameEntry GetNameEntry(int32 Idx);
};

/* Convenience: pass a lambda and have its source captured automatically for SDK emission. */
#define InitNameArrayDecryption(DecryptionLambda) NameArray::InitDecryption(DecryptionLambda, #DecryptionLambda)
#define InitNamePoolDecryption(DecryptionLambda)  NameArray::InitPoolDecryption(DecryptionLambda, #DecryptionLambda)
