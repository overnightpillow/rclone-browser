#include "osx_helper.h"
#include <ApplicationServices/ApplicationServices.h>
#include <Cocoa/Cocoa.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {

// Qt6 removed QtMacExtras, which provided QtMac::fromCGImageRef. Render the
// CGImage into a premultiplied ARGB32 QImage via a bitmap context over the
// QImage's own scanlines.
QImage imageFromCGImage(CGImageRef image) {
  const size_t width = CGImageGetWidth(image);
  const size_t height = CGImageGetHeight(image);
  if (width == 0 || height == 0) {
    return QImage();
  }

  QImage result(static_cast<int>(width), static_cast<int>(height),
                QImage::Format_ARGB32_Premultiplied);
  if (result.isNull()) {
    return QImage();
  }
  result.fill(Qt::transparent);

  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      result.bits(), width, height, 8, static_cast<size_t>(result.bytesPerLine()),
      colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(colorSpace);

  if (!context) {
    return QImage();
  }

  CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
  CGContextRelease(context);

  return result;
}

} // namespace

QIcon osxGetIcon(const QString &extension) {
  QIcon icon;
  @autoreleasepool {
    NSString *ext = extension.toNSString();
    // iconForFileType: was deprecated in macOS 12 in favour of the UTType-based
    // iconForContentType:.
    UTType *type = [UTType typeWithFilenameExtension:ext];
    NSImage *image = type ? [[NSWorkspace sharedWorkspace] iconForContentType:type]
                          : [[NSWorkspace sharedWorkspace]
                                iconForContentType:UTTypeData];
    if (!image) {
      return icon;
    }

    NSRect rect = NSMakeRect(0, 0, image.size.width, image.size.height);
    CGImageRef imageRef = [image CGImageForProposedRect:&rect
                                                context:nil
                                                  hints:nil];
    if (imageRef) {
      QImage converted = imageFromCGImage(imageRef);
      if (!converted.isNull()) {
        icon = QIcon(QPixmap::fromImage(converted));
      }
    }
  }
  return icon;
}

void osxShowDockIcon() {
  ProcessSerialNumber psn = {0, kCurrentProcess};
  TransformProcessType(&psn, kProcessTransformToForegroundApplication);
}

void osxHideDockIcon() {
  ProcessSerialNumber psn = {0, kCurrentProcess};
  TransformProcessType(&psn, kProcessTransformToUIElementApplication);
}
