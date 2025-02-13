// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/AssemblyBuilderX64.h"
#include "Luau/CodeAllocator.h"
#include "Luau/CodeBlockUnwind.h"
#include "Luau/UnwindBuilder.h"
#include "Luau/UnwindBuilderDwarf2.h"
#include "Luau/UnwindBuilderWin.h"

#include "doctest.h"

#include <memory>
#include <stdexcept>

#include <string.h>

using namespace Luau::CodeGen;

TEST_SUITE_BEGIN("CodeAllocation");

TEST_CASE("CodeAllocation")
{
    size_t blockSize = 1024 * 1024;
    size_t maxTotalSize = 1024 * 1024;
    CodeAllocator allocator(blockSize, maxTotalSize);

    uint8_t* nativeData = nullptr;
    size_t sizeNativeData = 0;
    uint8_t* nativeEntry = nullptr;

    std::vector<uint8_t> code;
    code.resize(128);

    REQUIRE(allocator.allocate(nullptr, 0, code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
    CHECK(nativeData != nullptr);
    CHECK(sizeNativeData == 128);
    CHECK(nativeEntry != nullptr);
    CHECK(nativeEntry == nativeData);

    std::vector<uint8_t> data;
    data.resize(8);

    REQUIRE(allocator.allocate(data.data(), data.size(), code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
    CHECK(nativeData != nullptr);
    CHECK(sizeNativeData == 16 + 128);
    CHECK(nativeEntry != nullptr);
    CHECK(nativeEntry == nativeData + 16);
}

TEST_CASE("CodeAllocationFailure")
{
    size_t blockSize = 3000;
    size_t maxTotalSize = 7000;
    CodeAllocator allocator(blockSize, maxTotalSize);

    uint8_t* nativeData;
    size_t sizeNativeData;
    uint8_t* nativeEntry;

    std::vector<uint8_t> code;
    code.resize(4000);

    // allocation has to fit in a block
    REQUIRE(!allocator.allocate(nullptr, 0, code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));

    // each allocation exhausts a block, so third allocation fails
    code.resize(2000);
    REQUIRE(allocator.allocate(nullptr, 0, code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
    REQUIRE(allocator.allocate(nullptr, 0, code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
    REQUIRE(!allocator.allocate(nullptr, 0, code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
}

TEST_CASE("CodeAllocationWithUnwindCallbacks")
{
    struct Info
    {
        std::vector<uint8_t> unwind;
        uint8_t* block = nullptr;
        bool destroyCalled = false;
    };
    Info info;
    info.unwind.resize(8);

    {
        size_t blockSize = 1024 * 1024;
        size_t maxTotalSize = 1024 * 1024;
        CodeAllocator allocator(blockSize, maxTotalSize);

        uint8_t* nativeData = nullptr;
        size_t sizeNativeData = 0;
        uint8_t* nativeEntry = nullptr;

        std::vector<uint8_t> code;
        code.resize(128);

        std::vector<uint8_t> data;
        data.resize(8);

        allocator.context = &info;
        allocator.createBlockUnwindInfo = [](void* context, uint8_t* block, size_t blockSize, size_t& unwindDataSizeInBlock) -> void* {
            Info& info = *(Info*)context;

            CHECK(info.unwind.size() == 8);
            memcpy(block, info.unwind.data(), info.unwind.size());
            unwindDataSizeInBlock = 8;

            info.block = block;

            return new int(7);
        };
        allocator.destroyBlockUnwindInfo = [](void* context, void* unwindData) {
            Info& info = *(Info*)context;

            info.destroyCalled = true;

            CHECK(*(int*)unwindData == 7);
            delete (int*)unwindData;
        };

        REQUIRE(allocator.allocate(data.data(), data.size(), code.data(), code.size(), nativeData, sizeNativeData, nativeEntry));
        CHECK(nativeData != nullptr);
        CHECK(sizeNativeData == 16 + 128);
        CHECK(nativeEntry != nullptr);
        CHECK(nativeEntry == nativeData + 16);
        CHECK(nativeData == info.block + 16);
    }

    CHECK(info.destroyCalled);
}

TEST_CASE("WindowsUnwindCodesX64")
{
    UnwindBuilderWin unwind;

    unwind.start();
    unwind.spill(16, rdx);
    unwind.spill(8, rcx);
    unwind.save(rdi);
    unwind.save(rsi);
    unwind.save(rbx);
    unwind.save(rbp);
    unwind.save(r12);
    unwind.save(r13);
    unwind.save(r14);
    unwind.save(r15);
    unwind.allocStack(72);
    unwind.setupFrameReg(rbp, 48);
    unwind.finish();

    std::vector<char> data;
    data.resize(unwind.getSize());
    unwind.finalize(data.data(), nullptr, 0);

    std::vector<uint8_t> expected{0x01, 0x23, 0x0a, 0x35, 0x23, 0x33, 0x1e, 0x82, 0x1a, 0xf0, 0x18, 0xe0, 0x16, 0xd0, 0x14, 0xc0, 0x12, 0x50, 0x10,
        0x30, 0x0e, 0x60, 0x0c, 0x70};

    REQUIRE(data.size() == expected.size());
    CHECK(memcmp(data.data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("Dwarf2UnwindCodesX64")
{
    UnwindBuilderDwarf2 unwind;

    unwind.start();
    unwind.save(rdi);
    unwind.save(rsi);
    unwind.save(rbx);
    unwind.save(rbp);
    unwind.save(r12);
    unwind.save(r13);
    unwind.save(r14);
    unwind.save(r15);
    unwind.allocStack(72);
    unwind.setupFrameReg(rbp, 48);
    unwind.finish();

    std::vector<char> data;
    data.resize(unwind.getSize());
    unwind.finalize(data.data(), nullptr, 0);

    std::vector<uint8_t> expected{0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x78, 0x10, 0x0c, 0x07, 0x08, 0x05, 0x10, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x0e, 0x10, 0x85, 0x02, 0x02, 0x02, 0x0e, 0x18, 0x84, 0x03, 0x02, 0x02, 0x0e, 0x20, 0x83,
        0x04, 0x02, 0x02, 0x0e, 0x28, 0x86, 0x05, 0x02, 0x02, 0x0e, 0x30, 0x8c, 0x06, 0x02, 0x02, 0x0e, 0x38, 0x8d, 0x07, 0x02, 0x02, 0x0e, 0x40,
        0x8e, 0x08, 0x02, 0x02, 0x0e, 0x48, 0x8f, 0x09, 0x02, 0x04, 0x0e, 0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    REQUIRE(data.size() == expected.size());
    CHECK(memcmp(data.data(), expected.data(), expected.size()) == 0);
}

#if defined(__x86_64__) || defined(_M_X64)

#if defined(_WIN32)
// Windows x64 ABI
constexpr RegisterX64 rArg1 = rcx;
constexpr RegisterX64 rArg2 = rdx;
#else
// System V AMD64 ABI
constexpr RegisterX64 rArg1 = rdi;
constexpr RegisterX64 rArg2 = rsi;
#endif

constexpr RegisterX64 rNonVol1 = r12;
constexpr RegisterX64 rNonVol2 = rbx;

TEST_CASE("GeneratedCodeExecution")
{
    AssemblyBuilderX64 build(/* logText= */ false);

    build.mov(rax, rArg1);
    build.add(rax, rArg2);
    build.imul(rax, rax, 7);
    build.ret();

    build.finalize();

    size_t blockSize = 1024 * 1024;
    size_t maxTotalSize = 1024 * 1024;
    CodeAllocator allocator(blockSize, maxTotalSize);

    uint8_t* nativeData;
    size_t sizeNativeData;
    uint8_t* nativeEntry;
    REQUIRE(allocator.allocate(build.data.data(), build.data.size(), build.code.data(), build.code.size(), nativeData, sizeNativeData, nativeEntry));
    REQUIRE(nativeEntry);

    using FunctionType = int64_t(int64_t, int64_t);
    FunctionType* f = (FunctionType*)nativeEntry;
    int64_t result = f(10, 20);
    CHECK(result == 210);
}

void throwing(int64_t arg)
{
    CHECK(arg == 25);

    throw std::runtime_error("testing");
}

TEST_CASE("GeneratedCodeExecutionWithThrow")
{
    AssemblyBuilderX64 build(/* logText= */ false);

#if defined(_WIN32)
    std::unique_ptr<UnwindBuilder> unwind = std::make_unique<UnwindBuilderWin>();
#else
    std::unique_ptr<UnwindBuilder> unwind = std::make_unique<UnwindBuilderDwarf2>();
#endif

    unwind->start();

    // Prologue
    build.push(rNonVol1);
    unwind->save(rNonVol1);
    build.push(rNonVol2);
    unwind->save(rNonVol2);
    build.push(rbp);
    unwind->save(rbp);

    int stackSize = 32;
    int localsSize = 16;

    build.sub(rsp, stackSize + localsSize);
    unwind->allocStack(stackSize + localsSize);

    build.lea(rbp, qword[rsp + stackSize]);
    unwind->setupFrameReg(rbp, stackSize);

    unwind->finish();

    // Body
    build.mov(rNonVol1, rArg1);
    build.mov(rNonVol2, rArg2);

    build.add(rNonVol1, 15);
    build.mov(rArg1, rNonVol1);
    build.call(rNonVol2);

    // Epilogue
    build.lea(rsp, qword[rbp + localsSize]);
    build.pop(rbp);
    build.pop(rNonVol2);
    build.pop(rNonVol1);
    build.ret();

    build.finalize();

    size_t blockSize = 1024 * 1024;
    size_t maxTotalSize = 1024 * 1024;
    CodeAllocator allocator(blockSize, maxTotalSize);

    allocator.context = unwind.get();
    allocator.createBlockUnwindInfo = createBlockUnwindInfo;
    allocator.destroyBlockUnwindInfo = destroyBlockUnwindInfo;

    uint8_t* nativeData;
    size_t sizeNativeData;
    uint8_t* nativeEntry;
    REQUIRE(allocator.allocate(build.data.data(), build.data.size(), build.code.data(), build.code.size(), nativeData, sizeNativeData, nativeEntry));
    REQUIRE(nativeEntry);

    using FunctionType = int64_t(int64_t, void (*)(int64_t));
    FunctionType* f = (FunctionType*)nativeEntry;

    // To simplify debugging, CHECK_THROWS_WITH_AS is not used here
    try
    {
        f(10, throwing);
    }
    catch (const std::runtime_error& error)
    {
        CHECK(strcmp(error.what(), "testing") == 0);
    }
}

#endif

TEST_SUITE_END();
