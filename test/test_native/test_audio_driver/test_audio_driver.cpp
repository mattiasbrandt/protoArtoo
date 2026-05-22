// =============================================================================
// test/test_native/test_audio_driver/test_audio_driver.cpp
//
// Unit tests for the AudioDriver abstract interface.
// Covers capability bits, default catalog methods, and interface contracts.
// =============================================================================

#include <unity.h>
#include <string.h>
#include "../../../include/audio_driver.h"
#include "../../../include/audio_dollar_parser.h"

// Stub driver without catalog support.
class AudioDriverStub : public AudioDriver {
   public:
    bool begin(uint8_t vol) override { last_vol = vol; return true; }
    void playTrack(uint16_t track) override { last_track = track; }
    void stop() override { stopped = true; }
    void setVolume(uint8_t vol) override { last_vol = vol; }
    const char* driverName() const override { return "StubDriver"; }

    uint8_t last_vol = 0;
    uint16_t last_track = 0;
    bool stopped = false;
};

// Stub driver WITH catalog support (simulates CHIRP).
class AudioDriverCatalogStub : public AudioDriver {
   public:
    bool begin(uint8_t vol) override { last_vol = vol; return true; }
    void playTrack(uint16_t track) override { last_track = track; }
    void stop() override { stopped = true; }
    void setVolume(uint8_t vol) override { last_vol = vol; }
    const char* driverName() const override { return "CatalogStub"; }

    uint8_t capabilities() const override {
        return AUDIO_CAP_CATALOG;
    }

    bool refreshCatalog() override {
        catalog_refreshed = true;
        return true;
    }

    bool isCatalogReady() const override {
        return catalog_ready;
    }

    uint16_t getCatalogEntryCount() const override {
        return catalog_entry_count;
    }

    const AudioCatalogEntry* getCatalogEntries() const override {
        return catalog_entry_count > 0 ? &test_entries[0] : nullptr;
    }

    uint8_t getCatalogBankCount() const override {
        return catalog_bank_count;
    }

    const AudioCatalogBank* getCatalogBanks() const override {
        return catalog_bank_count > 0 ? &test_banks[0] : nullptr;
    }

    uint8_t last_vol = 0;
    uint16_t last_track = 0;
    bool stopped = false;
    bool catalog_refreshed = false;
    bool catalog_ready = false;
    uint16_t catalog_entry_count = 0;
    uint8_t catalog_bank_count = 0;

    // Test data
    AudioCatalogEntry test_entries[3] = {
        {.bank = 1, .page = 'A', .index = 1, .name = "Entry1"},
        {.bank = 1, .page = 'A', .index = 2, .name = "Entry2"},
        {.bank = 1, .page = 'A', .index = 3, .name = "Entry3"},
    };

    AudioCatalogBank test_banks[2] = {
        {.bank = 1, .page = 'A', .dirName = "Bank1A", .count = 10},
        {.bank = 1, .page = 'B', .dirName = "Bank1B", .count = 5},
    };
};

// Test: Stub driver without catalog returns safe defaults
void test_stub_driver_no_catalog_support() {
    AudioDriverStub driver;

    // Capabilities should indicate no catalog
    TEST_ASSERT_EQUAL_UINT8(0, driver.capabilities());

    // Catalog methods should return safe defaults
    TEST_ASSERT_FALSE(driver.isCatalogReady());
    TEST_ASSERT_EQUAL_UINT16(0, driver.getCatalogEntryCount());
    TEST_ASSERT_NULL(driver.getCatalogEntries());
    TEST_ASSERT_EQUAL_UINT8(0, driver.getCatalogBankCount());
    TEST_ASSERT_NULL(driver.getCatalogBanks());
    TEST_ASSERT_FALSE(driver.refreshCatalog());
}

// Test: Stub driver with catalog can expose catalog capability
void test_catalog_stub_exposes_capability() {
    AudioDriverCatalogStub driver;

    uint8_t caps = driver.capabilities();
    TEST_ASSERT_TRUE(caps & AudioDriver::AUDIO_CAP_CATALOG);
}

// Test: Catalog stub can populate entries
void test_catalog_stub_entries() {
    AudioDriverCatalogStub driver;
    driver.catalog_entry_count = 3;
    driver.catalog_ready = true;

    TEST_ASSERT_EQUAL_UINT16(3, driver.getCatalogEntryCount());
    TEST_ASSERT_TRUE(driver.isCatalogReady());

    const AudioCatalogEntry* entries = driver.getCatalogEntries();
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_UINT8(1, entries[0].bank);
    TEST_ASSERT_EQUAL_CHAR('A', entries[0].page);
    TEST_ASSERT_EQUAL_UINT16(1, entries[0].index);
    TEST_ASSERT_EQUAL_STRING("Entry1", entries[0].name);
}

// Test: Catalog stub can populate banks
void test_catalog_stub_banks() {
    AudioDriverCatalogStub driver;
    driver.catalog_bank_count = 2;

    TEST_ASSERT_EQUAL_UINT8(2, driver.getCatalogBankCount());

    const AudioCatalogBank* banks = driver.getCatalogBanks();
    TEST_ASSERT_NOT_NULL(banks);
    TEST_ASSERT_EQUAL_UINT8(1, banks[0].bank);
    TEST_ASSERT_EQUAL_CHAR('A', banks[0].page);
    TEST_ASSERT_EQUAL_STRING("Bank1A", banks[0].dirName);
    TEST_ASSERT_EQUAL_UINT16(10, banks[0].count);
}

