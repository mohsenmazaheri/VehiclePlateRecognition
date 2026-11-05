#include "application/line_crossing.h"
#include "application/passage_pipeline.h"
#include "application/plate_voter.h"
#include "domain/plate_format.h"
#include "infrastructure/tracking/byte_tracker.h"
#include "simulation/synthetic_scene.h"

#include <QtTest>

#include <algorithm>
#include <map>
#include <optional>

using namespace vpr;

namespace {

class CollectingSink final : public IPassageSink {
public:
    void onPassage(const PassageEvent& event) override { events.push_back(event); }
    std::vector<PassageEvent> events;
};

std::shared_ptr<CollectingSink> runScene(const std::shared_ptr<sim::SyntheticScene>& scene, double errorRate,
                                         PassageCounters* counters = nullptr)
{
    auto sink = std::make_shared<CollectingSink>();
    PassagePipeline::Dependencies deps{
        std::make_shared<sim::SimVehicleDetector>(scene),
        std::make_shared<sim::SimPlateDetector>(scene),
        std::make_shared<sim::SimPlateReader>(scene, errorRate),
        std::make_shared<ByteTracker>(),
        sink};
    PassagePipeline pipeline(std::move(deps), defaultPlateFormats());
    std::int64_t index = 0;
    for (std::int64_t ms = 0; ms <= scene->durationMs() + 1500; ms += 40)
        pipeline.process(Frame{blankImage(scene->width(), scene->height(), ms), index++});
    pipeline.flush();
    if (counters)
        *counters = pipeline.counters();
    return sink;
}

Track trackAt(int id, double y)
{
    return Track{id, RectF{100, y, 100, 100}, 0.9F};
}

class FixedVehicles final : public IVehicleDetector {
public:
    explicit FixedVehicles(std::vector<Detection> found) : detections(std::move(found)) {}
    std::vector<Detection> detect(const Image&) override { return detections; }
    std::vector<Detection> detections;
};

// Every detection becomes a track at once, numbered by its position in the list.
class PassThroughTracker final : public ITracker {
public:
    TrackUpdate update(const std::vector<Detection>& detections, std::int64_t) override
    {
        TrackUpdate update;
        int id = 0;
        for (const Detection& detection : detections)
            update.active.push_back(Track{++id, detection.box, detection.confidence});
        return update;
    }
};

class ScriptedPlates final : public IPlateDetector {
public:
    std::vector<Detection> detect(const Image& crop) override
    {
        originYs.push_back(crop.originY);
        if (onlyWhenWidthBelow > 0 && crop.width >= onlyWhenWidthBelow)
            return {};
        return boxes;
    }
    std::vector<Detection> boxes;
    std::vector<int> originYs;
    int onlyWhenWidthBelow = 0;
};

class FixedReader final : public IPlateReader {
public:
    std::optional<PlateReading> read(const Image&) override
    {
        ++reads;
        return PlateReading{"21QA308", 0.9F};
    }
    int reads = 0;
};

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void armenianPrivatePlateIsAccepted()
    {
        const auto match = defaultPlateFormats().classify("34 ab-567");
        QVERIFY(match.has_value());
        QCOMPARE(match->countryCode, std::string("AM"));
        QCOMPARE(match->canonical, std::string("34AB567"));
        QCOMPARE(match->display, std::string("34 AB 567"));
    }

    void armenianThreeDigitPrivatePlateIsAccepted()
    {
        const auto match = defaultPlateFormats().classify("123AB45");
        QVERIFY(match.has_value());
        QCOMPARE(match->countryCode, std::string("AM"));
        QCOMPARE(match->display, std::string("123 AB 45"));
    }

