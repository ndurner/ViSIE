#ifndef COLORPROFILE_H
#define COLORPROFILE_H

using ColorParams = struct {
  // values from COLR atom https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFChap3/qtff3.html#//apple_ref/doc/uid/TP40000939-CH205-125526
  unsigned int primaries, transfer, matrix;
};

#endif // COLORPROFILE_H
