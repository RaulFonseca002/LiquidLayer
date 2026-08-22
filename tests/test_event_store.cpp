#include "liquid/events/FileEventStore.hpp"
#include "liquid/events/MemoryEventStore.hpp"
#include "liquid/events/Replay.hpp"
#include "liquid/events/ValueCodec.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

liquid::SessionId session_id() {
    return liquid::SessionId{0x0102030405060708ULL};
}

liquid::EventStoreMetadata metadata() {
    return liquid::EventStoreMetadata{
        session_id(),
        "0.1.0-test",
        liquid::FeedbackTiming::Deferred,
        1,
        liquid::EventStoreMetadata::currentFileFormatVersion
    };
}

liquid::FileEventStoreOptions recovery_options() {
    liquid::FileEventStoreOptions options;
    options.writableRecovery = true;
    return options;
}

liquid::Value object_value(std::string key, liquid::Value value) {
    liquid::Value::Object object;
    object.emplace(std::move(key), std::move(value));
    return liquid::Value(std::move(object));
}

liquid::Value checkpoint_value(
    liquid::RecordId replayPosition = liquid::RecordId{0}
) {
    liquid::SerializedWorldState state;
    state.session = session_id();
    state.replayPosition = replayPosition;
    return liquid::ReplayProjector{}.checkpoint_payload(state);
}

liquid::Value checkpoint_value(const liquid::EventStore& store) {
    liquid::ReplayProjector projector;
    return projector.checkpoint_payload(
        projector.project(store.metadata(), store.read_all()));
}

liquid::EventData frame_event(std::uint64_t frame) {
    return liquid::EventData{
        liquid::EventType::FrameCompleted,
        1,
        liquid::Value(frame)
    };
}

template <typename Function>
void expect_store_error(Function function) {
    bool thrown = false;
    try {
        function();
    } catch (const liquid::EventStoreError&) {
        thrown = true;
    }
    REQUIRE(thrown);
}

template <typename Function>
void expect_invalid(Function function) {
    bool thrown = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    REQUIRE(thrown);
}

class TemporaryFile {
private:
    std::filesystem::path filePath;

public:
    explicit TemporaryFile(std::string suffix) {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        filePath = std::filesystem::temp_directory_path() /
            ("liquid-event-store-" + std::to_string(nonce) + "-" + suffix);
        std::error_code ignored;
        std::filesystem::remove(filePath, ignored);
        std::filesystem::remove(filePath.string() + ".replacement", ignored);
        std::filesystem::remove(filePath.string() + ".lock", ignored);
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(filePath, ignored);
        std::filesystem::remove(filePath.string() + ".replacement", ignored);
        std::filesystem::remove(filePath.string() + ".lock", ignored);
    }

