#include "infrastructure/onnx/yolox_core.h"

#include <QtTest>

#include <cmath>
#include <memory>
#include <stdexcept>

using namespace vpr;

namespace {

constexpr int kInput = 64;           // grids of 8x8, 4x4 and 2x2
constexpr std::size_t kColumns = 7;  // box, objectness, two classes

std::vector<float> emptyOutput()
{
    return std::vector<float>(yolox::expectedRows(kInput, kInput) * kColumns, 0.0F);
}

// Writes a box into the stride 8 grid. Centre and size are in model input pixels.
void placeBox(std::vector<float>& output, int gx, int gy, double centreX, double centreY, double width,
              double height, float objectness, float classZero, float classOne)
{
    float* row = output.data() + (static_cast<std::size_t>(gy) * (kInput / 8) + gx) * kColumns;
    row[0] = static_cast<float>(centreX / 8.0 - gx);
    row[1] = static_cast<float>(centreY / 8.0 - gy);
    row[2] = static_cast<float>(std::log(width / 8.0));
    row[3] = static_cast<float>(std::log(height / 8.0));
    row[4] = objectness;
    row[5] = classZero;
    row[6] = classOne;
}

std::vector<Detection> decodeOutput(const std::vector<float>& output, double scale, int imageSize,
                                    std::vector<int> classes)
{
    return yolox::decode(output.data(), yolox::expectedRows(kInput, kInput), kColumns, kInput, kInput, scale,
                         imageSize, imageSize, {0.1F, 0.45F, std::move(classes)});
}

} // namespace

class TestYoloxCore : public QObject {
    Q_OBJECT

private slots:
    void inputIsBgrPaddedAtBottomRight()
    {
        // A wide red image: fits the width, leaving padding below.
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(32 * 16 * 3);
        for (std::size_t i = 0; i < pixels->size(); i += 3)
            (*pixels)[i] = 200;
        Image image;
        image.width = 32;
        image.height = 16;
        image.pixels = pixels;

        std::vector<float> tensor;
        const double scale = yolox::prepareInput(image, kInput, kInput, tensor);
        QCOMPARE(scale, 2.0);
        QCOMPARE(tensor.size(), std::size_t{kInput * kInput * 3});

        const auto at = [&](int plane, int y, int x) {
            return tensor[static_cast<std::size_t>(plane) * kInput * kInput + static_cast<std::size_t>(y) * kInput + x];
        };
        QCOMPARE(at(0, 0, 0), 0.0F);    // blue
        QCOMPARE(at(1, 0, 0), 0.0F);    // green
        QCOMPARE(at(2, 0, 0), 200.0F);  // red
        QCOMPARE(at(2, 31, 63), 200.0F);
        QCOMPARE(at(0, 32, 0), 114.0F);  // padding starts right below the image
        QCOMPARE(at(2, 63, 63), 114.0F);
    }

    void rowCountCoversAllThreeStrides()
    {
        QCOMPARE(yolox::expectedRows(640, 640), std::size_t{6400 + 1600 + 400});
        QCOMPARE(yolox::expectedRows(64, 64), std::size_t{64 + 16 + 4});
    }

    void boxReturnsToImageCoordinates()
    {
        auto output = emptyOutput();
        placeBox(output, 2, 3, 20.0, 28.0, 16.0, 8.0, 0.9F, 1.0F, 0.0F);
        const auto found = decodeOutput(output, 0.5, 128, {0});
        QCOMPARE(found.size(), std::size_t{1});
        QVERIFY(std::abs(found[0].box.x - 24.0) < 1e-3);
        QVERIFY(std::abs(found[0].box.y - 48.0) < 1e-3);
        QVERIFY(std::abs(found[0].box.w - 32.0) < 1e-3);
        QVERIFY(std::abs(found[0].box.h - 16.0) < 1e-3);
        QVERIFY(std::abs(found[0].confidence - 0.9F) < 1e-5F);
    }

    void classesOutsideTheListAreIgnored()
    {
        auto output = emptyOutput();
        placeBox(output, 2, 3, 20.0, 28.0, 16.0, 8.0, 0.9F, 0.0F, 1.0F);
        QVERIFY(decodeOutput(output, 1.0, 64, {0}).empty());
    }

    void overlappingBoxesOfDifferentClassesMerge()
    {
        auto output = emptyOutput();
        placeBox(output, 2, 3, 20.0, 28.0, 16.0, 16.0, 0.9F, 0.8F, 0.0F);
        placeBox(output, 3, 3, 21.0, 28.0, 16.0, 16.0, 0.9F, 0.0F, 0.6F);
        placeBox(output, 6, 6, 52.0, 52.0, 8.0, 8.0, 0.9F, 0.5F, 0.0F);
        const auto found = decodeOutput(output, 1.0, 64, {0, 1});
        QCOMPARE(found.size(), std::size_t{2});
        QVERIFY(std::abs(found[0].confidence - 0.72F) < 1e-5F);  // the stronger of the merged pair
        QVERIFY(std::abs(found[1].confidence - 0.45F) < 1e-5F);
    }

    void boxesAreClampedToTheImage()
    {
        auto output = emptyOutput();
        placeBox(output, 0, 0, 2.0, 2.0, 16.0, 16.0, 0.9F, 1.0F, 0.0F);
        const auto found = decodeOutput(output, 1.0, 64, {0});
        QCOMPARE(found.size(), std::size_t{1});
        QCOMPARE(found[0].box.x, 0.0);
        QCOMPARE(found[0].box.y, 0.0);
        QVERIFY(std::abs(found[0].box.w - 10.0) < 1e-3);
    }

    void wrongOutputShapeIsRejected()
    {
        const std::vector<float> tooShort(10 * kColumns, 0.0F);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 (void)yolox::decode(tooShort.data(), 10, kColumns, kInput, kInput, 1.0, 64, 64,
                                                     {0.1F, 0.45F, {0}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, (void)decodeOutput(emptyOutput(), 1.0, 64, {2}));
    }
};

QTEST_APPLESS_MAIN(TestYoloxCore)
#include "test_yolox_core.moc"
