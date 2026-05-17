#include "Arch_arm64.h"

#include <cstddef>

#include "Utils.h"

/*
ARM64 stubs for the Architecture_x86_64 surface. OffsetFinder calls a tiny subset
of these on ARM64; functions specific to x86 instruction encodings are no-ops.
ARM64-native decoders live in Utils.h's ASMUtils namespace and are used directly.
*/

namespace Architecture_x86_64
{
    bool IsValid64BitVirtualAddress(const uintptr_t Address)
    {
        return Address != 0 && !IsBadReadPtr(Address);
    }

    bool IsValid64BitVirtualAddress(const void* Address)
    {
        return IsValid64BitVirtualAddress(reinterpret_cast<uintptr_t>(Address));
    }

    bool Is32BitRIPRelativeJump(const uintptr_t /*Address*/) { return false; }

    uintptr_t Resolve32BitRIPRelativeJumpTarget(const uintptr_t Address) { return Address; }
    uintptr_t Resolve32BitRegisterRelativeJump(const uintptr_t Address)  { return Address; }
    uintptr_t Resolve32BitSectionRelativeCall(const uintptr_t Address)   { return Address; }
    uintptr_t Resolve32BitRelativeCall(const uintptr_t Address)          { return Address; }
    uintptr_t Resolve32BitRelativeMove(const uintptr_t Address)          { return Address; }
    uintptr_t Resolve32BitRelativeLea(const uintptr_t Address)           { return Address; }
    uintptr_t Resolve32BitRelativePush(const uintptr_t Address)          { return Address; }
    uintptr_t Resolve32bitAbsoluteCall(const uintptr_t Address)          { return Address; }
    uintptr_t Resolve32bitAbsoluteMove(const uintptr_t Address)          { return Address; }

    bool IsFunctionRet(const uintptr_t Address)
    {
        if (IsBadReadPtr(Address)) return false;
        // ARM64 RET / RET LR variants.
        const uint32_t Instr = *reinterpret_cast<const uint32_t*>(Address);
        return Instr == 0xD65F03C0 || Instr == 0xD65F03E0 || Instr == 0xD65F03E1;
    }

    uintptr_t ResolveJumpIfInstructionIsJump(const uintptr_t Address, const uintptr_t DefaultReturnValueOnFail)
    {
        if (IsBadReadPtr(Address)) return DefaultReturnValueOnFail;
        const uint32_t Instr = *reinterpret_cast<const uint32_t*>(Address);
        if (ASMUtils::IsBranchInstruction(Instr))
            return ASMUtils::ResolveBranchTarget(Address);
        return DefaultReturnValueOnFail;
    }

    uintptr_t FindNextFunctionStart(const uintptr_t Address)
    {
        if (IsBadReadPtr(Address)) return 0;
        for (uintptr_t Addr = Address; Addr < Address + 0xFFFF; Addr += 4)
        {
            if (IsFunctionRet(Addr))
                return Addr + 4;
        }
        return 0;
    }

    uintptr_t FindNextFunctionStart(const void* Address)
    {
        return FindNextFunctionStart(reinterpret_cast<uintptr_t>(Address));
    }

    uintptr_t FindFunctionEnd(const uintptr_t Address, uint32_t Range)
    {
        if (IsBadReadPtr(Address)) return 0;
        if (Range > 0xFFFF) Range = 0xFFFF;
        for (uintptr_t i = 0; i < Range; i += 4)
        {
            if (IsFunctionRet(Address + i))
                return Address + i;
        }
        return 0;
    }

    uintptr_t GetRipRelativeCalledFunction(const uintptr_t Address, const int32_t OneBasedFuncIndex, bool(*IsWantedTarget)(const uintptr_t CalledAddr))
    {
        if (!Address || OneBasedFuncIndex == 0) return 0;
        const int32_t Multiply = OneBasedFuncIndex > 0 ? 1 : -1;
        const int32_t Target = OneBasedFuncIndex * Multiply;

        int32_t NumCalls = 0;
        for (int i = 0; i < 0xFFF; i += 4)
        {
            const int32_t ByteOffset = i * Multiply;
            const uintptr_t CurrentPC = Address + ByteOffset;
            if (IsBadReadPtr(CurrentPC)) break;

            const uint32_t Instr = *reinterpret_cast<const uint32_t*>(CurrentPC);
            if (!ASMUtils::IsBranchInstruction(Instr)) continue;

            const uintptr_t CallTarget = ASMUtils::ResolveBranchTarget(CurrentPC);
            if (!IsInProcessRange(CallTarget)) continue;

            if (++NumCalls == Target)
            {
                if (IsWantedTarget && !IsWantedTarget(CallTarget))
                {
                    --NumCalls;
                    continue;
                }
                return CallTarget;
            }
        }
        return 0;
    }
}