    const std::filesystem::path& path() const {
        return filePath;
    }
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

std::vector<std::uint8_t> read_hex_fixture(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string hex((std::istreambuf_iterator<char>(input)), {});
    std::vector<std::uint8_t> bytes;
    std::string compact;
    for (char character : hex) {
        if (!std::isspace(static_cast<unsigned char>(character)))
            compact.push_back(character);
    }
    REQUIRE(compact.size() % 2 == 0);
    bytes.reserve(compact.size() / 2);
    for (std::size_t index = 0; index < compact.size(); index += 2) {
        const std::string octet = compact.substr(index, 2);
        bytes.push_back(static_cast<std::uint8_t>(
            std::stoul(octet, nullptr, 16)));
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::size_t> batch_offsets(const std::vector<std::uint8_t>& bytes) {
    const std::vector<std::uint8_t> magic{'L', 'B', 'A', 'T'};
    std::vector<std::size_t> offsets;
    auto cursor = bytes.begin();

    while ((cursor = std::search(cursor, bytes.end(), magic.begin(), magic.end())) !=
           bytes.end()) {
        offsets.push_back(static_cast<std::size_t>(cursor - bytes.begin()));
        ++cursor;
    }

    return offsets;
}

}

TEST_CASE("test_event_store") {
    using namespace liquid;

    Value::Object canonicalObject;
    canonicalObject.emplace("z", Value(std::uint64_t{9}));
    canonicalObject.emplace("a", Value("text"));
    Value canonical(std::move(canonicalObject));
    const auto encoded = encode_value(canonical);
    REQUIRE(decode_value(encoded) == canonical);
    auto withTrailingByte = encoded;
    withTrailingByte.push_back(0);
    expect_invalid([&] { static_cast<void>(decode_value(withTrailingByte)); });
    expect_invalid([] {
        const std::vector<std::uint8_t> unknownTag{255};
        static_cast<void>(decode_value(unknownTag));
    });

    MemoryEventStore memory(metadata(), 8);
    const RecordId first = memory.append(EventData{
        EventType::SessionStarted,
        1,
        object_value("mode", Value("test"))
    });
    REQUIRE(first == RecordId{1});

    const std::vector<EventData> batch{
        EventData{EventType::FrameStarted, 1, Value(std::uint64_t{10})},
        EventData{EventType::FrameCompleted, 1, Value(std::uint64_t{10})}
    };
    REQUIRE((memory.append_batch(batch) ==
            std::vector<RecordId>{RecordId{2}, RecordId{3}}));
    REQUIRE(memory.read_all().size() == 3);
    expect_store_error([&] {
        memory.append(EventData{EventType::FrameStarted, 2, Value()});
    });
    EventStoreMetadata futureMetadata = metadata();
    futureMetadata.fileFormatVersion = 2;
    expect_store_error([&] { MemoryEventStore unsupported(futureMetadata); });

    const RecordId checkpoint = memory.checkpoint(checkpoint_value(memory));
    memory.append(EventData{EventType::FrameCompleted, 1, Value()});
    memory.retain_from_checkpoint(checkpoint);
    const auto retainedMemory = memory.read_all();
    REQUIRE(retainedMemory.front().sequence == checkpoint);
    REQUIRE(retainedMemory.front().type == EventType::Checkpoint);
    REQUIRE(retainedMemory.back().type == EventType::Retention);
    REQUIRE(memory.metadata().fileGeneration == 2);

    TemporaryFile firstFile("first.bin");
    TemporaryFile secondFile("second.bin");
    const std::vector<EventData> deterministicEvents{
        EventData{EventType::SessionStarted, 1, object_value("run", Value("same"))},
        EventData{EventType::FrameCompleted, 1, Value(std::uint64_t{42})}
    };
    {
        FileEventStore store(firstFile.path(), metadata());
        REQUIRE((store.append_batch(deterministicEvents) ==
                std::vector<RecordId>{RecordId{1}, RecordId{2}}));
    }
    {
        FileEventStore store(secondFile.path(), metadata());
        store.append_batch(deterministicEvents);
    }
    REQUIRE(read_bytes(firstFile.path()) == read_bytes(secondFile.path()));
    {
        FileEventStore reopened(firstFile.path(), metadata());
        REQUIRE(reopened.read_all().size() == 2);
        REQUIRE(reopened.read_all().front().payload ==
               deterministicEvents.front().payload);
        expect_store_error([&] {
            FileEventStore secondWriter(firstFile.path(), metadata());
        });
    }

    TemporaryFile recoveredFile("recovered.bin");
    {
        FileEventStore store(recoveredFile.path(), metadata());
        store.append(deterministicEvents.front());
    }
    {
        std::ofstream tail(recoveredFile.path(),
                           std::ios::binary | std::ios::app);
        const std::uint8_t garbage[] = {0xde, 0xad, 0xbe, 0xef};
        tail.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }
    expect_store_error([&] {
        FileEventStore noRecovery(recoveredFile.path(), metadata());
    });
    {
        FileEventStore recovered(
            recoveredFile.path(), metadata(), recovery_options());
        const auto records = recovered.read_all();
        REQUIRE(records.size() == 2);
        REQUIRE(records.back().type == EventType::Recovery);
        REQUIRE(records.back().sequence == RecordId{2});
    }

    TemporaryFile finalCorruptFile("final-corrupt.bin");
    {
        FileEventStore store(finalCorruptFile.path(), metadata());
        store.append(deterministicEvents.front());
    }
    auto finalCorruptBytes = read_bytes(finalCorruptFile.path());
    const auto finalOffsets = batch_offsets(finalCorruptBytes);
    REQUIRE(finalOffsets.size() == 1);
    finalCorruptBytes[finalOffsets.front() + 32] ^= 0x01U;
    write_bytes(finalCorruptFile.path(), finalCorruptBytes);
    {
        FileEventStore recovered(
            finalCorruptFile.path(), metadata(), recovery_options());
        const auto records = recovered.read_all();
        REQUIRE(records.size() == 1);
        REQUIRE(records.front().type == EventType::Recovery);
        REQUIRE(records.front().sequence == RecordId{1});
    }

    TemporaryFile corruptedFile("corrupt.bin");
    {
        FileEventStore store(corruptedFile.path(), metadata());
        store.append(deterministicEvents.front());
        store.append(deterministicEvents.back());
    }
    auto corruptedBytes = read_bytes(corruptedFile.path());
    const auto offsets = batch_offsets(corruptedBytes);
    REQUIRE(offsets.size() == 2);
    corruptedBytes[offsets.front() + 32] ^= 0x01U;
    write_bytes(corruptedFile.path(), corruptedBytes);
    expect_store_error([&] {
        FileEventStore corrupted(
            corruptedFile.path(), metadata(), recovery_options());
    });

    TemporaryFile retainedFile("retained.bin");
    RecordId fileCheckpoint;
    {
        FileEventStore store(retainedFile.path(), metadata());
        store.append(deterministicEvents.front());
        fileCheckpoint = store.checkpoint(checkpoint_value(store));
        store.append(deterministicEvents.back());
        store.retain_from_checkpoint(fileCheckpoint);
        REQUIRE(store.metadata().fileGeneration == 2);
        REQUIRE(store.read_all().front().sequence == fileCheckpoint);
        REQUIRE(store.read_all().back().type == EventType::Retention);
        expect_store_error([&] {
            FileEventStore competingWriter(retainedFile.path(), metadata());
        });
    }
    {
        FileEventStore reopened(retainedFile.path(), metadata());
        REQUIRE(reopened.metadata().fileGeneration == 2);
        REQUIRE(reopened.read_all().front().sequence == fileCheckpoint);
        REQUIRE(reopened.read_all().back().type == EventType::Retention);
    }

}

TEST_CASE("file event store format v1 matches the golden fixture") {
    using namespace liquid;

    TemporaryFile generated("golden-v1.bin");
    EventStoreMetadata goldenMetadata;
    goldenMetadata.session = SessionId{0x0102030405060708ULL};
    goldenMetadata.engineVersion = "0.1.0-golden";
    goldenMetadata.feedbackTiming = FeedbackTiming::Deferred;

    Value::Object payload;
    payload.emplace("key", Value("Light:office"));
    payload.emplace("value", Value(std::uint64_t{70}));
    {
        FileEventStore store(generated.path(), goldenMetadata);
        store.append(EventData{
            EventType::ComponentMutated,
            1,
            Value(std::move(payload))
        });
    }

    const auto fixture = read_hex_fixture(
        std::filesystem::path(LIQUID_TEST_FIXTURE_DIR) /
        "event_store_v1_golden.hex");
    REQUIRE(read_bytes(generated.path()) == fixture);

    FileEventStore reopened(generated.path(), goldenMetadata);
    REQUIRE(reopened.read_all().size() == 1);
    REQUIRE(reopened.read_all().front().type == EventType::ComponentMutated);
}

TEST_CASE("file event store rejects incomplete checkpoints and future versions") {
    using namespace liquid;

    MemoryEventStore memory(metadata());
    expect_store_error([&] {
        memory.append(EventData{
            EventType::Checkpoint,
            1,
            object_value("components", Value(Value::Object{}))
        });
    });

    TemporaryFile headerVersionFile("future-header.bin");
    {
        FileEventStore store(headerVersionFile.path(), metadata());
        store.append(frame_event(1));
    }
    auto headerBytes = read_bytes(headerVersionFile.path());
    REQUIRE(headerBytes.size() > 10);
    headerBytes[8] = 2;
    headerBytes[9] = 0;
    write_bytes(headerVersionFile.path(), headerBytes);
    expect_store_error([&] {
        FileEventStore store(
            headerVersionFile.path(), metadata(), recovery_options());
    });

    TemporaryFile batchVersionFile("future-batch.bin");
    {
        FileEventStore store(batchVersionFile.path(), metadata());
        store.append(frame_event(1));
    }
    auto batchBytes = read_bytes(batchVersionFile.path());
    const auto offsets = batch_offsets(batchBytes);
    REQUIRE(offsets.size() == 1);
    batchBytes[offsets.front() + 4] = 2;
    batchBytes[offsets.front() + 5] = 0;
    write_bytes(batchVersionFile.path(), batchBytes);
    expect_store_error([&] {
        FileEventStore store(
            batchVersionFile.path(), metadata(), recovery_options());
    });
    REQUIRE(read_bytes(batchVersionFile.path()) == batchBytes);
}

TEST_CASE("file event store recovers every truncated final-batch prefix") {
    using namespace liquid;

    TemporaryFile source("truncation-source.bin");
    {
        FileEventStore store(source.path(), metadata());
        store.append(frame_event(1));
        store.append(frame_event(2));
    }

    const auto complete = read_bytes(source.path());
    const auto offsets = batch_offsets(complete);
    REQUIRE(offsets.size() == 2);
    const std::size_t finalBatchBytes = complete.size() - offsets[1];

    for (std::size_t retained = 1; retained < finalBatchBytes; ++retained) {
        TemporaryFile truncated("truncated-" + std::to_string(retained) + ".bin");
        const std::size_t cut = offsets[1] + retained;
        write_bytes(truncated.path(), std::vector<std::uint8_t>(
            complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(cut)));

        FileEventStore recovered(
            truncated.path(), metadata(), recovery_options());
        const auto records = recovered.read_all();
        REQUIRE(records.size() == 2);
        REQUIRE(records.front().payload == Value(std::uint64_t{1}));
        REQUIRE(records.back().type == EventType::Recovery);
        REQUIRE(records.back().sequence == RecordId{2});
    }
}

TEST_CASE("recovery replacement preserves either the old or evidenced file") {
    using namespace liquid;

    TemporaryFile beforeReplace("recovery-before-replace.bin");
    {
        FileEventStore store(beforeReplace.path(), metadata());
        store.append(frame_event(1));
    }
    {
        std::ofstream tail(beforeReplace.path(), std::ios::binary | std::ios::app);
        const std::uint8_t garbage[] = {0xaa, 0xbb, 0xcc};
        tail.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }
    const auto original = read_bytes(beforeReplace.path());
    FileEventStoreOptions beforeOptions;
    beforeOptions.writableRecovery = true;
    beforeOptions.faultInjector = [](FileEventStoreFaultPoint point) {
        if (point == FileEventStoreFaultPoint::RecoveryReplacementReady)
            throw EventStoreError("injected recovery failure before replacement");
    };
    expect_store_error([&] {
        FileEventStore store(beforeReplace.path(), metadata(), beforeOptions);
    });
    REQUIRE(read_bytes(beforeReplace.path()) == original);
    REQUIRE(!std::filesystem::exists(beforeReplace.path().string() + ".replacement"));
    {
        FileEventStore recovered(
            beforeReplace.path(), metadata(), recovery_options());
        REQUIRE(recovered.read_all().back().type == EventType::Recovery);
    }

    TemporaryFile afterReplace("recovery-after-replace.bin");
    {
        FileEventStore store(afterReplace.path(), metadata());
        store.append(frame_event(1));
    }
    {
        std::ofstream tail(afterReplace.path(), std::ios::binary | std::ios::app);
        const std::uint8_t garbage[] = {0xdd, 0xee};
        tail.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }
    FileEventStoreOptions afterOptions;
    afterOptions.writableRecovery = true;
    afterOptions.faultInjector = [](FileEventStoreFaultPoint point) {
        if (point == FileEventStoreFaultPoint::RecoveryReplaced)
            throw EventStoreError("injected recovery failure after replacement");
    };
    expect_store_error([&] {
        FileEventStore store(afterReplace.path(), metadata(), afterOptions);
    });
    {
        FileEventStore reopened(afterReplace.path(), metadata());
        REQUIRE(reopened.read_all().size() == 2);
        REQUIRE(reopened.read_all().back().type == EventType::Recovery);
    }
}

TEST_CASE("compaction replacement is atomic and post-replace failures fault the store") {
    using namespace liquid;

    TemporaryFile beforeReplace("compaction-before-replace.bin");
    bool injectBefore = true;
    FileEventStoreOptions beforeOptions;
    beforeOptions.faultInjector = [&injectBefore](FileEventStoreFaultPoint point) {
        if (injectBefore &&
            point == FileEventStoreFaultPoint::CompactionReplacementReady) {
            throw EventStoreError("injected compaction failure before replacement");
        }
    };
    {
        FileEventStore store(beforeReplace.path(), metadata(), beforeOptions);
        store.append(frame_event(1));
        const RecordId checkpoint = store.checkpoint(checkpoint_value(store));
        store.append(frame_event(2));
        const auto original = read_bytes(beforeReplace.path());
        expect_store_error([&] { store.retain_from_checkpoint(checkpoint); });
        REQUIRE(read_bytes(beforeReplace.path()) == original);
        REQUIRE(!std::filesystem::exists(
            beforeReplace.path().string() + ".replacement"));
        injectBefore = false;
        store.append(frame_event(3));
    }

    TemporaryFile afterReplace("compaction-after-replace.bin");
    FileEventStoreOptions afterOptions;
    afterOptions.faultInjector = [](FileEventStoreFaultPoint point) {
        if (point == FileEventStoreFaultPoint::CompactionReplaced)
            throw EventStoreError("injected compaction failure after replacement");
    };
    RecordId checkpoint;
    {
        FileEventStore store(afterReplace.path(), metadata(), afterOptions);
        store.append(frame_event(1));
        checkpoint = store.checkpoint(checkpoint_value(store));
        store.append(frame_event(2));
        expect_store_error([&] { store.retain_from_checkpoint(checkpoint); });
        REQUIRE(store.metadata().fileGeneration == 2);
        REQUIRE(store.read_all().front().sequence == checkpoint);
        REQUIRE(store.read_all().back().type == EventType::Retention);
        expect_store_error([&] { store.append(frame_event(3)); });
    }
    {
        FileEventStore reopened(afterReplace.path(), metadata());
        REQUIRE(reopened.metadata().fileGeneration == 2);
        REQUIRE(reopened.read_all().front().sequence == checkpoint);
        REQUIRE(reopened.read_all().back().type == EventType::Retention);
    }
}

TEST_CASE("retention rejects stale checkpoints without changing either store") {
    using namespace liquid;

    MemoryEventStore memory(metadata(), 5);
    memory.append(frame_event(1));
    const RecordId memoryCheckpoint = memory.checkpoint(
        checkpoint_value(RecordId{1}));
    const auto memoryRecords = memory.read_all();
    const std::uint64_t memoryGeneration = memory.metadata().fileGeneration;
    expect_store_error([&] { memory.retain_from_checkpoint(memoryCheckpoint); });
    REQUIRE(memory.read_all() == memoryRecords);
    REQUIRE(memory.metadata().fileGeneration == memoryGeneration);
    REQUIRE_THROWS_AS(
        ReplayProjector{}.project(memory.metadata(), memory.read_all()),
        EventStoreError);

    TemporaryFile file("stale-checkpoint.bin");
    FileEventStore durable(file.path(), metadata());
    durable.append(frame_event(1));
    const RecordId fileCheckpoint = durable.checkpoint(
        checkpoint_value(RecordId{1}));
    const auto fileRecords = durable.read_all();
    const auto fileBytes = read_bytes(file.path());
    const std::uint64_t fileGeneration = durable.metadata().fileGeneration;
    expect_store_error([&] { durable.retain_from_checkpoint(fileCheckpoint); });
    REQUIRE(durable.read_all() == fileRecords);
    REQUIRE(read_bytes(file.path()) == fileBytes);
    REQUIRE(durable.metadata().fileGeneration == fileGeneration);
    REQUIRE_THROWS_AS(
        ReplayProjector{}.project(durable.metadata(), durable.read_all()),
        EventStoreError);
}

TEST_CASE("memory retention preflights exhausted record sequences") {
    using namespace liquid;

    MemoryEventStore store(metadata(), 4);
    store.set_next_sequence_for_test(RecordId{
        std::numeric_limits<std::uint64_t>::max() - 1});
    store.append(frame_event(1));
    const RecordId checkpoint = store.checkpoint(checkpoint_value(store));
    const auto records = store.read_all();
    const std::uint64_t generation = store.metadata().fileGeneration;

    expect_store_error([&] { store.retain_from_checkpoint(checkpoint); });
    REQUIRE(store.read_all() == records);
    REQUIRE(store.metadata().fileGeneration == generation);
}

#ifndef _WIN32
TEST_CASE("file event store refuses symbolic-link paths") {
    using namespace liquid;

    TemporaryFile target("symlink-target.bin");
    write_bytes(target.path(), std::vector<std::uint8_t>{1, 2, 3, 4});
    TemporaryFile link("symlink.bin");
    std::filesystem::create_symlink(target.path(), link.path());
    expect_store_error([&] { FileEventStore store(link.path(), metadata()); });
    REQUIRE((read_bytes(target.path()) ==
             std::vector<std::uint8_t>{1, 2, 3, 4}));

    TemporaryFile hardLink("hard-link.bin");
    std::filesystem::create_hard_link(target.path(), hardLink.path());
    expect_store_error([&] { FileEventStore store(hardLink.path(), metadata()); });
    REQUIRE((read_bytes(target.path()) ==
             std::vector<std::uint8_t>{1, 2, 3, 4}));
}
#endif
