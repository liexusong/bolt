#ifdef __LIBGM__
#include <wand/magick_wand.h>

#define MagickWandGenesis()
#define MagickGetImageBlob(wand, size)                         \
                MagickWriteImageBlob(wand, size)
#define MagickSetImageCompressionQuality(wand, quality)        \
                MagickSetCompressionQuality(wand, quality)
#define MagickCompositeImage(wand, cwand, compose, bool, x, y)  \
                MagickCompositeImage(wand, cwand, compose, x, y)
#define MagickResizeImage(wand, width, height, filter)          \
                MagickResizeImage(wand, width, height, filter, 1.0)

#else
#include <MagickWand/MagickWand.h>
#endif