// Test: Refresh catalog can be called and returns expected status
void test_catalog_refresh() {
    AudioDriverCatalogStub driver;

    TEST_ASSERT_FALSE(driver.catalog_refreshed);
    bool result = driver.refreshCatalog();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(driver.catalog_refreshed);
}

// Test: Catalog stub with zero entries returns nullptr
void test_catalog_stub_empty_entries() {
    AudioDriverCatalogStub driver;
    driver.catalog_entry_count = 0;

    TEST_ASSERT_EQUAL_UINT16(0, driver.getCatalogEntryCount());
    TEST_ASSERT_NULL(driver.getCatalogEntries());
}

// Test: Catalog stub with zero banks returns nullptr
void test_catalog_stub_empty_banks() {
    AudioDriverCatalogStub driver;
    driver.catalog_bank_count = 0;

    TEST_ASSERT_EQUAL_UINT8(0, driver.getCatalogBankCount());
    TEST_ASSERT_NULL(driver.getCatalogBanks());
}

// Test: Base class capability bits are properly defined
void test_capability_bits_defined() {
    // Ensure each capability bit is unique
    TEST_ASSERT_EQUAL_UINT8(0x01, AudioDriver::AUDIO_CAP_STATUS_QUERY);
    TEST_ASSERT_EQUAL_UINT8(0x02, AudioDriver::AUDIO_CAP_DEVICE_TYPE);
    TEST_ASSERT_EQUAL_UINT8(0x04, AudioDriver::AUDIO_CAP_TRACK_COUNT);
    TEST_ASSERT_EQUAL_UINT8(0x08, AudioDriver::AUDIO_CAP_CURRENT_TRACK);
    TEST_ASSERT_EQUAL_UINT8(0x10, AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING);
    TEST_ASSERT_EQUAL_UINT8(0x20, AudioDriver::AUDIO_CAP_CATALOG);
}

// Test: Catalog structs have correct size and field layout
void test_catalog_struct_layout() {
    AudioCatalogEntry entry{};
    entry.bank = 1;
    entry.page = 'A';
    entry.index = 42;
    strncpy(entry.name, "TestEntry", sizeof(entry.name) - 1);

    TEST_ASSERT_EQUAL_UINT8(1, entry.bank);
    TEST_ASSERT_EQUAL_CHAR('A', entry.page);
    TEST_ASSERT_EQUAL_UINT16(42, entry.index);
    TEST_ASSERT_EQUAL_STRING("TestEntry", entry.name);

    AudioCatalogBank bank{};
    bank.bank = 2;
    bank.page = 'B';
    strncpy(bank.dirName, "TestBank", sizeof(bank.dirName) - 1);
    bank.count = 100;

    TEST_ASSERT_EQUAL_UINT8(2, bank.bank);
    TEST_ASSERT_EQUAL_CHAR('B', bank.page);
    TEST_ASSERT_EQUAL_STRING("TestBank", bank.dirName);
    TEST_ASSERT_EQUAL_UINT16(100, bank.count);
}

// Test: Multiple drivers can coexist with different capabilities
void test_mixed_driver_capabilities() {
    AudioDriverStub stub_driver;
    AudioDriverCatalogStub catalog_driver;

    TEST_ASSERT_EQUAL_UINT8(0, stub_driver.capabilities());
    TEST_ASSERT_NOT_EQUAL(0, catalog_driver.capabilities());
    TEST_ASSERT_TRUE(catalog_driver.capabilities() & AudioDriver::AUDIO_CAP_CATALOG);
}

// Test: audioClampVolume() pure function clamps to valid range
void test_audioClampVolume() {
    // Valid range unchanged
    TEST_ASSERT_EQUAL_UINT8(0, audioClampVolume(0));
    TEST_ASSERT_EQUAL_UINT8(15, audioClampVolume(15));
    TEST_ASSERT_EQUAL_UINT8(30, audioClampVolume(30));

    // Boundary: max is not exceeded
    TEST_ASSERT_EQUAL_UINT8(AUDIO_VOLUME_MAX, audioClampVolume(AUDIO_VOLUME_MAX));

    // Out-of-range values clamped to max
    TEST_ASSERT_EQUAL_UINT8(30, audioClampVolume(31));
    TEST_ASSERT_EQUAL_UINT8(30, audioClampVolume(100));
    TEST_ASSERT_EQUAL_UINT8(30, audioClampVolume(255));
}

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Tear down after each test
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_stub_driver_no_catalog_support);
    RUN_TEST(test_catalog_stub_exposes_capability);
    RUN_TEST(test_catalog_stub_entries);
    RUN_TEST(test_catalog_stub_banks);
    RUN_TEST(test_catalog_refresh);
    RUN_TEST(test_catalog_stub_empty_entries);
    RUN_TEST(test_catalog_stub_empty_banks);
    RUN_TEST(test_capability_bits_defined);
    RUN_TEST(test_catalog_struct_layout);
    RUN_TEST(test_mixed_driver_capabilities);
    RUN_TEST(test_audioClampVolume);

    return UNITY_END();
}
