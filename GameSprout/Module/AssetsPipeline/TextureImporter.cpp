#include "AssetImportJob.h"
#include <QImage>
#include <QBuffer>
#include <QImageReader>
#include <cmath>
#include <algorithm>

namespace {
float Decode(float value, bool srgb) { return !srgb ? value : value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); }
float Encode(float value, bool srgb) { return !srgb ? value : value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f; }
QImage Resize(const QImage & input, int width, int height, bool srgb, bool transparency) {
    QImage output(width, height, QImage::Format_RGBA8888);
    if (output.isNull()) return output;
    const double dx = double(input.width()) / width, dy = double(input.height()) / height;
    for (int y = 0; y < height; ++y) {
        uchar * line = output.scanLine(y);
        for (int x = 0; x < width; ++x) {
            double red = 0, green = 0, blue = 0, alpha = 0, weight = 0, colorWeight = 0;
            const double x0 = x * dx, x1 = (x + 1) * dx, y0 = y * dy, y1 = (y + 1) * dy;
            for (int sy = int(y0); sy < std::min(input.height(), int(std::ceil(y1))); ++sy) {
                const uchar * src = input.constScanLine(sy);
                for (int sx = int(x0); sx < std::min(input.width(), int(std::ceil(x1))); ++sx) {
                    const double w = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)))
                        * (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                    const double a = src[sx * 4 + 3] / 255.0;
                    const double cw = w * (transparency ? a : 1.0);
                    red += Decode(src[sx * 4] / 255.0f, srgb) * cw;
                    green += Decode(src[sx * 4 + 1] / 255.0f, srgb) * cw;
                    blue += Decode(src[sx * 4 + 2] / 255.0f, srgb) * cw;
                    alpha += a * w; weight += w; colorWeight += cw;
                }
            }
            if (colorWeight > 0) { red /= colorWeight; green /= colorWeight; blue /= colorWeight; }
            line[x * 4] = uchar(std::clamp(std::lround(Encode(red, srgb) * 255), 0L, 255L));
            line[x * 4 + 1] = uchar(std::clamp(std::lround(Encode(green, srgb) * 255), 0L, 255L));
            line[x * 4 + 2] = uchar(std::clamp(std::lround(Encode(blue, srgb) * 255), 0L, 255L));
            line[x * 4 + 3] = uchar(std::clamp(std::lround(alpha / weight * 255), 0L, 255L));
        }
    }
    return output;
}
void Bleed(QImage & image) {
    // Extend the nearest known colors into transparent texels, without changing alpha.
    std::vector<uint8_t> known(size_t(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        known[size_t(y) * image.width() + x] = image.constScanLine(y)[x * 4 + 3] != 0;
    for (int pass = 0; pass < 8; ++pass) {
        const QImage previous = image.copy();
        std::vector<uint8_t> next = known;
        bool changed = false;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const size_t index = size_t(y) * image.width() + x;
            if (known[index]) continue;
            int sum[3]{}, count = 0;
            for (int sy = std::max(0, y - 1); sy <= std::min(image.height() - 1, y + 1); ++sy)
                for (int sx = std::max(0, x - 1); sx <= std::min(image.width() - 1, x + 1); ++sx) {
                    if (!known[size_t(sy) * image.width() + sx]) continue;
                    const uchar * pixel = previous.constScanLine(sy) + sx * 4;
                    for (int c = 0; c < 3; ++c) sum[c] += pixel[c];
                    ++count;
                }
            if (count) {
                uchar * pixel = image.scanLine(y) + x * 4;
                for (int c = 0; c < 3; ++c) pixel[c] = uchar(sum[c] / count);
                next[index] = 1; changed = true;
            }
        }
        known.swap(next);
        if (!changed) break;
    }
}
}

bool ImportTextureBytes(AssetImportJob & job, QByteArray bytes, const std::string & key,
    const std::string & name, const Json::Value & settings) {
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    QImage image = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return job.Fail("Image decoder: " + reader.errorString().toStdString());
    const bool srgb = settings.get("ColorSpace", "SRGB").asString() == "SRGB";
    const bool transparency = settings.get("AlphaIsTransparency", false).asBool();
    if (settings.get("AlphaSource", "FromSource").asString() == "None") {
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.scanLine(y)[x * 4 + 3] = 255;
    }
    const int maximum = settings.get("MaxSize", 2048).asInt();
    if (maximum <= 0) return job.Fail("Invalid texture maximum size");
    if (std::max(image.width(), image.height()) > maximum) {
        const double scale = double(maximum) / std::max(image.width(), image.height());
        image = Resize(image, std::max(1, int(std::lround(image.width() * scale))), std::max(1, int(std::lround(image.height() * scale))), srgb, transparency);
    }
    if (image.isNull()) return job.Fail("Texture allocation failed");
    Json::Value metadata;
    metadata["Format"] = "RGBA8";
    metadata["Settings"] = settings;
    metadata["Width"] = image.width(); metadata["Height"] = image.height();
    metadata["Mips"] = Json::Value(Json::arrayValue);
    QByteArray payload;
    for (;;) {
        if (job.Cancelled()) return false;
        if (transparency) Bleed(image);
        Json::Value mip;
        mip["Width"] = image.width(); mip["Height"] = image.height();
        mip["Offset"] = static_cast<Json::UInt64>(payload.size());
        mip["RowPitch"] = static_cast<Json::UInt64>(image.width()) * 4;
        mip["Size"] = static_cast<Json::UInt64>(image.width()) * image.height() * 4;
        metadata["Mips"].append(mip);
        for (int y = 0; y < image.height(); ++y) payload.append(reinterpret_cast<const char *>(image.constScanLine(y)), image.width() * 4);
        if (!settings.get("GenerateMipmaps", true).asBool() || (image.width() == 1 && image.height() == 1)) break;
        image = Resize(image, std::max(1, image.width() / 2), std::max(1, image.height() / 2), srgb, transparency);
        if (image.isNull()) return job.Fail("Mip allocation failed");
    }
    return job.Emit(eAssetType::Texture, key, name, metadata, payload);
}

bool ImportTexture(AssetImportJob & job) {
    return ImportTextureBytes(job, job.SourceBytes, "main",
        QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(), job.Settings);
}
