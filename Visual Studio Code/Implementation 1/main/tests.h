#ifndef Tests_H
#define Tests_H


#pragma once
#include <cstddef>
#include <cstdint>

struct TestContext
{
    int Pass = 0;
    int Fail = 0;
};

void Test_BeginSuite(TestContext& T, const char* SuiteName);
void Test_BeginCase(TestContext& T, int CaseNumber, const char* Description);
void Test_EndSuite(TestContext& T);

void Test_AssertTrue(TestContext& T, bool Cond, const char* Msg);
void Test_AssertFalse(TestContext& T, bool Cond, const char* Msg);

void Test_AssertEqBool(TestContext& T, bool Actual, bool Expected, const char* Msg);
void Test_AssertEqSize(TestContext& T, size_t Actual, size_t Expected, const char* Msg);

// Optional: print bytes
void Test_PrintHex(const char* Tag, const uint8_t* Buf, size_t Len);

int RunAllTests();

#endif