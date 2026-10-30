#include "AssetImportJob.h"
#include <QRawFont>
#include <QImage>
#include <QFont>

bool ImportFont(AssetImportJob & job) {
    const uint32_t size=job.Settings.get("FontSize",16).asUInt();
    if (size==0 || size>512) return job.Fail("Font size must be between 1 and 512 pixels");
    QRawFont font(job.SourceBytes,size,QFont::PreferDefaultHinting);
    if (!font.isValid()) return job.Fail("Font decoder rejected source");
    QString characters=QString::fromStdString(job.Settings.get("CustomCharacters","").asString());
    if (characters.isEmpty()) for (int i=32;i<127;++i) characters+=QChar(i);
    const auto points=characters.toUcs4();
    if (points.size()>4096) return job.Fail("Font atlas exceeds 4096 requested characters");
    const int padding=std::max(1,job.Settings.get("CharacterPadding",1).asInt());
    if (padding>64) return job.Fail("Font character padding exceeds 64 pixels");
    QImage atlas(1024,8192,QImage::Format_RGBA8888);
    if (atlas.isNull()) return job.Fail("Cannot allocate font atlas");
    atlas.fill(Qt::transparent);
    Json::Value glyphs(Json::arrayValue);
    std::set<uint32_t> seen;
    int x=padding,y=padding,row=0;
    for (uint32_t point : points) {
        if (job.Cancelled()) return false;
        if (!seen.insert(point).second) continue;
        char32_t code=point;
        const auto indexes=font.glyphIndexesForString(QString::fromUcs4(&code,1));
        if (indexes.isEmpty() || indexes[0]==0) return job.Fail("Font is missing requested character: "+std::to_string(point));
        const quint32 glyph=indexes[0];
        QImage bitmap=font.alphaMapForGlyph(glyph,QRawFont::PixelAntialiasing);
        if (bitmap.width()+padding*2>atlas.width()) return job.Fail("Glyph exceeds atlas width");
        if (x+bitmap.width()+padding>atlas.width()) {x=padding;y+=row+padding;row=0;}
        if (y+bitmap.height()+padding>atlas.height()) return job.Fail("Font atlas exceeds 1024x8192 pixels");
        for (int gy=0;gy<bitmap.height();++gy) for (int gx=0;gx<bitmap.width();++gx) {
            uchar * pixel=atlas.scanLine(y+gy)+(x+gx)*4;
            const int alpha=bitmap.format()==QImage::Format_Alpha8 ? bitmap.constScanLine(gy)[gx] : qGray(bitmap.pixel(gx,gy));
            pixel[0]=pixel[1]=pixel[2]=255;
            pixel[3]=uchar(job.Settings.get("RenderMode","Smooth").asString()=="Raster" ? (alpha>=128?255:0) : alpha);
        }
        const QRectF bounds=font.boundingRect(glyph);
        const auto advances=font.advancesForGlyphIndexes({glyph});
        Json::Value item;
        item["Codepoint"]=point;item["GlyphIndex"]=glyph;item["X"]=x;item["Y"]=y;
        item["Width"]=bitmap.width();item["Height"]=bitmap.height();
        item["BearingX"]=bounds.x();item["BearingY"]=bounds.y();
        item["AdvanceX"]=(advances.empty()?0:advances[0].x())+job.Settings.get("CharacterSpacing",0).asInt();
        glyphs.append(item);
        x+=bitmap.width()+padding;row=std::max(row,bitmap.height());
    }
    const int height=std::max(1,y+row+padding);
    QByteArray pixels;
    for (int line=0;line<height;++line) pixels.append(reinterpret_cast<const char *>(atlas.constScanLine(line)),atlas.width()*4);
    Json::Value texture;texture["Format"]="RGBA8";texture["Width"]=atlas.width();texture["Height"]=height;
    if (!job.Emit(eAssetType::Texture,"atlas","FontAtlas",texture,pixels)) return false;
    Json::Value metadata;
    metadata["Family"]=font.familyName().toStdString();metadata["Ascent"]=font.ascent();metadata["Descent"]=font.descent();
    metadata["PixelSize"]=size;metadata["Glyphs"]=glyphs;metadata["Settings"]=job.Settings;
    metadata["AtlasGuid"]=job.Request.GuidString;
    metadata["AtlasLocalID"]=static_cast<Json::UInt64>(job.ID(eAssetType::Texture,"atlas","FontAtlas"));
    return job.Emit(eAssetType::Font,"main",font.familyName().toStdString(),metadata,
        job.Settings.get("IncludeFontData",true).asBool()?job.SourceBytes:QByteArray{});
}
