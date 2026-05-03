#include "tests.h"
#include <cstdint>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "TEST";
static int s_CaseStartPass = 0;
static int s_CaseStartFail = 0;
static int s_CaseNumber = 0;

void Test_BeginSuite(TestContext& T, const char* SuiteName)
{
    (void)T;
    printf("\n");
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "    SUITE: %s", SuiteName);
    ESP_LOGI(TAG, "==================================================");
    printf("\n");
}

void Test_BeginCase(TestContext& T, int CaseNumber, const char* Description)
{
    s_CaseNumber = CaseNumber;
    s_CaseStartPass = T.Pass;
    s_CaseStartFail = T.Fail;

    printf("\n");
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "    TEST CASE %d: %s", CaseNumber, Description ? Description : "");
    ESP_LOGI(TAG, "--------------------------------------------------");
    printf("\n");
}

void Test_EndCase(TestContext& T)
{
    const int casePass = T.Pass - s_CaseStartPass;
    const int caseFail = T.Fail - s_CaseStartFail;

    printf("\n");
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "    CASE %d SUMMARY: %d PASS, %d FAIL", s_CaseNumber, casePass, caseFail);
    ESP_LOGI(TAG, "--------------------------------------------------");
    printf("\n");
}

void Test_EndSuite(TestContext& T)
{
    printf("\n");
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "    SUMMARY: %d PASS, %d FAIL", T.Pass, T.Fail);
    ESP_LOGI(TAG, "==================================================");
    printf("\n");
}

void Test_AssertTrue(TestContext& T, bool Cond, const char* Msg)
{
    if (Cond) { ++T.Pass; ESP_LOGI(TAG, "PASS: %s", Msg); }
    else      { ++T.Fail; ESP_LOGE(TAG, "FAIL: %s", Msg); }
}

void Test_AssertFalse(TestContext& T, bool Cond, const char* Msg)
{
    Test_AssertTrue(T, !Cond, Msg);
}

void Test_AssertEqBool(TestContext& T, bool Actual, bool Expected, const char* Msg)
{
    if (Actual == Expected) { ++T.Pass; ESP_LOGI(TAG, "PASS: %s", Msg); }
    else {
        ++T.Fail;
        ESP_LOGE(TAG, "FAIL: %s (Actual=%d Expected=%d)", Msg, (int)Actual, (int)Expected);
    }
}

void Test_AssertEqSize(TestContext& T, size_t Actual, size_t Expected, const char* Msg)
{
    if (Actual == Expected) { ++T.Pass; ESP_LOGI(TAG, "PASS: %s", Msg); }
    else {
        ++T.Fail;
        ESP_LOGE(TAG, "FAIL: %s (Actual=%u Expected=%u)", Msg, (unsigned)Actual, (unsigned)Expected);
    }
}

void Test_PrintHex(const char* TagLabel, const uint8_t* Buf, size_t Len)
{
    if (!Buf) { ESP_LOGW(TAG, "%s: (null)", TagLabel ? TagLabel : "BUF"); return; }

    ESP_LOGI(TAG, "%s (len=%u)", TagLabel ? TagLabel : "BUF", (unsigned)Len);

    char line[3 * 16 + 1];
    for (size_t i = 0; i < Len; i += 16)
    {
        size_t n = (Len - i < 16) ? (Len - i) : 16;

        size_t p = 0;
        for (size_t j = 0; j < n; ++j)
        {
            uint8_t b = Buf[i + j];
            const char* hex = "0123456789ABCDEF";
            line[p++] = hex[b >> 4];
            line[p++] = hex[b & 0x0F];
            line[p++] = ' ';
        }
        line[p] = 0;

        ESP_LOGI(TAG, "%04u: %s", (unsigned)i, line);
    }
}




int RunAllTests()
{
    TestContext T;
    Test_BeginSuite(T, "All Tests");
    Test_EndSuite(T);

    return T.Fail;
}