    void iranianPlateWithPersianDigitsIsNormalized()
    {
        const std::string raw = encodeUtf8(U"۱۲ ب ۳۴۵ ۶۷");
        const auto match = defaultPlateFormats().classify(raw);
        QVERIFY(match.has_value());
        QCOMPARE(match->countryCode, std::string("IR"));
        QCOMPARE(match->canonical, encodeUtf8(U"12ب34567"));
        QCOMPARE(match->display, encodeUtf8(U"12 ‎ب‎ 345 67"));
    }

    void otherPlateTypesAreNotAccepted()
    {
        const auto formats = defaultPlateFormats();
        QVERIFY(!formats.classify("1234Z").has_value());                // Armenian public transport, yellow
        QVERIFY(!formats.classify(encodeUtf8(U"12ت34567")).has_value()); // Iranian taxi letter
        QVERIFY(!formats.classify("").has_value());
    }

    void newCountryNeedsOnlyADefinition()
    {
        PlateFormatRegistry formats = defaultPlateFormats();
        formats.add(std::make_shared<PatternPlateFormat>("GE", "LLDDDLL", U"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "##-###-##"));
        const auto match = formats.classify("AB123CD");
        QVERIFY(match.has_value());
        QCOMPARE(match->countryCode, std::string("GE"));
        QCOMPARE(match->display, std::string("AB-123-CD"));
    }

    void voterFixesSingleCharacterErrors()
    {
        const PlateFormatRegistry formats = defaultPlateFormats();
        PlateVoter voter(&formats);
        voter.add({"34AB567", 0.8F});
        voter.add({"34AB561", 0.8F});
        voter.add({"31AB567", 0.8F});
        const auto result = voter.result();
        QVERIFY(result && result->valid);
        QCOMPARE(result->canonical, std::string("34AB567"));
    }

    void voterIgnoresInvalidReadingsWhenAValidOneExists()
    {
        const PlateFormatRegistry formats = defaultPlateFormats();
        PlateVoter voter(&formats);
        voter.add({"34A8567X", 0.9F});
        voter.add({"34A8567X", 0.9F});
        voter.add({"34AB567", 0.5F});
        const auto result = voter.result();
        QVERIFY(result && result->valid);
        QCOMPARE(result->canonical, std::string("34AB567"));
    }

    void aFewValidReadingsOfAnotherPlateDoNotWin()
    {
        // Seen on road video: a bus with a plate of no configured format, and once the plate of the car beside it.
        const PlateFormatRegistry formats = defaultPlateFormats();
        PlateVoter voter(&formats);
        for (int i = 0; i < 4; ++i)
            voter.add({"1234Z", 0.9F});
        voter.add({"56CD789", 0.9F});
        const auto result = voter.result();
        QVERIFY(result.has_value());
        QVERIFY(!result->valid);
        QCOMPARE(result->canonical, std::string("1234Z"));
    }

    void unsureReadingsAreDropped()
    {
        auto plates = std::make_shared<ScriptedPlates>();
        plates->boxes = {{RectF{10, 80, 40, 10}, 0.9F}};
        auto reader = std::make_shared<FixedReader>();  // always 0.9
        auto sink = std::make_shared<CollectingSink>();
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
        config.readEveryNFrames = 1;
        config.line = CountingLine{{0.0, 0.3}, {1.0, 0.3}};
        config.minReadingConfidence = 0.95F;
        auto vehicles = std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{100, 100, 100, 100}, 0.9F}});
        PassagePipeline pipeline({vehicles, plates, reader, std::make_shared<PassThroughTracker>(), sink},
                                 defaultPlateFormats(), config);
        pipeline.process(Frame{blankImage(1000, 1000, 0), 0});
        vehicles->detections = {{RectF{100, 250, 100, 100}, 0.9F}};  // the bottom passes y = 300
        pipeline.process(Frame{blankImage(1000, 1000, 40), 1});
        pipeline.flush();

