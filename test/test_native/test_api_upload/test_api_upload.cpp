// =============================================================================
// test/test_native/test_api_upload/test_api_upload.cpp
//
// Native unit tests for the OTA upload guard and response mapping.
//
// The handlers themselves are about the Update library, which only exists on
// the device, so what is covered here is the part that decides: whether an
// image can possibly fit its partition, and what the client is told about each
// outcome. The round-trips themselves are hardware evidence, recorded on the
// issue.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "api_upload.h"

// Real values from partitions/partitions_ota.csv, so a partition-table change
// that these tests were written against shows up as a failure here.
static constexpr size_t kAppPartitionSize = 0x1A0000;   // 1,703,936 bytes
static constexpr size_t kFsPartitionSize = 0xA0000;     // 655,360 bytes

void setUp() {
}

void tearDown() {
}

void test_image_that_fits_the_partition_is_accepted() {
    // The build's own firmware is ~1.45 MB against a 1.625 MB partition.
    TEST_ASSERT_TRUE(uploadContentLengthFits(1516295, kAppPartitionSize));
    TEST_ASSERT_TRUE(uploadContentLengthFits(180000, kFsPartitionSize));
}

void test_image_exactly_filling_the_partition_is_not_rejected_by_framing() {
    // An image that exactly fills its partition arrives with multipart framing
    // on top, so a guard comparing raw Content-Length against partition size
    // would reject a perfectly valid image. The allowance exists for this.
    TEST_ASSERT_TRUE(uploadContentLengthFits(kAppPartitionSize + 300, kAppPartitionSize));
    TEST_ASSERT_TRUE(
        uploadContentLengthFits(kAppPartitionSize + kUploadMultipartOverheadAllowance,
                                kAppPartitionSize));
}

void test_image_larger_than_the_partition_is_rejected() {
    TEST_ASSERT_FALSE(uploadContentLengthFits(
        kAppPartitionSize + kUploadMultipartOverheadAllowance + 1, kAppPartitionSize));
    // The 4 MB the ticket asked about cannot fit a 1.625 MB app partition.
    TEST_ASSERT_FALSE(uploadContentLengthFits(4 * 1024 * 1024, kAppPartitionSize));
    // Nor 1.5 MB into a 640 KB filesystem partition.
    TEST_ASSERT_FALSE(uploadContentLengthFits(1536 * 1024, kFsPartitionSize));
}

void test_unknown_partition_size_does_not_strand_the_device() {
    // If the partition lookup fails, rejecting every upload would leave no way
    // back except a physical reflash. Let it through and let the write enforce
    // the real bound.
    TEST_ASSERT_TRUE(uploadContentLengthFits(4 * 1024 * 1024, 0));
}

void test_transport_ceiling_clears_every_partition_on_this_board() {
    // The backend's own limit must sit above our guard, or it rejects first
    // with the wrong error shape.
    TEST_ASSERT_GREATER_THAN(kAppPartitionSize + kUploadMultipartOverheadAllowance,
                             kUploadTransportCeiling);
    TEST_ASSERT_GREATER_THAN(kFsPartitionSize + kUploadMultipartOverheadAllowance,
                             kUploadTransportCeiling);
}

void test_success_body_carries_the_transfer_evidence() {
    // The device reboots a second after this response, taking the log ring
    // with it, so these numbers only ever reach the operator here.
    char body[96] = {};
    TEST_ASSERT_TRUE(formatUploadSuccessJson(body, sizeof(body), 1516295, 41234, 8710));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"bytes\":1516295,\"minHeapFree\":41234,\"durationMs\":8710}", body);
}

void test_success_body_reports_overflow_rather_than_truncating() {
    char tiny[16] = {};
    TEST_ASSERT_FALSE(formatUploadSuccessJson(tiny, sizeof(tiny), 1516295, 41234, 8710));
    TEST_ASSERT_FALSE(formatUploadSuccessJson(nullptr, 0, 1, 1, 1));
}

