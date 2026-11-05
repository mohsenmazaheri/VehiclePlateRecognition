// Plate model input and output handling, checked with hand made tensors. No model runtime is needed.
#include "infrastructure/onnx/plate_core.h"
#include "infrastructure/onnx/resize.h"

#include <QtTest>

#include <memory>
#include <stdexcept>
#include <vector>

using namespace vpr;

namespace {

Image solid(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    auto pixels = std::make_shared<std::vector<std::uint8_t>>();
    pixels->reserve(static_cast<std::size_t>(width) * height * 3);
    for (int i = 0; i < width * height; ++i) {
        pixels->push_back(r);
        pixels->push_back(g);
        pixels->push_back(b);
    }
    Image image;
    image.width = width;
    image.height = height;
    image.pixels = std::move(pixels);
    return image;
}

plate::ReaderConfig smallConfig()
{
    plate::ReaderConfig config;
    config.alphabet = U"AB1_";
    config.padChar = U'_';
    config.plateSlots = 3;
    config.inputWidth = 4;
    config.inputHeight = 2;
    return config;
}

} // namespace

class TestPlateCore : public QObject {
    Q_OBJECT

private slots:
    void aWideImageIsCentredWithGreyAboveAndBelow()
    {
        std::vector<float> tensor;
        const plate::Letterbox box = plate::prepareDetectorInput(solid(200, 100, 255, 0, 51), 100, 100, tensor);
        QCOMPARE(box.scale, 0.5);
        QCOMPARE(box.padX, 0);
        QCOMPARE(box.padY, 25);
        QCOMPARE(tensor.size(), std::size_t{30000});

        const float grey = 114 / 255.0F;
        QCOMPARE(tensor[0], grey);                    // red plane, top row is padding
        QCOMPARE(tensor[50 * 100 + 50], 1.0F);        // red plane, middle of the picture
        QCOMPARE(tensor[10000 + 50 * 100 + 50], 0.0F);  // green plane
        QCOMPARE(tensor[20000 + 50 * 100 + 50], 51 / 255.0F);  // blue plane, so the order is RGB
    }

    void boxesReturnToImageCoordinates()
    {
        const plate::Letterbox box{0.5, 0, 25};
        const std::vector<float> rows{0, 10, 35, 30, 45, 0, 0.9F};
        const auto detections = plate::decodeDetections(rows.data(), 1, 7, box, 200, 100, 0.25F);
        QCOMPARE(detections.size(), std::size_t{1});
        QCOMPARE(detections[0].box.x, 20.0);
        QCOMPARE(detections[0].box.y, 20.0);
        QCOMPARE(detections[0].box.w, 40.0);
        QCOMPARE(detections[0].box.h, 20.0);
        QCOMPARE(detections[0].confidence, 0.9F);
    }

    void weakBoxesAreDroppedAndBoxesStayInsideTheImage()
    {
        const plate::Letterbox box{1.0, 0, 0};
        const std::vector<float> rows{0, 1, 1, 5, 5, 0, 0.1F,    // too weak
                                      0, -9, -9, 500, 500, 0, 0.8F};
        const auto detections = plate::decodeDetections(rows.data(), 2, 7, box, 50, 40, 0.25F);
        QCOMPARE(detections.size(), std::size_t{1});
        QCOMPARE(detections[0].box.x, 0.0);
        QCOMPARE(detections[0].box.w, 50.0);
        QCOMPARE(detections[0].box.h, 40.0);
    }

    void aWrongDetectorOutputIsRefusedButEmptyIsFine()
    {
        const std::vector<float> rows(6, 0.0F);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 (void)plate::decodeDetections(rows.data(), 1, 6, {}, 10, 10, 0.25F));
        QVERIFY(plate::decodeDetections(nullptr, 0, 6, {}, 10, 10, 0.25F).empty());
    }

    void thePublishedConfigFormatIsRead()
    {
        const auto config = plate::parseReaderConfig(
            "# comment: ignored\nmax_plate_slots: 10\nalphabet: '0123_'\r\npad_char: '_'\n"
            "img_height: 64\nimg_width: 128\nplate_regions: [ 'A', 'B',\n                 'C: D' ]\n");
        QCOMPARE(config.plateSlots, 10);
        QVERIFY(config.alphabet == U"0123_");
        QVERIFY(config.padChar == U'_');
        QCOMPARE(config.inputWidth, 128);
        QCOMPARE(config.inputHeight, 64);
    }

    void aBadConfigIsRefused()
    {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, (void)plate::parseReaderConfig("alphabet: 'AB_'\n"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 (void)plate::parseReaderConfig("max_plate_slots: 2\nalphabet: 'AB'\npad_char: '_'\n"
                                                                "img_height: 2\nimg_width: 2\n"));
    }

    void anAlphabetMayHoldPersianLetters()
    {
        const auto config = plate::parseReaderConfig(
            "max_plate_slots: 1\nalphabet: '1\xD8\xA8_'\npad_char: '_'\nimg_height: 2\nimg_width: 2\n");
        QCOMPARE(config.alphabet.size(), std::size_t{3});
        const std::vector<float> probabilities{0.1F, 0.8F, 0.1F};
        const auto reading = plate::decodeText(probabilities.data(), 3, config);
        QVERIFY(reading.has_value());
        QCOMPARE(reading->text, std::string("\xD8\xA8"));
    }

    void trailingPaddingIsRemovedAndTheLowestScoreIsKept()
    {
        const std::vector<float> probabilities{0.9F, 0.0F, 0.1F, 0.0F,   // A
                                               0.0F, 0.0F, 0.6F, 0.4F,   // 1
                                               0.0F, 0.0F, 0.0F, 1.0F};  // padding
        const auto reading = plate::decodeText(probabilities.data(), 12, smallConfig());
        QVERIFY(reading.has_value());
        QCOMPARE(reading->text, std::string("A1"));
        QCOMPARE(reading->confidence, 0.6F);
    }

    void onlyPaddingReadsAsNothing()
    {
        const std::vector<float> probabilities{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
        QVERIFY(!plate::decodeText(probabilities.data(), 12, smallConfig()).has_value());
    }

    void aReaderOutputOfTheWrongSizeIsRefused()
    {
        const std::vector<float> probabilities(11, 0.0F);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, (void)plate::decodeText(probabilities.data(), 11, smallConfig()));
    }

    void thePlateIsStretchedToTheReaderInput()
    {
        std::vector<std::uint8_t> tensor;
        plate::prepareReaderInput(solid(10, 10, 7, 8, 9), smallConfig(), tensor);
        QCOMPARE(tensor.size(), std::size_t{24});
        QCOMPARE(int{tensor[0]}, 7);
        QCOMPARE(int{tensor[1]}, 8);
        QCOMPARE(int{tensor[23]}, 9);
    }
};

QTEST_APPLESS_MAIN(TestPlateCore)
#include "test_plate_core.moc"