        QCOMPARE(reader->reads, 2);
        QCOMPARE(sink->events.size(), std::size_t{1});
        QCOMPARE(sink->events[0].status, PassageStatus::NotVisible);
    }

    void voterReportsUnknownFormat()
    {
        const PlateFormatRegistry formats = defaultPlateFormats();
        PlateVoter voter(&formats);
        voter.add({"1234Z", 0.9F});
        const auto result = voter.result();
        QVERIFY(result.has_value());
        QVERIFY(!result->valid);
        QCOMPARE(result->canonical, std::string("1234Z"));
    }

    void voterWithoutReadingsHasNoResult()
    {
        const PlateFormatRegistry formats = defaultPlateFormats();
        QVERIFY(!PlateVoter(&formats).result().has_value());
    }

    void lineCrossingFiresOncePerTrack()
    {
        LineCrossingDetector line({{0.0, 0.5}, {1.0, 0.5}});
        QVERIFY(line.update({trackAt(1, 100)}, 1000, 1000).empty());
        const auto first = line.update({trackAt(1, 450)}, 1000, 1000);
        QCOMPARE(first.size(), std::size_t(1));
        QCOMPARE(first[0].direction, CrossingDirection::Forward);
        // Box jitter around the line must not produce another event.
        QVERIFY(line.update({trackAt(1, 390)}, 1000, 1000).empty());
        QVERIFY(line.update({trackAt(1, 450)}, 1000, 1000).empty());
    }

    void lineCrossingSeesDirection()
    {
        LineCrossingDetector line({{0.0, 0.5}, {1.0, 0.5}});
        QVERIFY(line.update({trackAt(1, 800)}, 1000, 1000).empty());
        const auto crossing = line.update({trackAt(1, 100)}, 1000, 1000);
        QCOMPARE(crossing.size(), std::size_t(1));
        QCOMPARE(crossing[0].direction, CrossingDirection::Backward);
    }

    void lineCrossingIgnoresVehiclesPastTheSegmentEnd()
    {
        LineCrossingDetector line({{0.5, 0.5}, {1.0, 0.5}});  // right half only
        QVERIFY(line.update({trackAt(1, 100)}, 1000, 1000).empty());
        QVERIFY(line.update({trackAt(1, 450)}, 1000, 1000).empty());  // x = 150, left of the segment
    }

    void movingTheLineDoesNotRepeatAnEvent()
    {
        LineCrossingDetector line({{0.0, 0.5}, {1.0, 0.5}});
        QVERIFY(line.update({trackAt(1, 100)}, 1000, 1000).empty());
        QCOMPARE(line.update({trackAt(1, 450)}, 1000, 1000).size(), std::size_t(1));
        line.setLine({{0.0, 0.7}, {1.0, 0.7}});
        QVERIFY(line.update({trackAt(1, 500)}, 1000, 1000).empty());
        QVERIFY(line.update({trackAt(1, 700)}, 1000, 1000).empty());
    }

    void trackerNeedsConfirmationAndKeepsIdentity()
    {
        ByteTracker tracker;
        const auto at = [](double y) { return std::vector<Detection>{{RectF{100, y, 100, 100}, 0.9F}}; };
        QVERIFY(tracker.update(at(100), 0).active.empty());  // a one frame false positive stays hidden
        QVERIFY(tracker.update(at(110), 40).active.empty());
        const auto confirmed = tracker.update(at(120), 80);
        QCOMPARE(confirmed.active.size(), std::size_t(1));
        const int id = confirmed.active[0].id;
        QCOMPARE(tracker.update(at(130), 120).active[0].id, id);
    }

    void trackerSurvivesDroppedFrames()
    {
        ByteTracker tracker;
        const auto at = [](double y) { return std::vector<Detection>{{RectF{100, y, 100, 100}, 0.9F}}; };
        int id = 0;
        for (int i = 0; i < 5; ++i) {
            const auto update = tracker.update(at(100 + i * 20.0), i * 40);
            if (!update.active.empty())
                id = update.active[0].id;
        }
        // 400 ms gap, moved 200 px: no overlap with the old box, only the prediction can match it.
        const auto after = tracker.update(at(100 + 4 * 20.0 + 200), 4 * 40 + 400);
        QCOMPARE(after.active.size(), std::size_t(1));
        QCOMPARE(after.active[0].id, id);
    }

    void trackerKeepsTrackWithLowConfidenceButDoesNotStartOne()
    {
        ByteTracker tracker;
        const RectF box{100, 100, 100, 100};
        QVERIFY(tracker.update({{box, 0.3F}}, 0).active.empty());
        QVERIFY(tracker.update({{box, 0.3F}}, 40).active.empty());
        QVERIFY(tracker.update({{box, 0.3F}}, 80).active.empty());

        ByteTracker other;
        (void)other.update({{box, 0.9F}}, 0);
        (void)other.update({{box, 0.9F}}, 40);
        const int id = other.update({{box, 0.9F}}, 80).active.at(0).id;
        QCOMPARE(other.update({{box, 0.3F}}, 120).active.at(0).id, id);
    }

    void trackerReportsRemovedTracks()
    {
        ByteTracker tracker;
        const RectF box{100, 100, 100, 100};
        (void)tracker.update({{box, 0.9F}}, 0);
        (void)tracker.update({{box, 0.9F}}, 40);
        const int id = tracker.update({{box, 0.9F}}, 80).active.at(0).id;
        QVERIFY(tracker.update({}, 500).removed.empty());
        const auto gone = tracker.update({}, 1200).removed;
        QCOMPARE(gone.size(), std::size_t(1));
        QCOMPARE(gone[0], id);
    }

    void cropKeepsOriginAndClampsToImage()
    {
        const Image image = blankImage(100, 50, 7);
        const Image crop = image.crop({90, 40, 50, 50});
        QCOMPARE(crop.width, 10);
        QCOMPARE(crop.height, 10);
        QCOMPARE(crop.originX, 90);
        QCOMPARE(crop.originY, 40);
        QCOMPARE(crop.timestampMs, std::int64_t(7));
        QVERIFY(image.crop({200, 200, 10, 10}).empty());
    }

    void pipelineRequiresEveryDependency()
    {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 PassagePipeline(PassagePipeline::Dependencies{}, defaultPlateFormats()));
    }

    void pipelineEmitsOneEventPerVehicle()
    {
        const auto scene = sim::SyntheticScene::demo();
        PassageCounters counters;
        const auto sink = runScene(scene, 0.0, &counters);

        QCOMPARE(sink->events.size(), scene->vehicles().size());
        QCOMPARE(counters.crossed, long(5));
        QCOMPARE(counters.read, long(4));
        QCOMPARE(counters.notVisible, long(1));
        QCOMPARE(counters.unknownFormat, long(0));

        std::map<std::string, std::string> countries;
        for (const PassageEvent& event : sink->events) {
            QVERIFY(!event.vehicleImage.empty());
            if (event.status == PassageStatus::Read)
                countries[event.plateText] = event.countryCode;
        }
        QCOMPARE(countries.at("34AB567"), std::string("AM"));
        QCOMPARE(countries.at("70XY123"), std::string("AM"));
        QCOMPARE(countries.at(encodeUtf8(U"12ب34567")), std::string("IR"));
        QCOMPARE(countries.at(encodeUtf8(U"77م12313")), std::string("IR"));
    }

    void pipelineVotesOutNoisyReadings()
    {
        const auto scene = sim::SyntheticScene::demo();
        const auto sink = runScene(scene, 0.3);
        std::vector<std::string> plates;
        for (const PassageEvent& event : sink->events) {
            if (event.status == PassageStatus::Read)
                plates.push_back(event.plateText);
        }
        std::sort(plates.begin(), plates.end());
        std::vector<std::string> expected{"34AB567", "70XY123", encodeUtf8(U"12ب34567"),
                                          encodeUtf8(U"77م12313")};
        std::sort(expected.begin(), expected.end());
        QCOMPARE(plates, expected);
    }

    void movingTheLineAtRuntimeIsHonoured()
    {
        const auto scene = sim::SyntheticScene::demo();
        auto sink = std::make_shared<CollectingSink>();
        PassagePipeline pipeline({std::make_shared<sim::SimVehicleDetector>(scene),
                                  std::make_shared<sim::SimPlateDetector>(scene),
                                  std::make_shared<sim::SimPlateReader>(scene),
                                  std::make_shared<ByteTracker>(), sink},
                                 defaultPlateFormats());
        // A line outside the picture: nothing can cross it.
        pipeline.setLine({{0.0, 2.0}, {1.0, 2.0}});
        std::int64_t index = 0;
        for (std::int64_t ms = 0; ms <= scene->durationMs() + 1500; ms += 40)
            pipeline.process(Frame{blankImage(scene->width(), scene->height(), ms), index++});
        pipeline.flush();
        QVERIFY(sink->events.empty());
    }

    void readsStayWithinTheBudgetAndStartNearTheLine()
    {
        auto plates = std::make_shared<ScriptedPlates>();
        auto reader = std::make_shared<FixedReader>();
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
        config.readEveryNFrames = 1;
        config.maxPlateReadsPerFrame = 1;
        PassagePipeline pipeline({std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{0, 0, 100, 100}, 0.9F},
                                                                                         {RectF{200, 200, 100, 100}, 0.9F},
                                                                                         {RectF{400, 400, 100, 100}, 0.9F},
                                                                                         {RectF{600, 600, 100, 100}, 0.9F},
                                                                                         {RectF{800, 700, 100, 100}, 0.9F}}),
                                  plates, reader, std::make_shared<PassThroughTracker>(),
                                  std::make_shared<CollectingSink>()},
                                 defaultPlateFormats(), config);

        const int frames = 6;
        for (int i = 0; i < frames; ++i)
            pipeline.process(Frame{blankImage(1000, 1000, i * 40), i});

        QCOMPARE(plates->originYs.size(), std::size_t{frames});  // one read per frame
        // The three vehicles nearest the line take turns, the two far ones wait.
        QCOMPARE(plates->originYs[0], 700);
        for (const int y : plates->originYs)
            QVERIFY(y >= 400);
        QCOMPARE(static_cast<int>(std::count(plates->originYs.begin(), plates->originYs.end(), 400)), 2);
        QCOMPARE(static_cast<int>(std::count(plates->originYs.begin(), plates->originYs.end(), 600)), 2);
        QCOMPARE(static_cast<int>(std::count(plates->originYs.begin(), plates->originYs.end(), 700)), 2);
    }

    void aPlateOfTheVehicleBehindIsNotTaken()
    {
        // The nearer vehicle (id 2) overlaps the one behind it (id 1). The plate of the one behind
        // shows at the top of the nearer vehicle's crop.
        auto plates = std::make_shared<ScriptedPlates>();
        plates->boxes = {{RectF{40, 5, 20, 8}, 0.9F}};
        auto reader = std::make_shared<FixedReader>();
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
        config.readEveryNFrames = 1;
        PassagePipeline pipeline({std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{100, 0, 100, 110}, 0.9F},
                                                                                         {RectF{100, 100, 100, 100}, 0.9F}}),
                                  plates, reader, std::make_shared<PassThroughTracker>(),
                                  std::make_shared<CollectingSink>()},
                                 defaultPlateFormats(), config);

        pipeline.process(Frame{blankImage(500, 500, 0), 0});

        // Both crops were searched and the same spot was returned for each. For the one behind that spot
        // is the only candidate, so it is read. For the nearer one the spot lies near the bottom of the
        // box behind, so it belongs there and is not read a second time.
        QCOMPARE(plates->originYs.size(), std::size_t{2});
        QCOMPARE(reader->reads, 1);
    }

    void thePhotoShowsThePlateEvenIfALargerViewCameLater()
    {
        auto plates = std::make_shared<ScriptedPlates>();
        plates->boxes = {{RectF{10, 80, 40, 10}, 0.9F}};
        plates->onlyWhenWidthBelow = 150;  // later the plate has left the bottom of the picture
        auto reader = std::make_shared<FixedReader>();
        auto sink = std::make_shared<CollectingSink>();
        auto vehicles = std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{100, 100, 100, 100}, 0.9F}});
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
        config.readEveryNFrames = 1;
        PassagePipeline pipeline({vehicles, plates, reader, std::make_shared<PassThroughTracker>(), sink},
                                 defaultPlateFormats(), config);

        pipeline.process(Frame{blankImage(1000, 1000, 0), 0});
        vehicles->detections = {{RectF{100, 800, 300, 200}, 0.9F}};
        pipeline.process(Frame{blankImage(1000, 1000, 40), 1});
        pipeline.flush();

        QCOMPARE(sink->events.size(), std::size_t{1});
        QCOMPARE(sink->events[0].vehicleImage.width, 100);  // not the larger, 300 wide view
        QCOMPARE(sink->events[0].plateText, std::string("21QA308"));
    }

    void aReadPlateIsReportedAtTheCrossing()
    {
        for (const bool reportWhenRead : {false, true}) {
            auto plates = std::make_shared<ScriptedPlates>();
            plates->boxes = {{RectF{10, 80, 40, 10}, 0.9F}};
            auto sink = std::make_shared<CollectingSink>();
            auto vehicles = std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{100, 100, 100, 100}, 0.9F}});
            PipelineConfig config;
            config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
            config.readEveryNFrames = 1;
            config.reportWhenRead = reportWhenRead;
            PassagePipeline pipeline({vehicles, plates, std::make_shared<FixedReader>(),
                                      std::make_shared<PassThroughTracker>(), sink},
                                     defaultPlateFormats(), config);

            pipeline.process(Frame{blankImage(1000, 1000, 0), 0});
            vehicles->detections = {{RectF{100, 850, 100, 100}, 0.9F}};
            pipeline.process(Frame{blankImage(1000, 1000, 40), 1});

            // Without the option the pipeline keeps reading for graceFrames after the crossing.
            QCOMPARE(sink->events.size(), std::size_t{reportWhenRead ? 1U : 0U});
            if (reportWhenRead) {
                QCOMPARE(sink->events[0].plateText, std::string("21QA308"));
                QVERIFY(!sink->events[0].plateImage.empty());
            }
        }
    }

    void aVehicleWithoutReadPlateStillWaitsForIt()
    {
        auto plates = std::make_shared<ScriptedPlates>();  // finds no plate
        auto sink = std::make_shared<CollectingSink>();
        auto vehicles = std::make_shared<FixedVehicles>(std::vector<Detection>{{RectF{100, 100, 100, 100}, 0.9F}});
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.9}, {1.0, 0.9}};
        config.readEveryNFrames = 1;
        config.graceFrames = 3;
        config.reportWhenRead = true;
        PassagePipeline pipeline({vehicles, plates, std::make_shared<FixedReader>(),
                                  std::make_shared<PassThroughTracker>(), sink},
                                 defaultPlateFormats(), config);

        pipeline.process(Frame{blankImage(1000, 1000, 0), 0});
        vehicles->detections = {{RectF{100, 850, 100, 100}, 0.9F}};
        for (int i = 1; i <= 3; ++i) {
            pipeline.process(Frame{blankImage(1000, 1000, i * 40), i});
            QVERIFY(sink->events.empty());
        }
        pipeline.process(Frame{blankImage(1000, 1000, 160), 4});
        QCOMPARE(sink->events.size(), std::size_t{1});
        QCOMPARE(sink->events[0].status, PassageStatus::NotVisible);
    }
};

QTEST_APPLESS_MAIN(TestCore)
#include "test_core.moc"