void test_a_post_with_no_image_is_not_a_successful_flash() {
    // Proven on the device before this existed: an empty POST to
    // /upload/firmware answered 200 and rebooted the controller, re-running
    // the image it was already running. An outage with nothing to show for it.
    UploadResponse fw = uploadFailureResponse(UploadTarget::kFirmware, UploadOutcome::kNoImage);
    TEST_ASSERT_EQUAL_INT(400, fw.code);
    TEST_ASSERT_NOT_NULL(strstr(fw.body, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(fw.body, "no image received"));

    UploadResponse fs = uploadFailureResponse(UploadTarget::kFilesystem, UploadOutcome::kNoImage);
    TEST_ASSERT_EQUAL_INT(400, fs.code);
    TEST_ASSERT_NOT_NULL(strstr(fs.body, "no image received"));
}

void test_failure_responses_carry_a_json_error_message_per_target() {
    // data/firmware.js reads jsonData.error to show the operator what went
    // wrong, so every failure body must carry one and the two targets must not
    // be indistinguishable.
    const UploadOutcome failures[] = {UploadOutcome::kRejectedOversize, UploadOutcome::kFailed};
    const int expectedCodes[] = {413, 500};

    for (size_t i = 0; i < 2; ++i) {
        UploadResponse fw = uploadFailureResponse(UploadTarget::kFirmware, failures[i]);
        UploadResponse fs = uploadFailureResponse(UploadTarget::kFilesystem, failures[i]);

        TEST_ASSERT_EQUAL_INT(expectedCodes[i], fw.code);
        TEST_ASSERT_EQUAL_INT(expectedCodes[i], fs.code);
        TEST_ASSERT_NOT_NULL(strstr(fw.body, "\"ok\":false"));
        TEST_ASSERT_NOT_NULL(strstr(fw.body, "\"error\":\""));
        TEST_ASSERT_NOT_NULL(strstr(fs.body, "\"ok\":false"));
        TEST_ASSERT_NOT_NULL(strstr(fs.body, "\"error\":\""));
        TEST_ASSERT_TRUE(strcmp(fw.body, fs.body) != 0);
    }
}

void test_a_consumed_body_that_delivered_no_chunk_is_not_no_image() {
    // The failure this separation exists for: PsychicHttp's multipart parser
    // gives up on an allocation failure without reporting one, drains the rest
    // of the body and returns success, so a 1.5 MB firmware image arrives here
    // as a request that recorded absolutely nothing. Answering "no image
    // received" told the operator their file was the problem when the
    // controller was.
    const UploadOutcome outcome =
        uploadEffectiveOutcome(UploadOutcome::kInProgress, /*sawChunk=*/false,
                               /*contentLength=*/1549772, /*updaterHasError=*/false);
    TEST_ASSERT_EQUAL_INT((int)UploadOutcome::kBodyNotParsed, (int)outcome);

    UploadResponse fw = uploadFailureResponse(UploadTarget::kFirmware, outcome);
    TEST_ASSERT_EQUAL_INT(503, fw.code);
    TEST_ASSERT_NOT_NULL(strstr(fw.body, "\"ok\":false"));
    // Must not read as the client's fault.
    TEST_ASSERT_NULL(strstr(fw.body, "no image received"));
}

void test_an_empty_post_is_still_no_image() {
    // A POST with no body at all delivers no chunk either, and for that request
    // "no image received" is the accurate, actionable answer. Content-Length is
    // what tells the two apart.
    const UploadOutcome outcome =
        uploadEffectiveOutcome(UploadOutcome::kInProgress, /*sawChunk=*/false,
                               /*contentLength=*/0, /*updaterHasError=*/false);
    TEST_ASSERT_EQUAL_INT((int)UploadOutcome::kNoImage, (int)outcome);
    TEST_ASSERT_EQUAL_INT(400, uploadFailureResponse(UploadTarget::kFirmware, outcome).code);
}

void test_a_parsed_body_carrying_no_image_is_no_image_not_a_parse_failure() {
    // Chunks arrived, so the parser worked; the part just held nothing to
    // write. That is a client-side problem and must keep its 400.
    const UploadOutcome outcome =
        uploadEffectiveOutcome(UploadOutcome::kNoImage, /*sawChunk=*/true,
                               /*contentLength=*/300, /*updaterHasError=*/false);
    TEST_ASSERT_EQUAL_INT((int)UploadOutcome::kNoImage, (int)outcome);
}

void test_outcomes_decided_during_the_body_survive_the_mapping() {
    // Oversize and write failures are decided while chunks are streaming and
    // must not be reinterpreted afterwards.
    TEST_ASSERT_EQUAL_INT(
        (int)UploadOutcome::kRejectedOversize,
        (int)uploadEffectiveOutcome(UploadOutcome::kRejectedOversize, true, 4000000, false));
    TEST_ASSERT_EQUAL_INT(
        (int)UploadOutcome::kFailed,
        (int)uploadEffectiveOutcome(UploadOutcome::kFailed, true, 1549772, false));
}

void test_a_finalized_image_with_a_latched_updater_error_is_not_success() {
    TEST_ASSERT_EQUAL_INT(
        (int)UploadOutcome::kComplete,
        (int)uploadEffectiveOutcome(UploadOutcome::kComplete, true, 1549772, false));
    // An error the library latched without failing a call the handler checked
    // must not be reported to the operator as a successful flash.
    TEST_ASSERT_EQUAL_INT(
        (int)UploadOutcome::kFailed,
        (int)uploadEffectiveOutcome(UploadOutcome::kComplete, true, 1549772, true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_image_that_fits_the_partition_is_accepted);
    RUN_TEST(test_image_exactly_filling_the_partition_is_not_rejected_by_framing);
    RUN_TEST(test_image_larger_than_the_partition_is_rejected);
    RUN_TEST(test_unknown_partition_size_does_not_strand_the_device);
    RUN_TEST(test_transport_ceiling_clears_every_partition_on_this_board);
    RUN_TEST(test_success_body_carries_the_transfer_evidence);
    RUN_TEST(test_success_body_reports_overflow_rather_than_truncating);
    RUN_TEST(test_a_post_with_no_image_is_not_a_successful_flash);
    RUN_TEST(test_failure_responses_carry_a_json_error_message_per_target);
    RUN_TEST(test_a_consumed_body_that_delivered_no_chunk_is_not_no_image);
    RUN_TEST(test_an_empty_post_is_still_no_image);
    RUN_TEST(test_a_parsed_body_carrying_no_image_is_no_image_not_a_parse_failure);
    RUN_TEST(test_outcomes_decided_during_the_body_survive_the_mapping);
    RUN_TEST(test_a_finalized_image_with_a_latched_updater_error_is_not_success);
    return UNITY_END();
}
