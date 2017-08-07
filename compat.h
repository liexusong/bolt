#ifdef __LIBGM__
#include <wand/magick_wand.h>

#define MagickWandGenesis()                                \
                InitializeMagick(NULL)
#define MagickGetImageBlob(wand, size)                     \
                MagickWriteImageBlob(wand, size)
#define MagickSetImageCompressionQuality(wand, quality)    \
                MagickSetCompressionQuality(wand, quality)
#else
#include <MagickWand/MagickWand.h>
#endif
